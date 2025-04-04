#include "qc_comm.h"
#include "qc_public.h"
#include "qc_macro.h"
#include "qc_socket.h"
#include "qc_conf.h"
#include "qc_lockmutex.h"
#include "qc_memory.h"
#include <unistd.h>
//---------------------------------------------------------------
//连接池成员函数
qc_connection_s::qc_connection_s()
{
    iCurrsequence = 0;
    pthread_mutex_init(&logicPorcMutex, nullptr);
}
qc_connection_s::~qc_connection_s()
{
    pthread_mutex_destroy(&logicPorcMutex);
}
// 分配出去的时候初始化一些内容
void qc_connection_s::GetOneToUse()
{
    ++iCurrsequence;
    fd = -1;
    curStat = _PKG_HD_INIT;
    precvbuf = dataHeadInfo;
    irecvlen = sizeof(COMM_PKG_HEADER);

    precvMemPointer = nullptr;
    iThrowsendCount = 0;
    psendMemPointer = nullptr;
    events = 0;
    lastPingTime = time(nullptr);

    FloodkickLastTime = 0;
    FloodAttackCount = 0;
    iSendCount = 0;
}
// 回收回来的时候做一些事情
void qc_connection_s::PutOneToFree()
{
    ++iCurrsequence;
    if(precvMemPointer != NULL)//我们曾经给这个连接分配过接收数据的内存，则要释放内存
    {        
        CMemory::GetInstance()->FreeMemory(precvMemPointer);
        precvMemPointer = NULL;        
    }
    if(psendMemPointer != NULL) //如果发送数据的缓冲区里有内容，则要释放内存
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = NULL;
    }

    iThrowsendCount = 0;
}


//---------------------------------------------------------------
//初始化连接池
void CSocket::initconnection()
{
    lpqc_connection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();

    int ilenconnpool = sizeof(qc_connection_t);
    for (int i = 0; i < m_worker_connections; ++i)
    {
        p_Conn = (lpqc_connection_t)p_memory->AllocMemory(ilenconnpool, true);
        //定位new，调用构造函数
        p_Conn = new(p_Conn) qc_connection_t();
        p_Conn->GetOneToUse();
        m_connectionList.push_back(p_Conn);//所有连接都放在这个list
        m_freeconnectionList.push_back(p_Conn);//空闲的连接放在这里
    }
    m_free_connection_n = m_total_connection_n = m_connectionList.size();
    return;
}
//回收连接池
void CSocket::clearconnection()
{
    lpqc_connection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();

    while (!m_connectionList.empty())
    {
        p_Conn = m_connectionList.front();
        m_connectionList.pop_front();
        p_Conn->~qc_connection_s();
        p_memory->FreeMemory(p_Conn);
    }
    
}
//从连接池中获取一个空闲连接                                          
lpqc_connection_t CSocket::qc_get_connection(int isock)
{
    CLock lock(&m_connectionMutex);
    if(!m_freeconnectionList.empty())
    {
        //有空闲的，中里面获取一个
        lpqc_connection_t p_Conn = m_freeconnectionList.front();
        m_freeconnectionList.pop_front();
        p_Conn->GetOneToUse();
        --m_free_connection_n;
        p_Conn->fd = isock;
        return p_Conn;
    }
    //到这里说明没有空闲连接了，创建一个
    CMemory *p_memory = CMemory::GetInstance();
    lpqc_connection_t p_Conn = (lpqc_connection_t)p_memory->AllocMemory(sizeof(qc_connection_t), true);
    p_Conn = new(p_Conn) qc_connection_t();
    p_Conn->GetOneToUse();
    m_connectionList.push_back(p_Conn);
    ++m_total_connection_n;
    p_Conn->fd = isock;
    return p_Conn;
} 
//归还参数pConn所代表的连接到到连接池中                  
void CSocket::qc_free_connection(lpqc_connection_t pConn)
{
        //因为有线程可能要动连接池中连接，所以在合理互斥也是必要的
        CLock lock(&m_connectionMutex);  

        //首先明确一点，连接，所有连接全部都在m_connectionList里；
        pConn->PutOneToFree();
    
        //扔到空闲连接列表里
        m_freeconnectionList.push_back(pConn);
    
        //空闲连接数+1
        ++m_free_connection_n;

        return;
} 
//将要回收的连接放到一个队列中来，后续有专门的线程会处理这个队列中的连接的回收
//有些连接，我们不希望马上释放，要隔一段时间后再释放以确保服务器的稳定，所以，我们把这种隔一段时间才释放的连接先放到一个队列中来
void CSocket::inRecyConnectQueue(lpqc_connection_t pConn)
{
    std::list<lpqc_connection_t>::iterator pos;
    bool iffind = false;
    //针对连接回收列表的互斥量，因为线程ServerRecyConnectionThread()也有要用到这个回收列表
    CLock lock(&m_recyconnqueueMutex);
    //如下判断防止连接被多次扔到回收站中来

    for(pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.end(); ++pos)
    {
        if((*pos) == pConn)
        {
            iffind = true;
            break;
        }
    }
    if(iffind == true)
    {
        return;
    }
    pConn->inRecyTime = time(nullptr);
    ++(pConn->iCurrsequence);
    m_recyconnectionList.push_back(pConn);
    ++m_totol_recyconnection_n;
    --m_onlineUserCount;
    return;
}

//通用连接关闭函数
void CSocket::qc_close_connection(lpqc_connection_t pConn)
{
    qc_free_connection(pConn);
    if(pConn->fd != -1)
    {
        close(pConn->fd);
        pConn->fd = -1;
    }
    return;
}