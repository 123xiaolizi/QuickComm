#include "qc_public.h"
#include "qc_memory.h"
#include "qc_socket.h"
#include "qc_lockmutex.h"
#include "qc_conf.h"
#include "qc_macro.h"
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
#include <pthread.h>

CSocket::CSocket()
{
    // 配置相关
    m_worker_connections = 1;      // epoll连接最大项数
    m_ListenPortCount = 1;         // 监听一个端口
    m_RecyConnectionWaitTime = 60; // 等待这么些秒后才回收连接

    // epoll相关
    m_epollhandle = -1; // epoll返回的句柄
    

    // 一些和网络通讯有关的常用变量值，供后续频繁使用时提高效率
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);  // 包头的sizeof值【占用的字节数】
    m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER); // 消息头的sizeof值【占用的字节数】

    // 多线程相关
    // pthread_mutex_init(&m_recvMessageQueueMutex, NULL); //互斥量初始化

    // 各种队列相关
    m_iSendMsgQueueCount = 0;     // 发消息队列大小
    m_totol_recyconnection_n = 0; // 待释放连接队列大小
    m_cur_size_ = 0;              // 当前计时队列尺寸
    m_timer_value_ = 0;           // 当前计时队列头部的时间值
    m_iDiscardSendPkgCount = 0;   // 丢弃的发送数据包数量

    // 在线用户相关
    m_onlineUserCount = 0; // 在线用户数量统计，先给0
    m_lastprintTime = 0;   // 上次打印统计信息的时间，先给0
    return;
}
CSocket::~CSocket()
{
    //释放必须的内存
    //(1)监听端口相关内存的释放--------
    std::vector<lpqc_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos) //vector
	{		
		delete (*pos); //一定要把指针指向的内存干掉，不然内存泄漏
	}//end for
	m_ListenSocketList.clear();    
    return;
}
// 初始化函数[父进程中执行]
bool CSocket::Initialize()
{
    //读配置
    ReadConf();
    if(qc_open_listening_sockets() == false)  //打开监听端口    
        return false;  
    return true;

}
// 初始化函数[子进程中执行]
bool CSocket::Initialize_subproc()
{
    //发消息互斥量初始化
    if(pthread_mutex_init(&m_sendMessageQueueMutex, NULL)  != 0)
    {        
        qc_log_stderr(0,"CSocket::Initialize_subproc()中pthread_mutex_init(&m_sendMessageQueueMutex)失败.");
        return false;    
    }
    //连接相关互斥量初始化
    if(pthread_mutex_init(&m_connectionMutex, NULL)  != 0)
    {
        qc_log_stderr(0,"CSocket::Initialize_subproc()中pthread_mutex_init(&m_connectionMutex)失败.");
        return false;    
    }    
    //连接回收队列相关互斥量初始化
    if(pthread_mutex_init(&m_recyconnqueueMutex, NULL)  != 0)
    {
        qc_log_stderr(0,"CSocket::Initialize_subproc()中pthread_mutex_init(&m_recyconnqueueMutex)失败.");
        return false;    
    } 
    //和时间处理队列有关的互斥量初始化
    if(pthread_mutex_init(&m_timequeueMutex, NULL)  != 0)
    {
        qc_log_stderr(0,"CSocket::Initialize_subproc()中pthread_mutex_init(&m_timequeueMutex)失败.");
        return false;    
    }

    //初始化发消息相关信号量，信号量用于进程/线程 之间的同步，虽然 互斥量[pthread_mutex_lock]和 条件变量[pthread_cond_wait]都是线程之间的同步手段，但
    //这里用信号量实现 则 更容易理解，更容易简化问题，使用书写的代码短小且清晰；
    //第二个参数=0，表示信号量在线程之间共享，确实如此 ，如果非0，表示在进程之间共享
    //第三个参数=0，表示信号量的初始值，为0时，调用sem_wait()就会卡在那里卡着
    if (sem_init(&m_semEventSendQueue, 0, 0) == -1)
    {
        qc_log_stderr(0,"CSocket::Initialize_subproc()中sem_init(&m_semEventSendQueue,0,0)失败.");
        return false;
    }

    //创建线程
    int err;
    ThreadItem *pSendQueue;    //专门用来发送数据的线程
    m_threadVector.push_back(pSendQueue = new ThreadItem(this));                         //创建 一个新线程对象 并入到容器中 
    err = pthread_create(&pSendQueue->_Handle, NULL, ServerSendQueueThread,pSendQueue); //创建线程，错误不返回到errno，一般返回错误码
    if(err != 0)
    {
        qc_log_stderr(0,"CSocket::Initialize_subproc()中pthread_create(ServerSendQueueThread)失败.");
        return false;
    }

    //---
    ThreadItem *pRecyconn;    //专门用来回收连接的线程
    m_threadVector.push_back(pRecyconn = new ThreadItem(this)); 
    err = pthread_create(&pRecyconn->_Handle, NULL, ServerRecyConnectionThread,pRecyconn);
    if(err != 0)
    {
        qc_log_stderr(0,"CSocket::Initialize_subproc()中pthread_create(ServerRecyConnectionThread)失败.");
        return false;
    }

    if(m_ifkickTimeCount == 1)  //是否开启踢人时钟，1：开启   0：不开启
    {
        ThreadItem *pTimemonitor;    //专门用来处理到期不发心跳包的用户踢出的线程
        m_threadVector.push_back(pTimemonitor = new ThreadItem(this)); 
        err = pthread_create(&pTimemonitor->_Handle, NULL, ServerTimerQueueMonitorThread,pTimemonitor);
        if(err != 0)
        {
            qc_log_stderr(0,"CSocket::Initialize_subproc()中pthread_create(ServerTimerQueueMonitorThread)失败.");
            return false;
        }
    }

    return true;
}
// 关闭退出函数[子进程中执行]
void CSocket::Shutdown_subproc()
{
    if(sem_post(&m_semEventSendQueue) == -1)
    {
        qc_log_stderr(0,"CSocket::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");
    }

    std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); //等待一个线程终止
    }
    //(2)释放一下new出来的ThreadItem【线程池中的线程】    
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if(*iter)
			delete *iter;
	}
	m_threadVector.clear();

    //(3)队列相关
    clearMsgSendQueue();
    clearconnection();
    clearAllFromTimerQueue();
    
    //(4)多线程相关    
    pthread_mutex_destroy(&m_connectionMutex);          //连接相关互斥量释放
    pthread_mutex_destroy(&m_sendMessageQueueMutex);    //发消息互斥量释放    
    pthread_mutex_destroy(&m_recyconnqueueMutex);       //连接回收队列相关的互斥量释放
    pthread_mutex_destroy(&m_timequeueMutex);           //时间处理队列相关的互斥量释放
    sem_destroy(&m_semEventSendQueue);                  //发消息相关线程信号量释放
}


//专门用于读各种配置项
void CSocket::ReadConf()
{
    CConfig *p_config = CConfig::GetInstance();
    m_worker_connections      = p_config->GetIntDefault("worker_connections",m_worker_connections);              //epoll连接的最大项数
    m_ListenPortCount         = p_config->GetIntDefault("ListenPortCount",m_ListenPortCount);                    //取得要监听的端口数量
    m_RecyConnectionWaitTime  = p_config->GetIntDefault("Sock_RecyConnectionWaitTime",m_RecyConnectionWaitTime); //等待这么些秒后才回收连接

    m_ifkickTimeCount         = p_config->GetIntDefault("Sock_WaitTimeEnable",0);                                //是否开启踢人时钟，1：开启   0：不开启
	m_iWaitTime               = p_config->GetIntDefault("Sock_MaxWaitTime",m_iWaitTime);                         //多少秒检测一次是否 心跳超时，只有当Sock_WaitTimeEnable = 1时，本项才有用	
	m_iWaitTime               = (m_iWaitTime > 5)?m_iWaitTime:5;                                                 //不建议低于5秒钟，因为无需太频繁
    m_ifTimeOutKick           = p_config->GetIntDefault("Sock_TimeOutKick",0);                                   //当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当Sock_WaitTimeEnable = 1时，本项才有用 

    m_floodAkEnable          = p_config->GetIntDefault("Sock_FloodAttackKickEnable",0);                          //Flood攻击检测是否开启,1：开启   0：不开启
	m_floodTimeInterval      = p_config->GetIntDefault("Sock_FloodTimeInterval",100);                            //表示每次收到数据包的时间间隔是100(毫秒)
	m_floodKickCount         = p_config->GetIntDefault("Sock_FloodKickCounter",10);                              //累积多少次踢出此人

    return;
}
//监听必须的端口【支持多个端口】
bool CSocket::qc_open_listening_sockets()
{
    int isock;
    int iport;
    struct sockaddr_in serv_addr;
    char strinfo[100];//临时字符串

    //初始化相关
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    //监听本地所有的IP地址；INADDR_ANY表示的是一个服务器上所有的网卡（服务器可能不止一个网卡）多个本地ip地址都进行绑定端口号，进行侦听。
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    //中途用到一些配置信息
    CConfig *p_config = CConfig::GetInstance();
    for (int i = 0; i < m_ListenPortCount; i++)
    {
        isock = socket(AF_INET, SOCK_STREAM, 0);
        if (isock == -1)
        {
            qc_log_stderr(errno,"CSocket::Initialize()中socket()失败,i=%d.",i);
            //其实这里直接退出，那如果以往有成功创建的socket呢？就没得到释放吧，当然走到这里表示程序不正常，应该整个退出，也没必要释放了 
            return false;
        }

        //setsockopt（）:设置一些套接字参数选项；
        //参数2：是表示级别，和参数3配套使用，也就是说，参数3如果确定了，参数2就确定了;
        //参数3：允许重用本地地址
        //设置 SO_REUSEADDR，目的：主要是解决TIME_WAIT这个状态导致bind()失败的问题
        int reuseaddr = 1;
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void*) &reuseaddr, sizeof(reuseaddr)) == -1)
        {
            qc_log_stderr(errno, "CSocket::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.",i);
            close(isock);
            return false;
        }

        //处理惊群问题
        int reuseport = 1;
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEPORT,(const void *) &reuseport, sizeof(int))== -1) //端口复用需要内核支持
        {
            //失败就失败吧，失败顶多是惊群，但程序依旧可以正常运行，所以仅仅提示一下即可
            qc_log_stderr(errno,"CSocket::Initialize()中setsockopt(SO_REUSEPORT)失败",i);
        }

        //设置该socket为非阻塞
        if (setnonblocking(isock) == false)
        {
            qc_log_stderr(errno,"CSocket::Initialize()中setnonblocking()失败,i=%d.",i);
            close(isock);
            return false;
        }

        //设置本服务器要监听的地址和端口，这样客户端才能连接到该地址和端口并发送数据        
        strinfo[0] = 0;
        sprintf(strinfo,"ListenPort%d",i);
        iport = p_config->GetIntDefault(strinfo,10000);
        serv_addr.sin_port = htons((in_port_t)iport);   //in_port_t其实就是uint16_t

        //绑定服务器地址结构体
        if(bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        {
            qc_log_stderr(errno,"CSocket::Initialize()中bind()失败,i=%d.",i);
            close(isock);
            return false;
        }
        
        //开始监听
        if(listen(isock,QC_LISTEN_BACKLOG) == -1)
        {
            qc_log_stderr(errno,"CSocket::Initialize()中listen()失败,i=%d.",i);
            close(isock);
            return false;
        }

        //可以，放到列表里来
        lpqc_listening_t p_listensocketitem = new qc_listening_t; //千万不要写错，注意前边类型是指针，后边类型是一个结构体
        memset(p_listensocketitem,0,sizeof(qc_listening_t));      //注意后边用的是 qc_listening_t而不是lpqc_listening_t
        p_listensocketitem->port = iport;                          //记录下所监听的端口号
        p_listensocketitem->fd   = isock;                          //套接字木柄保存下来   
        qc_log_error_core(QC_LOG_INFO,0,"监听%d端口成功!",iport); //显示一些信息到日志中
        m_ListenSocketList.push_back(p_listensocketitem);          //加入到队列中
    }//end for

    if(m_ListenSocketList.size() <= 0)  //不可能一个端口都不监听吧
        return false;
    return true;
    
}
//关闭监听套接字
void CSocket::qc_close_listening_sockets()
{
    for(int i = 0; i < m_ListenPortCount; i++) //要关闭这么多个监听端口
    {  
        //qc_log_stderr(0,"端口是%d,socketid是%d.",m_ListenSocketList[i]->port,m_ListenSocketList[i]->fd);
        close(m_ListenSocketList[i]->fd);
        qc_log_error_core(QC_LOG_INFO,0,"关闭监听端口%d!",m_ListenSocketList[i]->port); //显示一些信息到日志中
    }//end for(int i = 0; i < m_ListenPortCount; i++)
    return;
}
//设置非阻塞套接字	
bool CSocket::setnonblocking(int sockfd)
{
    int nb = 1;
    if(ioctl(sockfd, FIONBIO, &nb) == -1)
    {
        return false;
    }
    return true;
}

//数据发送相关

//把数据扔到待发送对列中 
void CSocket::msgSend(char *psendbuf)
{
    
   CMemory *p_memory = CMemory::GetInstance();

   CLock lock(&m_sendMessageQueueMutex);  //互斥量

   //发送消息队列过大也可能给服务器带来风险
   if(m_iSendMsgQueueCount > 50000)
   {
       //发送队列过大，比如客户端恶意不接受数据，就会导致这个队列越来越大
       //那么可以考虑为了服务器安全，干掉一些数据的发送，虽然有可能导致客户端出现问题，但总比服务器不稳定要好很多
       m_iDiscardSendPkgCount++;
       p_memory->FreeMemory(psendbuf);
       return;
   }
   
   //总体数据并无风险，不会导致服务器崩溃，要看看个体数据，找一下恶意者了    
   LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)psendbuf;
   lpqc_connection_t p_Conn = pMsgHeader->pConn;
   if(p_Conn->iSendCount > 400)
   {
       //该用户收消息太慢【或者干脆不收消息】，累积的该用户的发送队列中有的数据条目数过大，认为是恶意用户，直接切断
       qc_log_stderr(0,"CSocket::msgSend()中发现某用户%d积压了大量待发送数据包，切断与他的连接！",p_Conn->fd);      
       m_iDiscardSendPkgCount++;
       p_memory->FreeMemory(psendbuf);
       zdClosesocketProc(p_Conn); //直接关闭
       return;
   }

   ++p_Conn->iSendCount; //发送队列中有的数据条目数+1；
   m_MsgSendQueue.push_back(psendbuf);     
   ++m_iSendMsgQueueCount;   //原子操作

   //将信号量的值+1,这样其他卡在sem_wait的就可以走下去
   if(sem_post(&m_semEventSendQueue)==-1)  //让ServerSendQueueThread()流程走下来干活
   {
       qc_log_stderr(0,"CSocket::msgSend()中sem_post(&m_semEventSendQueue)失败.");      
   }
   return;
    

}
//主动关闭一个连接时的要做些善后的处理函数
void CSocket::zdClosesocketProc(lpqc_connection_t p_Conn)
{
    if(m_ifkickTimeCount == 1)
    {
        DeleteFromTimerQueue(p_Conn);
    }
    if(p_Conn->fd != -1)
    {
        //关闭后epoll就会被从红黑树中删除，所以这之后无法收到任何epoll事件
        close(p_Conn->fd);
        p_Conn->fd = -1;
    }

    if(p_Conn->iThrowsendCount > 0)
    {
        --p_Conn->iThrowsendCount;
    }

    inRecyConnectQueue(p_Conn);
    return;
}

//打印统计信息
void CSocket::printTDInfo()
{
    time_t currtime = time(nullptr);
    if( (currtime - m_lastprintTime) > 10)
    {
        int tmprmqc = g_threadpool.getRecvMsgQueueCount();//收消息队列

        m_lastprintTime = currtime;
        int tmpoLUC = m_onlineUserCount;//atomic做个中转，直接打印atomic类型报错
        int tmpsmqc = m_iSendMsgQueueCount;//同理
        qc_log_stderr(0,"------------------------------------begin--------------------------------------");
        qc_log_stderr(0,"当前在线人数/总人数(%d/%d)。",tmpoLUC,m_worker_connections);        
        qc_log_stderr(0,"连接池中空闲连接/总连接/要释放的连接(%d/%d/%d)。",m_freeconnectionList.size(),m_connectionList.size(),m_recyconnectionList.size());
        qc_log_stderr(0,"当前时间队列大小(%d)。",m_timerQueuemap.size());        
        qc_log_stderr(0,"当前收消息队列/发消息队列大小分别为(%d/%d)，丢弃的待发送数据包数量为%d。",tmprmqc,tmpsmqc,m_iDiscardSendPkgCount);        
        if( tmprmqc > 100000)
        {
            //接收队列过大，报一下，这个属于应该 引起警觉的，考虑限速等等手段
            qc_log_stderr(0,"接收队列条目数量过大(%d)，要考虑限速或者增加处理线程数量了！！！！！！",tmprmqc);
        }
        qc_log_stderr(0,"-------------------------------------end---------------------------------------");
    }
}

//测试是否flood攻击成立，成立则返回true，否则返回false
bool CSocket::TestFlood(lpqc_connection_t pConn)
{
    struct timeval sCurrTime;//当前时间结构
    uint64_t iCurrTime;//当前时间-毫秒
    bool reco = false;

    gettimeofday(&sCurrTime, nullptr);
    iCurrTime = (sCurrTime.tv_sec * 1000 + sCurrTime.tv_usec / 1000);

    //两次收到包的时间 < 100毫秒
    if((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval)
    {
        pConn->FloodAttackCount++;
        pConn->FloodkickLastTime = iCurrTime;
    }
    else
    {
        //不频繁，恢复计数
        pConn->FloodAttackCount = 0;
        pConn->FloodkickLastTime = iCurrTime;
    }

    if(pConn->FloodAttackCount >= m_floodKickCount)
    {
        reco = true;
    }
    return reco;
}

///////////////////////epoll操作

//epoll功能初始化	
int  CSocket::qc_epoll_init()
{
    m_epollhandle = epoll_create(m_worker_connections);
    if(m_epollhandle == -1)
    {
        qc_log_stderr(errno,"CSocket::qc_epoll_init()中epoll_create()失败.");
        exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
    }

    //(2)创建连接池【数组】、创建出来，这个东西后续用于处理所有客户端的连接
    initconnection();

    //(3)遍历所有监听socket【监听端口】，我们为每个监听socket增加一个 连接池中的连接【说白了就是让一个socket和一个内存绑定，以方便记录该sokcet相关的数据、状态等等】
    std::vector<lpqc_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos)
    {
        lpqc_connection_t p_Conn = qc_get_connection((*pos)->fd); //从连接池中获取一个空闲连接对象
        if (p_Conn == NULL)
        {
            //这是致命问题，刚开始怎么可能连接池就为空呢？
            qc_log_stderr(errno,"CSocket::qc_epoll_init()中qc_get_connection()失败.");
            exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
        }
        p_Conn->listening = (*pos);   //连接对象 和监听对象关联，方便通过连接对象找监听对象
        (*pos)->connection = p_Conn;  //监听对象 和连接对象关联，方便通过监听对象找连接对象

        //rev->accept = 1; //监听端口必须设置accept标志为1  ，这个是否有必要，再研究

        //对监听端口的读事件设置处理方法，因为监听端口是用来等对方连接的发送三路握手的，所以监听端口关心的就是读事件
        p_Conn->rhandler = &CSocket::qc_event_accept;

        //往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发qc_epoll_process_events()里边的epoll_wait()往下走】
        if(qc_epoll_oper_event(
                                (*pos)->fd,         //Socket句柄
                                EPOLL_CTL_ADD,      //事件类型，这里是增加
                                EPOLLIN|EPOLLRDHUP, //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭
                                0,                  //对于事件类型为增加的，不需要这个参数
                                p_Conn              //连接池中的连接 
                                ) == -1) 
        {
            exit(2); //有问题，直接退出，日志 已经写过了
        }
    } //end for 
    return 1;
}



//epoll等待接收和处理事件
int  CSocket::qc_epoll_process_events(int timer)
{
    //等待事件，事件会返回到m_events里，最多返回qc_MAX_EVENTS个事件【因为我只提供了这些内存】；
    //如果两次调用epoll_wait()的事件间隔比较长，则可能在epoll的双向链表中，积累了多个事件，所以调用epoll_wait，可能取到多个事件
    //阻塞timer这么长时间除非：a)阻塞时间到达 b)阻塞期间收到事件【比如新用户连入】会立刻返回c)调用时有事件也会立刻返回d)如果来个信号，比如你用kill -1 pid测试
    //如果timer为-1则一直阻塞，如果timer为0则立即返回，即便没有任何事件
    //返回值：有错误发生返回-1，错误在errno中，比如你发个信号过来，就返回-1，错误信息是(4: Interrupted system call)
    //       如果你等待的是一段时间，并且超时了，则返回0；
    //       如果返回>0则表示成功捕获到这么多个事件【返回值里】
    int events = epoll_wait(m_epollhandle,m_events,QC_MAX_EVENTS,timer);    
    
    if(events == -1)
    {
        //有错误发生，发送某个信号给本进程就可以导致这个条件成立，而且错误码根据观察是4；
        //#define EINTR  4，EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
               //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。
        if(errno == EINTR) 
        {
            //信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下，因为一般也不会人为给worker进程发送消息
            qc_log_error_core(QC_LOG_INFO,errno,"CSocket::qc_epoll_process_events()中epoll_wait()失败!"); 
            return 1;  //正常返回
        }
        else
        {
            //这被认为应该是有问题，记录日志
            qc_log_error_core(QC_LOG_ALERT,errno,"CSocket::qc_epoll_process_events()中epoll_wait()失败!"); 
            return 0;  //非正常返回 
        }
    }

    if(events == 0) //超时，但没事件来
    {
        if(timer != -1)
        {
            //要求epoll_wait阻塞一定的时间而不是一直阻塞，这属于阻塞到时间了，则正常返回
            return 1;
        }
        //无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题        
        qc_log_error_core(QC_LOG_ALERT,0,"CSocket::qc_epoll_process_events()中epoll_wait()没超时却没返回任何事件!"); 
        return 0; //非正常返回 
    }

    //会惊群，一个telnet上来，4个worker进程都会被惊动，都执行下边这个
    //qc_log_stderr(0,"惊群测试:events=%d,进程id=%d",events,qc_pid); 
    //qc_log_stderr(0,"----------------------------------------"); 

    //走到这里，就是属于有事件收到了
    lpqc_connection_t p_Conn;
    //uintptr_t          instance;
    uint32_t           revents;
    for(int i = 0; i < events; ++i)    //遍历本次epoll_wait返回的所有事件，注意events才是返回的实际事件数量
    {
        p_Conn = (lpqc_connection_t)(m_events[i].data.ptr);           //qc_epoll_add_event()给进去的，这里能取出来


        //能走到这里，我们认为这些事件都没过期，就正常开始处理
        revents = m_events[i].events;//取出事件类型
        
      

        if(revents & EPOLLIN)  //如果是读事件
        {
           
            (this->* (p_Conn->rhandler) )(p_Conn);    //注意括号的运用来正确设置优先级，防止编译出错；【如果是个新客户连入
        }
        
        if(revents & EPOLLOUT) //如果是写事件【对方关闭连接也触发这个，再研究。。。。。。】，注意上边的 if(revents & (EPOLLERR|EPOLLHUP))  revents |= EPOLLIN|EPOLLOUT; 读写标记都给加上了
        {
            //qc_log_stderr(errno,"22222222222222222222.");
            if(revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) //客户端关闭，如果服务器端挂着一个写通知事件，则这里个条件是可能成立的
            {
                --p_Conn->iThrowsendCount;                 
            }
            else
            {
                (this->* (p_Conn->whandler) )(p_Conn);   //如果有数据没有发送完毕，由系统驱动来发送，则这里执行的应该是 CSocket::qc_write_request_handler()
            }            
        }
    } //end for(int i = 0; i < events; ++i)     
    return 1;
}



//对epoll事件的具体操作
int CSocket::qc_epoll_oper_event(
            int fd,//句柄，一个socket
            uint32_t eventtype,//事件类型，一般是EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL
            uint32_t flag,//标志，具体含义取决于eventtype
            int bcaction,//补充动作，用于补充flag标记的不足  :  0：增加   1：去掉 2：完全覆盖 ,eventtype是EPOLL_CTL_MOD时这个参数就有用
            lpqc_connection_t pConn)//pConn：一个指针【其实是一个连接】，EPOLL_CTL_ADD时增加到红黑树中去，将来epoll_wait时能取出来用
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    if(eventtype == EPOLL_CTL_ADD)
    {
        ev.events = flag;
        pConn->events = flag;
    }
    else if(eventtype == EPOLL_CTL_MOD)
    {
        ev.events = pConn->events;
        if(bcaction == 0)
        {
            ev.events |= flag;
        }
        else if (bcaction == 1)
        {
            ev.events  &= ~flag;
        }
        else
        {
            ev.events = flag;
        }
        pConn->events = ev.events;
    }
    else
    {
        return 1;
    }

    ev.data.ptr = (void*)pConn;
    if(epoll_ctl(m_epollhandle, eventtype, fd, &ev) == -1)
    {
        qc_log_stderr(errno,"CSocket::qc_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.",fd,eventtype,flag,bcaction);    
        return -1;
    }
    return 1;
}

//专门用来发送数据的线程
void* CSocket::ServerSendQueueThread(void *threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocket *pSocketObj = pThread->_pThis;
    int err;
    std::list <char*>::iterator pos,pos2,posend;
    
    char *pMsgBuf;
    //消息头
    LPSTRUC_MSG_HEADER pMsgHeader;
    //包头
    LPCOMM_PKG_HEADER  pPkgHeader;
    lpqc_connection_t p_Conn;
    unsigned short itmp;
    ssize_t sendsize;

    //内存管理对象
    CMemory *p_memory = CMemory::GetInstance();

    while (g_stopEvent == 0)
    {
        if (sem_wait(&pSocketObj->m_semEventSendQueue) == -1)
        {
            //失败？及时报告，其他的也不好干啥
            if (errno != EINTR)
            {
                qc_log_stderr(errno,"CSocket::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败.");
            }
        }
        //一般走到这里都表示需要处理数据收发了
        if (g_stopEvent != 0)//程序退出标记
        {
            break;
        }

        if (pSocketObj->m_iSendMsgQueueCount > 0)
        {
            //因为我们要操作发送消息对列m_MsgSendQueue，所以这里要临界
            err = pthread_mutex_lock(&pSocketObj->m_sendMessageQueueMutex);
            if ( err != 0) qc_log_stderr(err,"CSocket::ServerSendQueueThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);

            pos = pSocketObj->m_MsgSendQueue.begin();
            posend = pSocketObj->m_MsgSendQueue.end();

            while (pos != posend)
            {
                pMsgBuf = (*pos);
                pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;//指向消息头
                pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + pSocketObj->m_iLenMsgHeader);//指向包头
                p_Conn = pMsgHeader->pConn;
                //丢弃过期包
                if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
                {
                    pos2 = pos;
                    pos++;
                    pSocketObj->m_MsgSendQueue.erase(pos2);
                    --pSocketObj->m_iSendMsgQueueCount;
                    p_memory->FreeMemory(pMsgBuf);
                    continue;
                }

                if(p_Conn->iThrowsendCount > 0)
                {
                    //对方的接收缓冲区已经满了，等系统驱动来发送消息，这里不能发了，也发不出去
                    pos++;
                    continue;
                }

                --p_Conn->iSendCount;

                //记录一下，后面要释放内存
                p_Conn->psendMemPointer = pMsgBuf;
                pos2 = pos;
                pos++;
                pSocketObj->m_MsgSendQueue.erase(pos2);
                //发送消息队列容量少1
                --pSocketObj->m_iSendMsgQueueCount;
                //要发送的数据的缓冲区指针，因为发送数据不一定全部都能发送出去，我们要记录数据发送到了哪里，需要知道下次数据从哪里开始发送
                p_Conn->psendbuf = (char*)pPkgHeader;
                //包头+包体 长度 ，打包时用了htons【本机序转网络序】，所以这里为了得到该数值，用了个ntohs【网络序转本机序】
                itmp = ntohs(pPkgHeader->pkgLen);
                //要发送多少数据，因为发送数据不一定全部都能发送出去，我们需要知道剩余有多少数据还没发送
                p_Conn->isendlen = itmp;

                //epoll采用水平触发模式，到这里应该是还没有添加写事件到epoll中
                //先直接写，如果返回EAGIN（缓冲区满了)那么再把写事件添加到epoll中
                //写完了再中epoll中移除写事件
                //数据不多的时候可以避免频繁对epoll进程操作（写事件的增加/删除)

                sendsize = pSocketObj->sendproc(p_Conn, p_Conn->psendbuf, p_Conn->isendlen);
                if(sendsize > 0)
                {
                    if(sendsize == p_Conn->isendlen)
                    {
                        //发送成功 释放内存
                        p_memory->FreeMemory(p_Conn->psendMemPointer);
                        p_Conn->psendMemPointer = nullptr;
                        p_Conn->iThrowsendCount = 0;
                    }
                    else
                    {
                        //只发送一部分
                        //记录剩多少没发送，下次sendproc()调用进行发送
                        p_Conn->psendbuf = p_Conn->psendbuf + sendsize;
                        p_Conn->isendlen = p_Conn->isendlen - sendsize;
                        //标记写缓冲区满了
                        ++p_Conn->iThrowsendCount;

                        if(pSocketObj->qc_epoll_oper_event(
                                p_Conn->fd,
                                EPOLL_CTL_MOD,
                                EPOLLOUT,
                                0,//对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                p_Conn) == -1)
                        {
                            qc_log_stderr(errno,"CSocket::ServerSendQueueThread()qc_epoll_oper_event()失败.");
                        }

                    }
                    continue;
                }

                //能走到这里，应该是有点问题的
                else if (sendsize == 0)
                {
                    //然后如果发送 缓冲区满则返回的应该是-1，而错误码应该是EAGAIN，所以我综合认为，这种情况我就把这个发送的包丢弃了【按对端关闭了socket处理】
                    p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的    
                    continue;
                }

                else if (sendsize == -1)
                {
                    ++p_Conn->iThrowsendCount;
                    if(pSocketObj->qc_epoll_oper_event(
                            p_Conn->fd,
                            EPOLL_CTL_MOD,
                            EPOLLOUT,
                            0,
                            p_Conn) == -1)
                    {
                        qc_log_stderr(errno,"CSocket::ServerSendQueueThread()qc_epoll_oper_event()-2失败.");
                    }
                    continue;
                }

                else
                {
                    //能走到这里的，应该就是返回值-2了，一般就认为对端断开了，等待recv()来做断开socket以及回收资源
                    p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的  
                    continue;
                }

            }

            err = pthread_mutex_unlock(&pSocketObj->m_sendMessageQueueMutex); 
            if(err != 0)  qc_log_stderr(err,"CSocket::ServerSendQueueThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
            
        }

    }

    return (void*)0;
    
}

// 处理发送消息队列
void CSocket::clearMsgSendQueue()
{
    char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();
	
	while(!m_MsgSendQueue.empty())
	{
		sTmpMempoint = m_MsgSendQueue.front();
		m_MsgSendQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}
}

