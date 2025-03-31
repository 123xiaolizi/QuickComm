#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>     //errno
#include <sys/stat.h>
#include <fcntl.h>
#include "qc_public.h"
#include "qc_macro.h"

//守护进程相关


int qc_daemon()
{
    //1.fork一个子进程
    switch(fork())
    {
        case -1:
            qc_log_error_core(QC_LOG_EMERG, errno, "qc_daemon()中fork()失败!");
            return -1;
        case 0:
            break;//子进程就往下执行
        default:
            return 1;//父进程直接返回，进去别的处理
    }
    //2.脱离终端控制
    if (setsid() == -1)
    {
        qc_log_error_core(QC_LOG_EMERG,errno,"qc_daemon()中setsid()失败!");
        return -1;
    }
    //3.不要让它来限制文件权限，以免引起混乱
    umask(0);

    //4.打开黑洞设备，将标准输入输出指向它
    int fd = open("/dev/null", O_RDWR);
    if (fd == -1)
    {
        qc_log_error_core(QC_LOG_EMERG,errno,"qc_daemon()中open(\"/dev/null\")失败!");
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) == -1)
    {
        qc_log_error_core(QC_LOG_EMERG,errno,"qc_daemon()中dup2(STDIN)失败!");      
        return -1;
    }

    if (dup2(fd, STDOUT_FILENO) == -1)
    {
        qc_log_error_core(QC_LOG_EMERG,errno,"qc_daemon()中dup2(STDOUT)失败!");      
        return -1;
    }
    if (fd > STDERR_FILENO)
    {
        if (dup2(fd, STDERR_FILENO) == -1)
        {
            qc_log_error_core(QC_LOG_EMERG, errno,"qc_daemon()中dup2(STDERR)失败!" );
            return -1;
        }
    }
    return 0;
}