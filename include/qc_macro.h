#ifndef __MACRO_H__
#define __MACRO_H__

//数字相关--------------------
#define QC_MAX_UINT32_VALUE   (uint32_t) 0xffffffff              //最大的32位无符号数：十进制是‭4294967295‬
#define QC_INT64_LEN          (sizeof("-9223372036854775808") - 1)  

//简单功能函数--------------------
//类似memcpy，但常规memcpy返回的是指向目标dst的指针，而这个qc_cpymem返回的是目标【拷贝数据后】的终点位置，连续复制多段数据时方便
#define qc_cpymem(dst, src, n)   (((u_char *) memcpy(dst, src, n)) + (n))  //注意#define写法，n这里用()包着，防止出现什么错误
#define qc_min(val1, val2)  ((val1 > val2) ? (val2) : (val1)) 

//把日志一共分成八个等级【级别从高到低，数字最小的级别最高，数字大的级别最低】，以方便管理、显示、过滤等等
#define QC_LOG_STDERR            0    //控制台错误【stderr】：最高级别日志，日志的内容写入log参数指定的文件，同时也尝试直接将日志输出到标准错误设备比如控制台屏幕
#define QC_LOG_EMERG             1    //紧急 【emerg】
#define QC_LOG_ALERT             2    //警戒 【alert】
#define QC_LOG_CRIT              3    //严重 【crit】
#define QC_LOG_ERR               4    //错误 【error】：属于常用级别
#define QC_LOG_WARN              5    //警告 【warn】：属于常用级别
#define QC_LOG_NOTICE            6    //注意 【notice】
#define QC_LOG_INFO              7    //信息 【info】
#define QC_LOG_DEBUG             8    //调试 【debug】：最低级别


#define QC_MAX_ERROR_STR   2048   //显示的错误信息最大数组长度
//和日志，打印输出有关
#define QC_ERROR_LOG_PATH       "error.log"


//进程相关----------------------
//标记当前进程类型
#define QC_PROCESS_MASTER     0  //master进程，管理进程
#define QC_PROCESS_WORKER     1  //worker进程，工作进程



#endif