#include "qc_public.h"
#include <string.h>

//去除尾部空格
void  Rtrim(char *string)
{
    size_t len = 0;
    if(string == nullptr)
    {
        return;
    }
    len = strlen(string);
    while(len > 0 && string[len-1] == ' ')
    {
        string[--len] = 0;
    }
    return;

}
//去除首部空格
void  Ltrim(char *string)
{
    size_t len = 0;
    len = strlen(string);
    char *p_tmp = string;
    //如果第一个字符不是空格就直接返回了
    if ((*p_tmp) != ' ')
        return;
    //找到第一个不是空格的字符
    while((*p_tmp) != '\0')
    {
        if((*p_tmp) == ' ')
        {
            ++p_tmp;
        }
        else
            break;
    }
    if ((*p_tmp) == '\0')
    {
        *string = '\0';
        return;
    }

    //将p_tmp 复制到string
    char* p_tmp2 = string;
    while ( (*p_tmp) != '\0')
    {
        (*p_tmp2++) = (*p_tmp++);
    }
    (*p_tmp2) = '\0';
    return;
    
}