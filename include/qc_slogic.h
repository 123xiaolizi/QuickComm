#ifndef __SLOGIC_H__
#define __SLOGIC_H__

#include <sys/socket.h>
#include "qc_logiccomm.h"
#include "qc_socket.h"
// 处理逻辑和通讯的子类
class CLogicSocket : public CSocket
{
public:
    CLogicSocket();
    virtual ~CLogicSocket();
    virtual bool Initialize();//初始化

public:
    // 通用收发数据相关函数
    void SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode);

    // 各种业务逻辑相关函数都在之类
    bool _HandleRegister(lpqc_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength);
    bool _HandleLogIn(lpqc_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength);
    bool _HandlePing(lpqc_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength);
    virtual void procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time); // 心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作
public:
    virtual void threadRecvProcFunc(char *pMsgBuf);
};

#endif