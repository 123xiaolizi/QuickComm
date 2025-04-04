#include "qc_conf.h"
#include "qc_public.h"
#include "qc_macro.h"
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

//本文件用的函数声明
static void freeresource();

pid_t   qc_pid;                //当前进程的pid
pid_t   qc_parent;             //父进程的pid
int     qc_process;            //进程类型，比如master,worker进程等
int     g_stopEvent;            //标志程序退出,0不退出1，退出

//和设置标题有关的全局量
size_t  g_argvneedmem=0;        //保存下这些argv参数所需要的内存大小
size_t  g_envneedmem=0;         //环境变量所占内存大小
int     g_os_argc;              //参数个数 
char    **g_os_argv;            //原始命令行参数数组,在main中会被赋值
char    *gp_envmem=NULL;        //指向自己分配的env环境变量的内存，在qc_init_setproctitle()函数中会被分配内存
int     g_daemonized=0;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了



int main(int argc, char *const *argv )
{

    //临时变量
    int i;
    int exitcode = 0;

    qc_pid = getpid();//取得进程pid
    qc_parent = getppid();//取得父进程的id 

    //统计argv所占的内存
    g_argvneedmem = 0;
    for(i = 0; i < argc; i++)
    {
        g_argvneedmem += strlen(argv[i]) + 1;
    }
    //统计环境变量所占的内存。注意判断方法是environ[i]是否为空作为环境变量结束标记
    for(i = 0; environ[i]; ++i)
    {
        g_envneedmem += strlen(environ[i]) + 1;
    }

    g_os_argc = argc;//保存参数个数
    g_os_argv = (char **) argv;//保存参数指针

    //配置文件读取
    CConfig* conf = CConfig::GetInstance();
    if(conf->Load("quickcomm.conf") == false)
    {
        qc_log_init();//初始化日志
        qc_log_stderr(0,"配置文件[%s]载入失败，退出!","quickcomm.conf");
    }


    qc_log_init();//初始化日志
    //创建守护进程
    if(conf->GetIntDefault("Daemon", 0) == 1)
    {
        //按守护进程的方式运行
        int cdaemonresult = qc_daemon();
        if (cdaemonresult == -1)
        {
            //失败了
        }
        if (cdaemonresult == 1)//父进程返回的1
        {
            //这是原始的父进程
            freeresource();
            return exitcode;
        }
    }

     // //正式开始工作流程
    qc_master_process_cycle();

    // // for(;;){

    // // }
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
