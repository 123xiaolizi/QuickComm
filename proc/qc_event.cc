#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "qc_conf.h"
#include "qc_macro.h"
#include "qc_public.h"

//处理网络事件和定时器事件，我们遵照nginx引入这个同名函数
void qc_process_events_and_timers()
{
    g_socket.qc_epoll_process_events(-1); //-1表示卡着等待吧

    //统计信息打印，考虑到测试的时候总会收到各种数据信息，所以上边的函数调用一般都不会卡住等待收数据
    g_socket.printTDInfo();
    
    //...再完善
}
