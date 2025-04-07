#include "qc_conf.h"
#include "qc_public.h"
#include "qc_macro.h"
#include "qc_socket.h"
#include "qc_threadpool.h"
#include "qc_slogic.h"
#include "qc_memory.h"
#include "qc_crc32.h"
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <csignal>

//本文件用的函数声明
static void freeresource();

pid_t   qc_pid;                //当前进程的pid
pid_t   qc_parent;             //父进程的pid
int     qc_process;            //进程类型，比如master,worker进程等
int     g_stopEvent;            //标志程序退出,0不退出1，退出


//socket/线程池相关
//CSocket      g_socket;          //socket全局对象
CLogicSocket   g_socket;        //socket全局对象  
CThreadPool    g_threadpool;    //线程池全局对象

//和设置标题有关的全局量
size_t  g_argvneedmem=0;        //保存下这些argv参数所需要的内存大小
size_t  g_envneedmem=0;         //环境变量所占内存大小
int     g_os_argc;              //参数个数 
char    **g_os_argv;            //原始命令行参数数组,在main中会被赋值
char    *gp_envmem=NULL;        //指向自己分配的env环境变量的内存，在qc_init_setproctitle()函数中会被分配内存
int     g_daemonized=0;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了

sig_atomic_t  qc_reap;         //标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
                                   //一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】                                   


int main(int argc, char *const *argv )
{
    int exitcode = 0; //退出代码
    int i;

    //初始化变量
    g_stopEvent = 0;//标记是否退出程序，0不退出

    qc_pid = getpid();
    qc_parent = getppid();

    //统计argv所占用的内存
    g_argvneedmem = 0;
    for(i = 0; i < argc; i++)
    {
        g_argvneedmem += strlen(environ[i] + 1);
    }

    g_os_argc = argc;
    g_os_argv = (char **)argv;

    //全局变量初始化
    qc_log.fd = -1;
    qc_process = QC_PROCESS_MASTER;
    qc_reap = 0; //标记子进程没有发生变化

    //读配置初始化
    CConfig *p_config = CConfig::GetInstance();
    if(p_config->Load("quickcomm.conf") == false)
    {
        qc_log_init();
        qc_log_stderr(0,"配置文件[%s]读取失败，退出！", "quickcomm.conf");
        exitcode = 2;
        goto lblexit;
    }

    //内存单例类初始化
    CMemory::GetInstance();
    //CRC32单例类初始化
    CCRC32::GetInstance();

    //日志初始化
    qc_log_init();
       
    if(qc_init_signals() != 0) //信号初始化
    {
        exitcode = 1;
        goto lblexit;
    }        
    if(g_socket.Initialize() == false)//初始化socket
    {
        exitcode = 1;
        goto lblexit;
    }
    
    //创建守护进程
    if(p_config->GetIntDefault("Daemon",0) == 1) //读配置文件，拿到配置文件中是否按守护进程方式启动的选项
    {
        //1：按守护进程方式运行
        int cdaemonresult = qc_daemon();
        if(cdaemonresult == -1) //fork()失败
        {
            exitcode = 1;    //标记失败
            goto lblexit;
        }
        if(cdaemonresult == 1)
        {
            //这是原始的父进程
            freeresource();   //只有进程退出了才goto到 lblexit，用于提醒用户进程退出了
                              //而我现在这个情况属于正常fork()守护进程后的正常退出，不应该跑到lblexit()去执行，因为那里有一条打印语句标记整个进程的退出，这里不该限制该条打印语句；
            exitcode = 0;
            return exitcode;  //整个进程直接在这里退出
        }
        //走到这里，成功创建了守护进程并且这里已经是fork()出来的进程，现在这个进程做了master进程
        g_daemonized = 1;    //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了
    }

    //正式工作流程
    qc_master_process_cycle();

lblexit:
    //(5)该释放的资源要释放掉
    qc_log_stderr(0,"程序退出，再见了!");
    freeresource();  //一系列的main返回前的释放动作函数
    //printf("程序退出，再见!\n");    
    return exitcode;

}


void freeresource()
{
    //(1)对于因为设置可执行程序标题导致的环境变量分配的内存，我们应该释放
    if(gp_envmem)
    {
        delete []gp_envmem;
        gp_envmem = NULL;
    }
    //(2)关闭日志文件
    if(qc_log.fd != STDERR_FILENO && qc_log.fd != -1)  
    {        
        close(qc_log.fd); //不用判断结果了
        qc_log.fd = -1; //标记下，防止被再次close吧        
    }
}
