#ifndef __COMM_H__
#define __COMM_H__

//每个包的最大长度（包头+包体)实际上编码是，包头+包体长度必须不大于 这个值-1000【29000】
#define _PKG_MAX_LENGTH      30000

//通信 收包状态定义
#define _PKG_HD_INIT         0  //初始状态，准备接收数据包头
#define _PKG_HD_RECVING      1  //接收包头中，包头不完整，继续接收中
#define _PKG_BD_INIT         2  //包头刚好收完，准备接收包体
#define _PKG_BD_RECVING      3  //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态

#define _DATA_BUFSIZE_       20 //定义一个固定大小的数组专门用来收包头

//结构定义
#pragma pack(1);//1字节对齐
//包头结构
typedef struct _COMM_PKG_HEADER
{
    //报文总长度【包头+包体】--2字节
    unsigned short pkgLen;
    //消息类型代码--2字节，用于区别每个不同的消息类型
    unsigned short msgCode;
    //CRC32校验码
    int            crc32;
}COMM_PKG_HEADER,*LPCOMM_PKG_HEADER;
#pragma pack()



#endif