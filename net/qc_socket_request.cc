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
    //qc_log_stderr(errno,"收到包了");
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
    
    n = recv(pConn->fd, buff, buflen, 0); //recv()系统函数， 最后一个参数flag，一般为0；     
    if(n == 0)
    {
      
        zdClosesocketProc(pConn);        
        return -1;
    }
    //客户端没断，走这里 
    if(n < 0) //这被认为有错误发生
    {
        //EAGAIN和EWOULDBLOCK[【这个应该常用在hp上】应该是一样的值，表示没收到数据，一般来讲，在ET模式下会出现这个错误，因为ET模式下是不停的recv肯定有一个时刻收到这个errno，但LT模式下一般是来事件才收，所以不该出现这个返回值
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            qc_log_stderr(errno,"CSocket::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }
        if(errno == EINTR)  //这个不算错误，是我参考官方nginx，官方nginx这个就不算错误；
        {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            qc_log_stderr(errno,"CSocket::recvproc()中errno == EINTR成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }

        //所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；

        zdClosesocketProc(pConn);
        return -1;
    }

    //能走到这里的，就认为收到了有效数据
    return n; //返回收到的字节数
}
// 包头收完整后的处理，我们称为包处理阶段1：写成函数，方便复用
void CSocket::qc_wait_request_handler_proc_p1(lpqc_connection_t pConn, bool &isflood)
{
    CMemory *p_memory = CMemory::GetInstance();		

    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo; //正好收到包头时，包头信息肯定是在dataHeadInfo里；

    unsigned short e_pkgLen; 
    e_pkgLen = ntohs(pPkgHeader->pkgLen);
                                                
                                               
    //恶意包或者错误包的判断
    if(e_pkgLen < m_iLenPkgHeader) 
    {
        pConn->curStat = _PKG_HD_INIT;      
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else if(e_pkgLen > (_PKG_MAX_LENGTH-1000))   //客户端发来包居然说包长度 > 29000?肯定是恶意包
    {
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else
    {
        //合法的包头，继续处理
        //我现在要分配内存开始收包体，因为包体长度并不是固定的，所以内存肯定要new出来；
        char *pTmpBuffer  = (char *)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen,false); //分配内存【长度是 消息头长度  + 包头长度 + 包体长度】，最后参数先给false，表示内存不需要memset;        
        pConn->precvMemPointer = pTmpBuffer;  //内存开始指针

        //a)先填写消息头内容
        LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        ptmpMsgHeader->pConn = pConn;
        ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence; //收到包时的连接池中连接序号记录到消息头里来，以备将来用；
        //b)再填写包头内容
        pTmpBuffer += m_iLenMsgHeader;                 //往后跳，跳过消息头，指向包头
        memcpy(pTmpBuffer,pPkgHeader,m_iLenPkgHeader); //直接把收到的包头拷贝进来
        if(e_pkgLen == m_iLenPkgHeader)
        {
            //这相当于收完整了，则直接入消息队列待后续业务逻辑线程去处理吧
            if(m_floodAkEnable == 1) 
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }
            qc_wait_request_handler_proc_plast(pConn,isflood);
            //qc_log_stderr(errno,"收到包了");
        } 
        else
        {
            pConn->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
            pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;  //pTmpBuffer指向包头
            pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体
        }                       
    }  //end if(e_pkgLen < m_iLenPkgHeader) 

    return;
}
// 收到一个完整包后的处理，放到一个函数中，方便调用
void CSocket::qc_wait_request_handler_proc_plast(lpqc_connection_t pConn, bool &isflood)
{
    if (isflood == false)
    {
        g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer); // 入消息队列并触发线程处理消息
        //qc_log_stderr(errno,"入消息队列并触发线程处理消息");//没问题

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
    // 这里参考借鉴了官方nginx函数qc_unix_send()的写法
    ssize_t n;

    for (;;)
    {
        n = send(c->fd, buff, size, 0); // send()系统函数， 最后一个参数flag，一般为0；
        if (n > 0)                      // 成功发送了一些数据
        {

            return n; // 返回本次发送的字节数
        }

        if (n == 0)
        {
            
            // 网上找资料：send=0表示超时，对方主动关闭了连接过程
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
    qc_log_stderr(errno,"触发发包");
    
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
        qc_log_stderr(errno,"CSocket::qc_write_request_handler()时if(sendsize == -1)成立"); //打印个日志，别的先不干啥
        return;
    }

    if(sendsize > 0 && sendsize == pConn->isendlen) //成功发送完毕，做个通知是可以的；
    {
        //如果是成功的发送完毕数据，则把写事件通知从epoll中干掉吧；其他情况，那就是断线了，等着系统内核把连接从红黑树中干掉即可；
        if(qc_epoll_oper_event(
                pConn->fd,          //socket句柄
                EPOLL_CTL_MOD,      //事件类型
                EPOLLOUT,           //标志可写
                1,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                pConn               //连接池中的连接
                ) == -1)
        {
            //有这情况发生？这可比较麻烦，不过先do nothing
            qc_log_stderr(errno,"CSocket::qc_write_request_handler()中qc_epoll_oper_event()失败。");
        }    

        //qc_log_stderr(0,"CSocket::qc_write_request_handler()中数据发送完毕，很好。"); //做个提示吧，商用时可以干掉
        
    }

    //能走下来的，要么数据发送完毕了，要么对端断开了，那么执行收尾工作吧；

    //数据发送完毕，或者把需要发送的数据干掉，都说明发送缓冲区可能有地方了，让发送线程往下走判断能否发送新数据
    if(sem_post(&m_semEventSendQueue)==-1)       
        qc_log_stderr(0,"CSocket::qc_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");


    p_memory->FreeMemory(pConn->psendMemPointer);  //释放内存
    pConn->psendMemPointer = NULL;        
    --pConn->iThrowsendCount;  //建议放在最后执行
    return;
}

void CSocket::threadRecvProcFunc(char *pMsgBuf)
{   
    return;
}