#ifndef __QC_MEMORY_H__
#define __QC_MEMORY_H__

#include <stddef.h>


class CMemory
{
private:
    CMemory(){}//要设计成单例
public:
    ~CMemory(){};

private:
    static CMemory *m_instance;

public:
    static CMemory* GetInstance()
    {
        if(m_instance == nullptr)
        {
            if (m_instance == nullptr)
            {
                m_instance = new CMemory();
                static CGarhuishou c1;
            }  
        }
        return m_instance;
    }

    class CGarhuishou
    {
        public:
            ~CGarhuishou()
            {
                if(CMemory::m_instance)
                {
                    delete CMemory::m_instance;
                    CMemory::m_instance = nullptr;
                }
            }
    };
public:
    void *AllocMemory(int memCount, bool ifmemset);
    void FreeMemory(void *point);

};





#endif