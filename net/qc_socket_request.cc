#include "qc_public.h"
#include "qc_memory.h"
#include "qc_socket.h"
#include "qc_lockmutex.h"
#include "qc_conf.h"
#include <errno.h> //errno
// #include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <pthread.h> //多线程
#include <stdlib.h>
#include <string.h>

// 数据来时的读处理函数,本函数会被qc_epoll_process_events()所调用
void CSocket::qc_read_request_handler(lpqc_connection_t pConn)
{
    bool isflood = false; // 是否flood攻击

    // 收包，必须保证 c->precvbuf指向正确的收包位置，保证c->irecvlen指向正确的收包宽度
    ssize_t reco = recvproc(pConn, pConn->precvbuf, pConn->irecvlen);
    if (reco <= 0)
    {
        // recvproc已经处理过了，这里直接返回
        return;
    }

    // 判断收到了多少数据
    if (pConn->curStat == _PKG_HD_INIT) // 连接建立起来时肯定是这个状态
    {
        if (reco == m_iLenPkgHeader) // 正好接收完整包头
        {
            qc_wait_request_handler_proc_p1(pConn, isflood);
        }
        else
        {
            // 收到的包头不完整--我们不能预料每个包的长度，也不能预料各种拆包/粘包情况，所以收到不完整包头【也算是缺包】是很可能的；
            pConn->curStat = _PKG_HD_RECVING;
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    else if (pConn->curStat == _PKG_HD_RECVING)
    {
        if (pConn->irecvlen == reco) // 说明接收完包头了
        {
            qc_wait_request_handler_proc_p1(pConn, isflood);
        }
        else
        {
            // 还没有接收完，继续
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    else if (pConn->curStat == _PKG_BD_INIT)
    {
        // 准备接收包体
        if (reco == pConn->irecvlen)
        {
            // 收到的宽度等于要收的宽度，包体也接收完整了
            if (m_floodAkEnable == 1)
            {
                isflood = TestFlood(pConn);
            }
            qc_wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            // 还需要继续收包体
            pConn->curStat = _PKG_BD_RECVING;
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    else if (pConn->curStat == _PKG_BD_RECVING)
    {
        if (pConn->irecvlen == reco)
        {
            if (m_floodAkEnable == 1)
            {
                isflood = TestFlood(pConn);
            }
            qc_wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    } // end  if (pConn->curStat == _PKG_HD_INIT)

    if (isflood == true)
    {
        zdClosesocketProc(pConn);
    }

    return;
}

// 接收从客户端来的数据专用函数
ssize_t CSocket::recvproc(lpqc_connection_t pConn, char *buff, ssize_t buflen)
{
    ssize_t n;
    n = recv(pConn->fd, buff, buflen, 0);
    if (n == 0)
    {
        zdClosesocketProc(pConn);
        return -1;
    }
    // 客户端没断开连接
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            qc_log_stderr(errno, "CSocket::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！"); // epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1;                                                                                              // 不当做错误处理，只是简单返回
        }
        // EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        // 例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。
        if (errno == EINTR) // 这个不算错误，参考nginx，官方nginx这个就不算错误；
        {
            // 我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            qc_log_stderr(errno, "CSocket::recvproc()中errno == EINTR成立，出乎我意料！"); // epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1;                                                                     // 不当做错误处理，只是简单返回
        }
        if (errno == ECONNRESET) // #define ECONNRESET 104 /* Connection reset by peer */
        {
            // 如果客户端没有正常关闭socket连接，却关闭了整个运行程序【真是够粗暴无理的，应该是直接给服务器发送rst包而不是4次挥手包完成连接断开】，那么会产生这个错误

            // do nothing
        }
        else
        {
            // 能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            if (errno == EBADF) // #define EBADF   9 /* Bad file descriptor */
            {
                // 因为多线程，偶尔会干掉socket，所以不排除产生这个错误的可能性
            }
            else
            {
                qc_log_stderr(errno, "CSocket::recvproc()中发生错误，我打印出来看看是啥错误！"); // 正式运营时可以考虑这些日志打印去掉
            }
        }
        zdClosesocketProc(pConn);
        return -1;
    }

    return n;
}
// 包头收完整后的处理，我们称为包处理阶段1：写成函数，方便复用
void CSocket::qc_wait_request_handler_proc_p1(lpqc_connection_t pConn, bool &isflood)
{
    CMemory *p_memory = CMemory::GetInstance();

    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo; // 正好收到包头时，包头信息肯定是在dataHeadInfo里；

    unsigned short e_pkgLen;
    e_pkgLen = ntohs(pPkgHeader->pkgLen); // 注意这里网络序转本机序，所有传输到网络上的2字节数据，都要用htons()转成网络序，所有从网络上收到的2字节数据，都要用ntohs()转成本机序
                                          // ntohs/htons的目的就是保证不同操作系统数据之间收发的正确性，【不管客户端/服务器是什么操作系统，发送的数字是多少，收到的就是多少】
                                          // 不明白的同学，直接百度搜索"网络字节序" "主机字节序" "c++ 大端" "c++ 小端"
    // 恶意包或者错误包的判断
    if (e_pkgLen < m_iLenPkgHeader)
    {
        // 伪造包/或者包错误，否则整个包长怎么可能比包头还小？（整个包长是包头+包体，就算包体为0字节，那么至少e_pkgLen == m_iLenPkgHeader）
        // 报文总长度 < 包头长度，认定非法用户，废包
        // 状态和接收位置都复原，这些值都有必要，因为有可能在其他状态比如_PKG_HD_RECVING状态调用这个函数；
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else if (e_pkgLen > (_PKG_MAX_LENGTH - 1000)) // 客户端发来包居然说包长度 > 29000?肯定是恶意包
    {
        // 恶意包，太大，认定非法用户，废包【包头中说这个包总长度这么大，这不行】
        // 状态和接收位置都复原，这些值都有必要，因为有可能在其他状态比如_PKG_HD_RECVING状态调用这个函数；
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else
    {
        // 合法的包头，继续处理
        // 我现在要分配内存开始收包体，因为包体长度并不是固定的，所以内存肯定要new出来；
        char *pTmpBuffer = (char *)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen, false); // 分配内存【长度是 消息头长度  + 包头长度 + 包体长度】，最后参数先给false，表示内存不需要memset;
        pConn->precvMemPointer = pTmpBuffer;                                                 // 内存开始指针

        // a)先填写消息头内容
        LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        ptmpMsgHeader->pConn = pConn;
        ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence; // 收到包时的连接池中连接序号记录到消息头里来，以备将来用；
        // b)再填写包头内容
        pTmpBuffer += m_iLenMsgHeader;                   // 往后跳，跳过消息头，指向包头
        memcpy(pTmpBuffer, pPkgHeader, m_iLenPkgHeader); // 直接把收到的包头拷贝进来
        if (e_pkgLen == m_iLenPkgHeader)
        {
            // 该报文只有包头无包体【我们允许一个包只有包头，没有包体】
            // 这相当于收完整了，则直接入消息队列待后续业务逻辑线程去处理吧
            if (m_floodAkEnable == 1)
            {
                // Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }
            qc_wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            // 开始收包体，注意我的写法
            pConn->curStat = _PKG_BD_INIT;                  // 当前状态发生改变，包头刚好收完，准备接收包体
            pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader; // pTmpBuffer指向包头，这里 + m_iLenPkgHeader后指向包体 weizhi
            pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;   // e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体
        }
    } // end if(e_pkgLen < m_iLenPkgHeader)

    return;
}
// 收到一个完整包后的处理，放到一个函数中，方便调用
void CSocket::qc_wait_request_handler_proc_plast(lpqc_connection_t pConn, bool &isflood)
{
    if (isflood == false)
    {
        g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer); // 入消息队列并触发线程处理消息
    }
    else
    {
        // 对于有攻击倾向的恶人，先把他的包丢掉
        CMemory *p_memory = CMemory::GetInstance();
        p_memory->FreeMemory(pConn->precvMemPointer); // 直接释放掉内存，根本不往消息队列入
    }

    pConn->precvMemPointer = NULL;
    pConn->curStat = _PKG_HD_INIT;         // 收包状态机的状态恢复为原始态，为收下一个包做准备
    pConn->precvbuf = pConn->dataHeadInfo; // 设置好收包的位置
    pConn->irecvlen = m_iLenPkgHeader;     // 设置好要接收数据的大小
    return;
}

// 将数据发送到客户端
ssize_t CSocket::sendproc(lpqc_connection_t c, char *buff, ssize_t size)
{
    // 这里参考借鉴了官方nginx函数ngx_unix_send()的写法
    ssize_t n;

    for (;;)
    {
        n = send(c->fd, buff, size, 0); // send()系统函数， 最后一个参数flag，一般为0；
        if (n > 0)                      // 成功发送了一些数据
        {
            // 发送成功一些数据，但发送了多少，我们这里不关心，也不需要再次send
            // 这里有两种情况
            //(1) n == size也就是想发送多少都发送成功了，这表示完全发完毕了
            //(2) n < size 没发送完毕，那肯定是发送缓冲区满了，所以也不必要重试发送，直接返回吧
            return n; // 返回本次发送的字节数
        }

        if (n == 0)
        {
            // send()返回0？ 一般recv()返回0表示断开,send()返回0，我这里就直接返回0吧【让调用者处理】；我个人认为send()返回0，要么你发送的字节是0，要么对端可能断开。
            // 网上找资料：send=0表示超时，对方主动关闭了连接过程
            // 我们写代码要遵循一个原则，连接断开，我们并不在send动作里处理诸如关闭socket这种动作，集中到recv那里处理，否则send,recv都处理都处理连接断开关闭socket则会乱套
            // 连接断开epoll会通知并且 recvproc()里会处理，不在这里处理
            return 0;
        }

        if (errno == EAGAIN) // 这东西应该等于EWOULDBLOCK
        {
            // 内核缓冲区满，这个不算错误
            return -1; // 表示发送缓冲区满了
        }

        if (errno == EINTR)
        {
            // 这个应该也不算错误 ，收到某个信号导致send产生这个错误？
            // 参考官方的写法，打印个日志，其他啥也没干，那就是等下次for循环重新send试一次了
            qc_log_stderr(errno, "CSocket::sendproc()中send()失败."); // 打印个日志看看啥时候出这个错误
            // 其他不需要做什么，等下次for循环吧
        }
        else
        {
            // 走到这里表示是其他错误码，都表示错误，错误我也不断开socket，我也依然等待recv()来统一处理断开，因为我是多线程，send()也处理断开，recv()也处理断开，很难处理好
            return -2;
        }
    } // end for
}

//设置数据发送时的写处理函数,当数据可写时epoll通知我们，我们在 int CSocket::qc_epoll_process_events(int timer)  中调用此函数
// 数据发送时的写处理函数
void CSocket::qc_write_request_handler(lpqc_connection_t pConn)
{
    CMemory *p_memory = CMemory::GetInstance();
    
    //这些代码的书写可以参照 void* CSocekt::ServerSendQueueThread(void* threadData)
    ssize_t sendsize = sendproc(pConn,pConn->psendbuf,pConn->isendlen);

    if(sendsize > 0 && sendsize != pConn->isendlen)
    {        
        //没有全部发送完毕，数据只发出去了一部分，那么发送到了哪里，剩余多少，继续记录，方便下次sendproc()时使用
        pConn->psendbuf = pConn->psendbuf + sendsize;
		pConn->isendlen = pConn->isendlen - sendsize;	
        return;
    }
    else if(sendsize == -1)
    {
        //这不太可能，可以发送数据时通知我发送数据，我发送时你却通知我发送缓冲区满？
        qc_log_stderr(errno,"CSocket::qc_write_request_handler()时if(sendsize == -1)成立，这很怪异。"); //打印个日志，别的先不干啥
        return;
    }

    if(sendsize > 0 && sendsize == pConn->isendlen) //成功发送完毕，做个通知是可以的；
    {
        //如果是成功的发送完毕数据，则把写事件通知从epoll中干掉吧；其他情况，那就是断线了，等着系统内核把连接从红黑树中干掉即可；
        if(qc_epoll_oper_event(
                pConn->fd,          //socket句柄
                EPOLL_CTL_MOD,      //事件类型，这里是修改【因为我们准备减去写通知】
                EPOLLOUT,           //标志，这里代表要减去的标志,EPOLLOUT：可写【可写的时候通知我】
                1,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                pConn               //连接池中的连接
                ) == -1)
        {
            //有这情况发生？这可比较麻烦，不过先do nothing
            qc_log_stderr(errno,"CSocekt::qc_write_request_handler()中qc_epoll_oper_event()失败。");
        }    
  
    }

    //能走下来的，要么数据发送完毕了，要么对端断开了，那么执行收尾工作吧；
    p_memory->FreeMemory(pConn->psendMemPointer);  //释放内存
    pConn->psendMemPointer = NULL;        
    --pConn->iThrowsendCount;//这个值恢复了，触发下面一行的信号量才有意义
    if(sem_post(&m_semEventSendQueue)==-1)       
        qc_log_stderr(0,"CSocekt::qc_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");

    return;
}

void CSocket::threadRecvProcFunc(char *pMsgBuf)
{   
    return;
}