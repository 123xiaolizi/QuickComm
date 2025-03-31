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

    //下列这些信号在执行本函数期间不希望收到（保护不希望由信号中断的代码临界区）
    //建议fork()子进程时学习这种写法，防止信号的干扰；
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

    //sigprocmask 是一个系统调用，用于检查或修改调用进程的信号掩码，该掩码决定了哪些信号当前被阻塞而无法递送。
    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -1)
    {
        qc_log_error_core(QC_LOG_ALERT,errno,"qc_master_process_cycle()中sigprocmask()失败!");
    }

    //设置主进程标题
    size_t size;
    int i;
    size = sizeof(master_process);
    size += g_argvneedmem;
    if (size < 1000)
    {
        char title[1000] = {0};
        strcpy(title,(const char*)master_process);
        strcat(title, " ");
        for(i = 0; i < g_os_argc; ++i)
        {
            strcat(title, g_os_argv[i]);
        }
        qc_setproctitle(title);//设置标题
        qc_log_error_core(QC_LOG_NOTICE, 0, "%s %P [master进程] 启动并开始运行......!",title,qc_pid);
    }

    //从配置文件中读取要创建工作进程的数量
    CConfig *conf = CConfig::GetInstance();
    int workprocess = conf->GetIntDefault("WorkerProcesses",2);
    //创建worker子进程
    qc_start_worker_processes(workprocess);

    /*
    //sigsuspend是一个原子操作，包含4个步骤：
    //a)根据给定的参数设置新的mask 并 阻塞当前进程【因为是个空集，所以不阻塞任何信号】
    //b)此时，一旦收到信号，便恢复原先的信号屏蔽
    //c)调用该信号对应的信号处理函数
    //d)信号处理函数返回后，sigsuspend返回，使程序流程继续往下走
    */
    //创建子进程后，父进程的执行流程会返回到这里
    sigemptyset(&set);//信号屏蔽字为空，表示不屏蔽任何信号

    for(;;)
    {
        //阻塞在这里，等待一个信号，此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒（返回）
        sigsuspend(&set);

        sleep(1);
    }
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
    qc_log_error_core(QC_LOG_NOTICE, 0, "%S %P [worker进程]启动并开始运行。。。", pprocname, qc_pid);
    for(;;)
    {
        //处理网络事件和定时器事件
        //qc_process_events_and_timers();
    }

    return;
}

//子进程创建时调用该函数进行初始化工作
static void qc_worker_process_init(int inum)
{
    sigset_t set; //信号集
    
    sigemptyset(&set);
    if(sigprocmask(SIG_SETMASK, &set, nullptr) == -1)
    {
        qc_log_error_core(QC_LOG_ALERT, errno,"qc_worker_process_init()中sigprocmask(SIG_SETMASK) 失败!");
    }
    CConfig *conf = CConfig::GetInstance();
    int tmpthreadnums = conf->GetIntDefault("ProcMsgRecvWorkThreadCount",5);
    //启动线程池

    //启动网络服务。。。


    return;

}