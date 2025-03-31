#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  //env
#include <string.h>
#include "qc_public.h"


//将原本的环境内存，拷贝到gp_envmem，为设置可执行程序重命名做准备
void   qc_init_setproctitle()
{
    //这里无需判断penvmen == NULL,有些编译器new会返回NULL
    //如果在重要的地方new失败了，让程序失控崩溃，帮助发现问题
    gp_envmem = new char[g_argvneedmem];
    memset(gp_envmem, 0, g_argvneedmem);

    char *ptmp = gp_envmem;
    //把原本内存拷贝到新内存
    for(int i = 0; environ[i] ; ++i)
    {
        //strlen是不包括字符串末尾的\0,所以这里加一
        size_t len = strlen(environ[i]) + 1;
        strcpy(ptmp, environ[i]);
        environ[i] = ptmp;
        ptmp += len;
    }
    return;
}



//设置可执行程序标题
void   qc_setproctitle(const char *title)
{
    //我们假设，所有的命令 行参数我们都不需要用到了，可以被随意覆盖了；
    //注意：我们的标题长度，不会长到原始标题和原始环境变量都装不下，否则怕出问题，不处理

    //计算标题长度
    size_t title_len = strlen(title);

    //计算总的原始的argv那块内存的总长度
    size_t esy = g_argvneedmem + g_envneedmem;
    if (esy <= title_len)
    {
        //标题过长就不进行设置了
        return;
    }

    //设置后续的命令行参数为空，表示只有argv[]中只有一个元素了，这是好习惯；防止后续argv被滥用，因为很多判断是用argv[] == NULL来做结束标记判断的;
    g_os_argv[1] = NULL;

    //设置标题，将原来的命令行参数都会覆盖掉，不要再使用这些命令行参数
    char *ptmp = g_os_argv[0];
    strcpy(ptmp, title);
    ptmp += title_len;//跳过标题

    //把剩余的原argv以及environ所占的内存全部清0，否则会出现在ps的cmd列可能还会残余一些没有被覆盖的内容
    size_t cha = esy - title_len;
    memset(ptmp, 0, cha);
    return;
}