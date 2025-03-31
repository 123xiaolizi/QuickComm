#include "qc_public.h"
#include "qc_conf.h"
#include <qc_macro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   //uintptr_t
#include <stdarg.h>   //va_start....
#include <unistd.h>   //STDERR_FILENO等
#include <sys/time.h> //gettimeofday
#include <time.h>     //localtime_r
#include <fcntl.h>    //open
#include <errno.h>    //errno

static u_char err_levels[][20] =
    {
        {"stderr"}, // 0：控制台错误
        {"emerg"},  // 1：紧急
        {"alert"},  // 2：警戒
        {"crit"},   // 3：严重
        {"error"},  // 4：错误
        {"warn"},   // 5：警告
        {"notice"}, // 6：注意
        {"info"},   // 7：信息
        {"debug"}   // 8：调试
};

qc_log_t qc_log;

// 描述：日志初始化，就是把日志文件打开 ，这里边涉及到释放的问题
void qc_log_init()
{
    u_char *plogname = nullptr;
    size_t nlen;

    // 从配置文件中读取日志相关配置
    CConfig* conf = CConfig::GetInstance();
    plogname = (u_char *)conf->GetString("Log");

    if (plogname == nullptr)
    {
        // 没读取到，给缺省路径文件名
        plogname = (u_char *)QC_ERROR_LOG_PATH;
    }
    qc_log.log_level = conf->GetIntDefault("LogLevel", QC_LOG_NOTICE); // 缺省等级
    // 绕过内核缓冲区，write()成功则写磁盘必然成功，但效率可能会比较低
    qc_log.fd = open((const char *)plogname, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (qc_log.fd == -1)
    {
        qc_log_stderr(errno, "[alert] could not open error log file: open() \"%s\" failed", plogname);
    }
    return;
}

// 描述：通过可变参数组合出字符串【支持...省略号形参】，自动往字符串最末尾增加换行符【所以调用者不用加\n】， 往标准错误上输出这个字符串；
// 调用格式比如：qc_log_stderr(0, "invalid option: \"%s\",%d", "testinfo",123);
void qc_log_stderr(int err, const char *fmt, ...)
{
    // 可变参列表
    va_list args;
    u_char errstr[QC_MAX_ERROR_STR + 1]; // 2048 + \0
    u_char *p, *last;
    // va_end处理之前有必要，否则字符串没有结束标记不行的
    memset(errstr, 0, sizeof(errstr));
    // last指向整个buffer最后去了【指向最后一个有效位置的后面也就是非有效位】，作为一个标记，防止输出内容超过这么长
    last = errstr + QC_MAX_ERROR_STR;
    p = qc_cpymem(errstr, "quickcomm: ", 11);
    // 使args指向起始的参数
    va_start(args, fmt);
    // 组合出这个字符串保存在errstr里
    p = qc_vslprintf(p, last, fmt, args);
    va_end(args); // 释放args
    if (err)      // 如果错误代码不是0，表示有错误发生
    {
        // 错误代码和错误信息也要显示出来
        p = qc_log_errno(p, last, err);
    }
    // 若位置不够，那换行也要插入到末尾，哪怕覆盖内容
    if (p >= (last - 1))
    {
        p = (last - 1) - 1;
    }
    *p++ = '\n';
    // 往标准错误输出信息
    write(STDERR_FILENO, errstr, p - errstr);

    if (qc_log.fd > STDERR_FILENO) // 如果是有效的日志文件，那么就写到日志文件中
    {
        // 不要再次把错误信息弄到字符串里，否则字符串里重复了
        err = 0;
        --p;
        *p = 0;
        qc_log_error_core(QC_LOG_STDERR, err, (const char *)errstr);
    }
    return;
}
//往日志文件中写日志，代码中有自动加换行符，所以调用时字符串不用刻意加\n；
//如果定向为标准错误，则直接往屏幕上写日志【比如日志文件打不开，则会直接定位到标准错误，此时日志就打印到屏幕上
//level:一个等级数字，我们把日志分成一些等级，以方便管理、显示、过滤等等，如果这个等级数字比配置文件中的等级数字"LogLevel"大，那么该条信息不被写到日志文件中
//err：是个错误代码，如果不是0，就应该转换成显示对应的错误信息,一起写到日志文件中，
void qc_log_error_core(int level, int err, const char *fmt, ...)
{
    u_char *last;
    u_char errstr[QC_MAX_ERROR_STR + 1];

    memset(errstr, 0, sizeof(errstr));
    last = errstr + QC_MAX_ERROR_STR;

    timeval tv;
    tm      tm;
    time_t  sec;//秒
    u_char  *p; //指向当前要拷贝数据到其中的内存位置
    va_list args;

    memset(&tv, 0, sizeof(timeval));
    memset(&tm, 0, sizeof(tm));
    //获取当前时间，返回自1970-01-01 00:00:00到现在经历的秒数【第二个参数是时区，一般不关心】
    gettimeofday(&tv, nullptr);

    sec = tv.tv_sec;
    localtime_r(&sec, &tm);
    tm.tm_mon++;
    tm.tm_year += 1900;
    //组合出一个当前时间字符串，格式形如：2025/01/08 19:57:11
    u_char strcurrtime[40] = {0};
    qc_slprintf(strcurrtime,
                (u_char *)-1,
                "%4d/%02d/%02d %02d:%02d:%02d",
                tm.tm_year, tm.tm_mon,
                tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec);
    //日期增加进来，得到形如：     2025/01/08 20:26:07
    p = qc_cpymem(errstr, strcurrtime, strlen((const char *)strcurrtime));
    //日志级别增加进来，得到形如：  2025/01/08 20:26:07 [crit]
    p = qc_slprintf(p, last, " [%s] ", err_levels[level]);
    //支持%P格式，进程id增加进来，得到形如：   2025/01/08 20:50:15 [crit] 2037:
    p = qc_slprintf(p, last, "%P: ", qc_pid);

    va_start(args, fmt);
    //把fmt和args参数弄进去，组合出来这个字符串
    p = qc_vslprintf(p, last, fmt, args);
    va_end(args);

    if (err)//如果错误代码不是0，表示有错误发生
    {
        p = qc_log_errno(p, last, err);
    }
    //若位置不够，那换行也要硬插入到末尾，哪怕覆盖到其他内容
    if (p >= (last - 1))
    {
        p = (last - 1) - 1; //把尾部空格留出来
                             //我觉得，last-1，才是最后 一个而有效的内存，而这个位置要保存\0，所以我认为再减1，这个位置，才适合保存\n
    }
    *p++ = '\n'; //增加个换行符       

    ssize_t   n;
    while(1) 
    {        
        if (level > qc_log.log_level) 
        {
            //要打印的这个日志的等级太落后（等级数字太大，比配置文件中的数字大)
            //这种日志就不打印了
            break;
        }
        //磁盘是否满了的判断，先算了吧，还是由管理员保证这个事情吧； 

        //写日志文件        
        n = write(qc_log.fd,errstr,p - errstr);  //文件写入成功后，如果中途
        if (n == -1) 
        {
            //写失败有问题
            if(errno == ENOSPC) //写失败，且原因是磁盘没空间了
            {
                //磁盘没空间了
                
            }
            else
            {
                //这是有其他错误，那么考虑把这个错误显示到标准错误设备吧；
                if(qc_log.fd != STDERR_FILENO) //当前是定位到文件的，则条件成立
                {
                    n = write(STDERR_FILENO,errstr,p - errstr);
                }
            }
        }
        break;
    } //end while    
    return;

}

//描述：给一段内存，一个错误编号，我要组合出一个字符串，形如(错误编号: 错误原因)，放到给的这段内存中去
u_char *qc_log_errno(u_char *buf, u_char *last, int err)
{
    char* perrorinfo = strerror(err);
    size_t len = strlen(perrorinfo);

    //插入一些字符串
    char leftstr[10] = {0};
    sprintf(leftstr, " (%d: ", err);
    size_t leftlen = strlen(leftstr);
    char rightstr[] = ")";
    size_t rightlen = strlen(rightstr);

    size_t extralen = leftlen + rightlen;
    if ((buf + len + extralen) < last)
    {
        //保证整个我装得下，我就装，否则我全部抛弃 ,
        //nginx的做法是 如果位置不够，就硬留出50个位置【哪怕覆盖掉以往的有效内容】，也要硬往后边塞，这样当然也可以；
        buf = qc_cpymem(buf, leftstr, leftlen);
        buf = qc_cpymem(buf, perrorinfo, len);
        buf = qc_cpymem(buf, rightstr, rightlen);
    }
    return buf;
}
