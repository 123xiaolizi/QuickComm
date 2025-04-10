#ifndef __LOCKMUTEX_H__
#define __LOCKMUTEX_H__

#include <pthread.h> 
//本类用于自动释放互斥量，防止忘记调用pthread_mutex_unlock的情况发生
class CLock
{
public:
	CLock(pthread_mutex_t *pMutex)
	{
		m_pMutex = pMutex;
		pthread_mutex_lock(m_pMutex); //加锁互斥量
	}
	~CLock()
	{
		pthread_mutex_unlock(m_pMutex); //解锁互斥量
	}
private:
	pthread_mutex_t *m_pMutex;
    
};

#endif