#include "qc_memory.h"
#include "qc_public.h"
#include "qc_socket.h"
#include "qc_slogic.h"
#include "qc_lockmutex.h"
#include "qc_conf.h"
#include "qc_macro.h"
#include "qc_crc32.h"
#include "qc_logiccomm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

// 定义成员函数指针
typedef bool (CLogicSocket::*handler)(lpqc_connection_t pConn,       // 连接池中连接的指针
                                      LPSTRUC_MSG_HEADER pMsgHeader, // 消息头指针
                                      char *pPkgBody,                // 包体指针
                                      unsigned short iBodyLength);   // 包体长度

// 用来保存 成员函数指针 的这么个数组
static const handler statusHandler[] =
    {
        // 数组前5个元素，保留，以备将来增加一些基本服务器功能
        &CLogicSocket::_HandlePing, // 【0】：心跳包的实现
        NULL,                       // 【1】：下标从0开始
        NULL,                       // 【2】：下标从0开始
        NULL,                       // 【3】：下标从0开始
        NULL,                       // 【4】：下标从0开始

        // 开始处理具体的业务逻辑
        &CLogicSocket::_HandleRegister, // 【5】：实现具体的注册功能
        &CLogicSocket::_HandleLogIn,    // 【6】：实现具体的登录功能
        //......其他待扩展，比如实现攻击功能，实现加血功能等等；

};
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler) / sizeof(handler) // 整个命令有多少个，编译时即可知道

// 构造函数
CLogicSocket::CLogicSocket()
{
}
// 析构函数
CLogicSocket::~CLogicSocket()
{
}

// 初始化函数【fork()子进程之前干这个事】
// 成功返回true，失败返回false
bool CLogicSocket::Initialize()
{
    // 做一些和本类相关的初始化工作
    //....日后根据需要扩展
    bool bParentInit = CSocket::Initialize(); // 调用父类的同名函数
    return bParentInit;
}

// 处理收到的数据包，由线程池来调用本函数，本函数是一个单独的线程；
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf)
{
    //qc_log_stderr(0,"threadRecvProcFunc()处理");
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                   // 消息头
    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + m_iLenMsgHeader); // 包头
    void *pPkgBody;                                                                // 指向包体的指针
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen);                             // 客户端指明的包宽度【包头+包体】

    if (m_iLenPkgHeader == pkglen)
    {
        // 没有包体，只有包头
        if (pPkgHeader->crc32 != 0) // 只有包头的crc值给0
        {
            return; // crc错，直接丢弃
        }
        pPkgBody = NULL;
    }
    else
    {
        // 有包体，走到这里
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);                     // 针对4字节的数据，网络序转主机序
        pPkgBody = (void *)(pMsgBuf + m_iLenMsgHeader + m_iLenPkgHeader); // 跳过消息头 以及 包头 ，指向包体

        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody, pkglen - m_iLenPkgHeader); // 计算纯包体的crc值
        if (calccrc != pPkgHeader->crc32)                                                                  // 服务器端根据包体计算crc值，和客户端传递过来的包头中的crc32信息比较
        {
            qc_log_stderr(0, "CLogicSocket::threadRecvProcFunc()中CRC错误[服务器:%d/客户端:%d]，丢弃数据!", calccrc, pPkgHeader->crc32); // 正式代码中可以干掉这个信息
            return;                                                                                                                      // crc错，直接丢弃
        }
        else
        {
            // qc_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC正确[服务器:%d/客户端:%d]，不错!",calccrc,pPkgHeader->crc32);
        }
    }

    unsigned short imsgCode = ntohs(pPkgHeader->msgCode); // 消息代码拿出来
    lpqc_connection_t p_Conn = pMsgHeader->pConn;         // 消息头中藏着连接池中连接的指针

    if (p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
    {
        return; // 丢弃不理这种包了【客户端断开了】
    }

    if (imsgCode >= AUTH_TOTAL_COMMANDS) // 无符号数不可能<0
    {
        qc_log_stderr(0, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!", imsgCode); // 这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return;                                                                                   // 丢弃不理这种包【恶意包或者错误包】
    }

    if (statusHandler[imsgCode] == NULL) // 这种用imsgCode的方式可以使查找要执行的成员函数效率特别高
    {
        qc_log_stderr(0, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!", imsgCode); // 这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return;                                                                                                   // 没有相关的处理函数
    }

    (this->*statusHandler[imsgCode])(p_Conn, pMsgHeader, (char *)pPkgBody, pkglen - m_iLenPkgHeader);
    return;
}

// 心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数是子类函数，实现具体的判断动作
void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
    CMemory *p_memory = CMemory::GetInstance();

    if (tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence) // 此连接没断
    {
        lpqc_connection_t p_Conn = tmpmsg->pConn;

        if (/*m_ifkickTimeCount == 1 && */ m_ifTimeOutKick == 1) // 能调用到本函数第一个条件肯定成立，所以第一个条件加不加无所谓，主要是第二个条件
        {
            zdClosesocketProc(p_Conn);
        }
        else if ((cur_time - p_Conn->lastPingTime) > (m_iWaitTime * 3 + 10)) // 超时踢的判断标准就是 每次检查的时间间隔*3，超过这个时间没发送心跳包，就踢【大家可以根据实际情况自由设定】
        {
            // 踢出去【如果此时此刻该用户正好断线，则这个socket可能立即被后续上来的连接复用  如果真有人这么倒霉，赶上这个点了，那么可能错踢，错踢就错踢】
            // qc_log_stderr(0,"时间到不发心跳包，踢出去!");   //感觉OK
            zdClosesocketProc(p_Conn);
        }

        p_memory->FreeMemory(tmpmsg); // 内存要释放
    }
    else // 此连接断了
    {
        p_memory->FreeMemory(tmpmsg); // 内存要释放
    }
    return;
}

// 发送没有包体的数据包给客户端
void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode)
{
    CMemory *p_memory = CMemory::GetInstance();

    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader, false);
    char *p_tmpbuf = p_sendbuf;

    memcpy(p_tmpbuf, pMsgHeader, m_iLenMsgHeader);
    p_tmpbuf += m_iLenMsgHeader;

    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf; // 指向的是我要发送出去的包的包头
    pPkgHeader->msgCode = htons(iMsgCode);
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader);
    pPkgHeader->crc32 = 0;
    msgSend(p_sendbuf);
    return;
}

//----------------------------------------------------------------------------------------------------------
// 处理各种业务逻辑
bool CLogicSocket::_HandleRegister(lpqc_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength)
{
    // qc_log_stderr(0,"执行了CLogicSocket::_HandleRegister()!");

    //(1)首先判断包体的合法性
    if (pPkgBody == NULL) // 具体看客户端服务器约定，如果约定这个命令[msgCode]必须带包体，那么如果不带包体，就认为是恶意包，直接不处理
    {
        return false;
    }

    int iRecvLen = sizeof(STRUCT_REGISTER);
    if (iRecvLen != iBodyLength) // 发送过来的结构大小不对，认为是恶意包，直接不处理
    {
        return false;
    }

    CLock lock(&pConn->logicPorcMutex); // 凡是和本用户有关的访问都互斥

    //(3)取得了整个发送过来的数据
    LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody;
    p_RecvInfo->iType = ntohl(p_RecvInfo->iType);               // 所有数值型,short,int,long,uint64_t,int64_t这种大家都不要忘记传输之前主机网络序，收到后网络转主机序
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0; // 这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0; // 这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。

    //(4)这里可能要考虑 根据业务逻辑，进一步判断收到的数据的合法性，
    // 当前该玩家的状态是否适合收到这个数据等等【比如如果用户没登陆，它就不适合购买物品等等】

    //(5)给客户端返回数据时，一般也是返回一个结构，这个结构内容具体由客户端/服务器协商，这里我们就以给客户端也返回同样的 STRUCT_REGISTER 结构来举例
    // LPSTRUCT_REGISTER pFromPkgHeader =  (LPSTRUCT_REGISTER)(((char *)pMsgHeader)+m_iLenMsgHeader);	//指向收到的包的包头，其中数据后续可能要用到
    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory *p_memory = CMemory::GetInstance();
    CCRC32 *p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(STRUCT_REGISTER);
    // a)分配要发送出去的包的内存

    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false); // 准备发送的格式，这里是 消息头+包头+包体
    // b)填充消息头
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader); // 消息头直接拷贝到这里来
    // c)填充包头
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader); // 指向包头
    pPkgHeader->msgCode = _CMD_REGISTER;                           // 消息代码，可以统一在qc_logiccomm.h中定义
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);              // htons主机序转网络序
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader + iSendLen);        // 整个包的尺寸【包头+包体尺寸】
    // d)填充包体
    LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader); // 跳过消息头，跳过包头，就是包体了
    // 。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；

    // e)包体内容全部确定好后，计算包体的crc32值
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char *)p_sendInfo, iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);

    // f)发送数据包
    msgSend(p_sendbuf);

    return true;
}
bool CLogicSocket::_HandleLogIn(lpqc_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength)
{
    if (pPkgBody == NULL)
    {
        return false;
    }
    int iRecvLen = sizeof(STRUCT_LOGIN);
    if (iRecvLen != iBodyLength)
    {
        return false;
    }
    CLock lock(&pConn->logicPorcMutex);

    LPSTRUCT_LOGIN p_RecvInfo = (LPSTRUCT_LOGIN)pPkgBody;
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;

    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory *p_memory = CMemory::GetInstance();
    CCRC32 *p_crc32 = CCRC32::GetInstance();

    int iSendLen = sizeof(STRUCT_LOGIN);
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader + iSendLen);
    LPSTRUCT_LOGIN p_sendInfo = (LPSTRUCT_LOGIN)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char *)p_sendInfo, iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);
    // qc_log_stderr(0,"成功收到了登录并返回结果！");
    msgSend(p_sendbuf);
    return true;
}

bool CLogicSocket::_HandlePing(lpqc_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength)
{
    if (iBodyLength != 0) // 有包体则认为是 非法包
        return false;

    CLock lock(&pConn->logicPorcMutex); // 凡是和本用户有关的访问都考虑用互斥，以免该用户同时发送过来两个命令达到各种作弊目的
    pConn->lastPingTime = time(NULL);   // 更新该变量

    // 服务器也发送 一个只有包头的数据包给客户端，作为返回的数据
    SendNoBodyPkgToClient(pMsgHeader, _CMD_PING);

    // qc_log_stderr(0,"成功收到了心跳包并返回结果！");
    return true;
}
