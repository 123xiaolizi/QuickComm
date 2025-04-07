#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   //uintptr_t
#include <stdarg.h>   //va_start....
#include <unistd.h>   //STDERR_FILENO等
#include <sys/time.h> //gettimeofday
#include <time.h>     //localtime_r
#include <fcntl.h>    //open
#include <errno.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "qc_public.h"
#include "qc_conf.h"
#include "qc_macro.h"
#include "qc_socket.h"

void CSocket::qc_event_accept(lpqc_connection_t oldc)
{
    struct sockaddr mysockaddr;
    socklen_t socklen;
    int err;
    int level;
    int s;
    static int use_accept4 = 1;
    lpqc_connection_t newc;

    socklen = sizeof(mysockaddr);
    do
    {
        if (use_accept4)
        {
            // accept4可以直接设置成非阻塞
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK);
        }
        else
        {
            s = accept(oldc->fd, &mysockaddr, &socklen);
        }

        if (s == -1)
        {
            err = errno;
            // 对accept、send和recv而言，事件未发生时errno通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）
            if (err == EAGAIN)
            {
                return;
            }
            level = QC_LOG_ALERT;
            // ECONNRESET错误则发生在对方意外关闭套接字后【您的主机中的软件放弃了一个已建立的连接--由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
            if (err == ECONNABORTED)
            {
                level = QC_LOG_ERR;
            }
            /*
            //EMFILE:进程的fd已用尽【已达到系统所允许单一进程所能打开的文件/套接字总数】
            ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
            /ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有process-specific的resource limits。按照常识，process-specific的resource limits，一定受限于system-wide的resource limits。
            */
            else if (err == EMFILE || err == ENFILE)
            {
                level = QC_LOG_CRIT;
            }
            // ENOSYS:功能未实现
            if (use_accept4 && err == ENOSYS)
            {
                use_accept4 = 0;
                continue;
            }

            // 对方关闭套接字 ECONNABORTED:连接被中止
            if (err == ECONNABORTED)
            {
                // 没想好做点什么,这里应该是可以不处理的
            }
            if (err == EMFILE || err == ENFILE)
            {
                // do nothing，nginx官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
                // 这里目前先不处理
            }
            return;
        } // end if (s == -1)

        // 到这里说明连接成功
        if (m_onlineUserCount >= m_worker_connections)
        {
            // 用户连接数过多，要关闭该用户socket，因为现在也没分配连接，所以直接关闭即可
            close(s);
            return;
        }

        // 如果某些恶意用户连上来发了1条数据就断，不断连接，会导致频繁调用qc_get_connection()使用我们短时间内产生大量连接，危及本服务器安全
        if (m_connectionList.size() > (m_worker_connections * 5))
        {
            if (m_freeconnectionList.size() < m_worker_connections)
            {
                // 整个连接池这么大了，而空闲连接却这么少了，所以我认为是  短时间内 产生大量连接，发一个包后就断开，我们不可能让这种情况持续发生，所以必须断开新入用户的连接
                // 一直到m_freeconnectionList变得足够大【连接池中连接被回收的足够多】
                close(s);
                return;
            }
        }

        // 这是针对新连入用户的连接
        newc = qc_get_connection(s);
        if (newc == nullptr)
        {
            if (close(s) == -1)
            {
                qc_log_error_core(QC_LOG_ALERT, errno, "CSocket::qc_event_accept()中close(%d)失败!", s);
            }
            return;
        }

        // 拷贝客户端地址到连接对象
        memcpy(&newc->s_sockaddr, &mysockaddr, socklen);

        if (!use_accept4) // 如果不是use_accept4()连接的话 需要在将对应的fd设置为非阻塞
        {
            if (setnonblocking(s) == false)
            {
                // 失败直接返回
                return;
            }
        }

        newc->listening = oldc->listening;

        newc->rhandler = &CSocket::qc_read_request_handler;
        newc->whandler = &CSocket::qc_write_request_handler;

        if (qc_epoll_oper_event(
                s,                      //Socket句柄 
                EPOLL_CTL_ADD,          //事件类型，这里是增加
                EPOLLIN | EPOLLRDHUP,   //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭 ，如果边缘触发模式可以增加 EPOLLET
                0,                      //对于事件类型为增加的，不需要这个参数
                newc) == -1)                  //连接池中的连接
        {
            //失败了 关闭
            qc_close_connection(newc);
            return;
        }
        
        //是否开启计时踢出非活动用户
        if(m_ifkickTimeCount == 1)
        {
            AddToTimerQueue(newc);
        }

        ++m_onlineUserCount;//用户数量加一
        break;
    }while(1);
    return;
}