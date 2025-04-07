#ifndef __PUBLIC_H__
#define __PUBLIC_H__
#include "qc_socket.h"
#include "qc_threadpool.h"
#include <sys/types.h>
#include <cstdarg>
#include <csignal>
#include "qc_slogic.h"

// 和运行日志相关
typedef struct
{
	int log_level; // 日志级别 或者日志类型，qc_macro.h里分0-8共9个级别
	int fd;		   // 日志文件描述符

}qc_log_t;


////////////////////////////////////外部全局量声明////////////////////////////////////

// 和进程本身有关的全局量
extern pid_t qc_pid; // 声明（告诉编译器变量在其他地方定义）
extern pid_t qc_parent;
extern int qc_process;
extern qc_log_t qc_log;
extern sig_atomic_t  qc_reap; 
extern int g_stopEvent;

extern size_t g_argvneedmem;
extern size_t g_envneedmem;
extern int g_os_argc;
extern char **g_os_argv;
extern char *gp_envmem;
extern int g_daemonized;

//extern CSocket  g_socket;
extern CLogicSocket  g_socket;  
extern CThreadPool   g_threadpool;



  
extern int           g_stopEvent;




/////////////////////////////////////////////一些函数声明////////////////////////////////////////////////////

// 字符串相关函数
void Rtrim(char *string);
void Ltrim(char *string);

// 日志相关
void qc_log_init();
void qc_log_stderr(int err, const char *fmt, ...);
void qc_log_error_core(int level, int err, const char *fmt, ...);
u_char *qc_log_errno(u_char *buf, u_char *last, int err);
u_char *qc_snprintf(u_char *buf, size_t max, const char *fmt, ...);
u_char *qc_slprintf(u_char *buf, u_char *last, const char *fmt, ...);
u_char *qc_vslprintf(u_char *buf, u_char *last, const char *fmt, va_list args);

// 设置可执行程序标题相关函数
void qc_init_setproctitle();
void qc_setproctitle(const char *title);

// 和信号/主流程相关相关
int qc_init_signals();
void qc_master_process_cycle();
int qc_daemon();
void qc_process_events_and_timers();
#endif