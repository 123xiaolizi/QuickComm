#include "qc_conf.h"
#include "qc_public.h"
#include <iostream>


pid_t   qc_pid;                //当前进程的pid
pid_t   qc_parent;             //父进程的pid
int     qc_process;            //进程类型，比如master,worker进程等
int     g_stopEvent;            //标志程序退出,0不退出1，退出




int main()
{

    auto conf = CConfig::GetInstance();
    conf.Load("conf.conf");
    for (auto i = conf.m_ConfigItemList.begin(); i != conf.m_ConfigItemList.end(); ++i)
    {
        std::cout << (*i)->ItemName << ' ' << (*i)->ItemContent << std::endl;
    }

    qc_log_init();
    qc_log_error_core(QC_LOG_ALERT,errno,"qc_master_process_cycle()中sigprocmask()失败!");
    return 0;

}



