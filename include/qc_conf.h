
#ifndef __CONF_H__
#define __CONF_H__

#include <vector>


//结构定义
typedef struct _CConfItem
{
	char ItemName[50];
	char ItemContent[500];
}CConfItem,*LPCConfItem;

class CConfig
{

private:
	CConfig() = default;
    
public:
    ~CConfig();

    CConfig& operator=(const CConfig&) = delete;

//---------------------------------------------------
public:
    //获取CConfig对象
    static CConfig& GetInstance()
    {
        static CConfig instance;
        return instance;
    }
    //装载配置文件
    bool Load(const char *pconfName);
    //根据ItemName获取配置信息字符串，不修改不用互斥
	const char *GetString(const char *p_itemname);
    //根据ItemName获取数字类型配置信息
	int  GetIntDefault(const char *p_itemname,const int def);

public:
	std::vector<LPCConfItem> m_ConfigItemList; //存储配置信息的列表

};

#endif
