#include"qc_conf.h"
#include"qc_public.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
CConfig::~CConfig()
{
    std::vector<LPCConfItem>::iterator pos;	
	for(pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
	{		
		delete (*pos);
	}//end for
	m_ConfigItemList.clear(); 
    return;
}
//装载配置文件
bool CConfig::Load(const char *pconfName)
{
    FILE *fp = fopen(pconfName, "r");
    if (fp == nullptr)
    {
        return false;
    }
    //存放每行数据
    char data[501];
    while(!feof(fp))
    {
        if(fgets(data, 500, fp) == nullptr) continue;
        if (data[0] == 0) continue;
        //处理注释行
        if(*data == ';' || *data == '#' || *data == ' ' || *data == '\t' || *data == '\n')
            continue;
    lblprocstring:
        //屁股后边若有换行，回车，空格等都截取掉
		if(strlen(data) > 0)
		{
			if(data[strlen(data)-1] == 10 || data[strlen(data)-1] == 13 || data[strlen(data)-1] == 32) 
			{
				data[strlen(data)-1] = 0;
				goto lblprocstring;
			}		
		}
        if (*data == 0 || *data == '[') continue;

        char * item = strchr(data,'=');
        if( item != nullptr)
        {
            LPCConfItem confitem = new CConfItem();
            memset(confitem, 0, sizeof(CConfItem));
            strncpy(confitem->ItemName, data, (int)(item - data));
            strcpy(confitem->ItemContent,item+1);

            Rtrim(confitem->ItemName);
			Ltrim(confitem->ItemName);
			Rtrim(confitem->ItemContent);
			Ltrim(confitem->ItemContent);

            m_ConfigItemList.emplace_back(confitem);
        }
    }
    fclose(fp);
    return true;
}
//根据ItemName获取配置信息字符串，不修改不用互斥
const char *CConfig::GetString(const char *p_itemname)
{
    for ( const auto it : m_ConfigItemList)
    {
        if (strcasecmp(p_itemname,it->ItemName) == 0)
        {
            return it->ItemContent;
        } 
    }
    return nullptr;
}
//根据ItemName获取数字类型配置信息
int  CConfig::GetIntDefault(const char *p_itemname,const int def)
{
    for ( const auto it : m_ConfigItemList)
    {
        if (strcasecmp(p_itemname,it->ItemName) == 0)
        {
            return atoi(it->ItemContent);
        } 
    }
    return def;
}