#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "qc_macro.h"
#include "qc_public.h"
#include "qc_conf.h"

//函数声明
static void qc_start_worker_processes(int threadnums);
static int qc_spawn_process(int threadnums,const char *pprocname);
static void qc_worker_process_cycle(int inum,const char *pprocname);
static void qc_worker_process_init(int inum);

//变量声明
static u_char  master_process[] = "master process";

void qc_master_process_cycle()
{
    sigset_t set;        //信号集

    sigemptyset(&set);   //清空信号集

    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符
    //.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......
    
    //设置，此时无法接受的信号；阻塞期间，你发过来的上述信号，多个会被合并为一个，暂存着，等你放开信号屏蔽后才能收到这些信号。。。
    //sigprocmask()在第三章第五节详细讲解过
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) //第一个参数用了SIG_BLOCK表明设置 进程 新的信号屏蔽字 为 “当前信号屏蔽字 和 第二个参数指向的信号集的并集
    {        
        qc_log_error_core(QC_LOG_ALERT,errno,"qc_master_process_cycle()中sigprocmask()失败!");
    }
    //即便sigprocmask失败，程序流程 也继续往下走

    //首先我设置主进程标题---------begin
    size_t size;
    int    i;
    size = sizeof(master_process);  //注意我这里用的是sizeof，所以字符串末尾的\0是被计算进来了的
    size += g_argvneedmem;          //argv参数长度加进来    
    if(size < 1000) //长度小于这个，我才设置标题
    {
        char title[1000] = {0};
        strcpy(title,(const char *)master_process); //"master process"
        strcat(title," ");  //跟一个空格分开一些，清晰    //"master process "
        for (i = 0; i < g_os_argc; i++)         //"master process ./nginx"
        {
            strcat(title,g_os_argv[i]);
        }//end for
        qc_setproctitle(title); //设置标题
        qc_log_error_core(QC_LOG_NOTICE,0,"%s %P 【master进程】启动并开始运行......!",title,qc_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    }    
    //首先我设置主进程标题---------end
        
    //从配置文件中读取要创建的worker进程数量
    CConfig *p_config = CConfig::GetInstance(); //单例类
    int workprocess = p_config->GetIntDefault("WorkerProcesses",1); //从配置文件中得到要创建的worker进程数量
    qc_start_worker_processes(workprocess);  //这里要创建worker子进程

    //创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来    
    sigemptyset(&set); //信号屏蔽字为空，表示不屏蔽任何信号
    
    for ( ;; ) 
    {

    
        sigsuspend(&set); //阻塞在这里，等待一个信号，此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒（返回）；

    }// end for(;;)
    return;
}

//根据给定的参数创建指定数量的子进程
static void qc_start_worker_processes(int threadnums)
{
    int i = 0;
    for (i = 0; i < threadnums; ++i)
    {
        qc_spawn_process(i, "worker process");
    }
    return;
}

//创建一个子进程
static int qc_spawn_process(int inum,const char *pprocname)
{
    pid_t pid;
    pid = fork();
    switch (pid)  //pid判断父子进程，分支处理
    {  
    case -1: //产生子进程失败
        qc_log_error_core(QC_LOG_ALERT,errno,"qc_spawn_process()fork()产生子进程num=%d,procname=\"%s\"失败!",inum,pprocname);
        return -1;

    case 0:  //子进程分支
        qc_parent = qc_pid;              //因为是子进程了，所有原来的pid变成了父pid
        qc_pid = getpid();                //重新获取pid,即本子进程的pid
        qc_worker_process_cycle(inum,pprocname);    //所有worker子进程在这个函数里不断循环着不出来，也就是说，子进程流程不往下边走;
        break;

    default: //这个应该是父进程分支，直接break;，流程往switch之后走            
        break;
    }

    return pid;
}

//worker子进程的功能函数,inum：进程编号 0号开始
static void qc_worker_process_cycle(int inum,const char *pprocname)
{
    qc_process = QC_PROCESS_WORKER; //设置进程类型

    //设置进程名
    qc_worker_process_init(inum);
    qc_setproctitle(pprocname);
    qc_log_error_core(QC_LOG_NOTICE, 0, "%s %d [worker进程]启动并开始运行。。。", pprocname, qc_pid);
    for(;;)
    {
        //处理网络事件和定时器事件
        qc_process_events_and_timers();
    }

    //如果从这个循环跳出来
    g_threadpool.StopAll();      //考虑在这里停止线程池；
    g_socket.Shutdown_subproc(); //socket需要释放的东西考虑释放；
    return;
}

//子进程创建时调用该函数进行初始化工作
static void qc_worker_process_init(int inum)
{
    sigset_t  set;      //信号集

    sigemptyset(&set);  //清空信号集
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)  //原来是屏蔽那10个信号【防止fork()期间收到信号导致混乱】，现在不再屏蔽任何信号【接收任何信号】
    {
        qc_log_error_core(QC_LOG_ALERT,errno,"qc_worker_process_init()中sigprocmask()失败!");
    }

    //线程池代码，率先创建，至少要比和socket相关的内容优先
    CConfig *p_config = CConfig::GetInstance();
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount",5); //处理接收到的消息的线程池中线程数量
    if(g_threadpool.Create(tmpthreadnums) == false)  //创建线程池中线程
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    sleep(1); //再休息1秒；

    if(g_socket.Initialize_subproc() == false) //初始化子进程需要具备的一些多线程能力相关的信息
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    
    //如下这些代码参照官方nginx里的qc_event_process_init()函数中的代码
    g_socket.qc_epoll_init();           //初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
    //g_socket.qc_epoll_listenportstart();//往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发qc_epoll_process_events()里边的epoll_wait()往下走】
    
    
    //....将来再扩充代码
    //....
    return;


}