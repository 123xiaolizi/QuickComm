#include "qc_public.h"
#include "qc_memory.h"
#include "qc_socket.h"
#include "qc_lockmutex.h"
#include "qc_conf.h"
#include <errno.h> //errno
// #include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <pthread.h> //多线程
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



//设置踢出时钟(向map表中增加内容)
void CSocket::AddToTimerQueue(lpqc_connection_t pConn)
{
    CMemory *p_memory = CMemory::GetInstance();

    time_t futtime = time(nullptr);
    futtime += m_iWaitTime; //20秒后的时间

    CLock lock(&m_timequeueMutex);
    LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader, false);
    tmpMsgHeader->pConn = pConn;
    tmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
    m_timerQueuemap.insert(std::make_pair(futtime, tmpMsgHeader));
    m_cur_size_++;
    m_timer_value_ = GetEarliestTime();
    return;
}
//从multimap中取得最早的时间返回去
time_t  CSocket::GetEarliestTime()
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;	
	pos = m_timerQueuemap.begin();		
	return pos->first;
}
//从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥，
LPSTRUC_MSG_HEADER CSocket::RemoveFirstTimer()
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;
    LPSTRUC_MSG_HEADER p_tmp;
    if(m_cur_size_ <= 0)
    {
        return nullptr;
    }
    pos = m_timerQueuemap.begin();
    p_tmp = pos->second;
    m_timerQueuemap.erase(pos);
    --m_cur_size_;
    return p_tmp;
}
//根据给的当前时间，从m_timeQueuemap找到比这个时间更老（更早）的节点【1个】返回去，这些节点都是时间超过了，要处理的节点
LPSTRUC_MSG_HEADER CSocket::GetOverTimeTimer(time_t cur_time)
{
	CMemory *p_memory = CMemory::GetInstance();
	LPSTRUC_MSG_HEADER ptmp;

	if (m_cur_size_ == 0 || m_timerQueuemap.empty())
		return NULL; //队列为空

	time_t earliesttime = GetEarliestTime(); //到multimap中去查询
	if (earliesttime <= cur_time)
	{
		//这回确实是有到时间的了【超时的节点】
		ptmp = RemoveFirstTimer();    //把这个超时的节点从 m_timerQueuemap 删掉，并把这个节点的第二项返回来；

		if(/*m_ifkickTimeCount == 1 && */m_ifTimeOutKick != 1)  //能调用到本函数第一个条件肯定成立，所以第一个条件加不加无所谓，主要是第二个条件
		{
			//如果不是要求超时就提出，则才做这里的事：

			//因为下次超时的时间我们也依然要判断，所以还要把这个节点加回来        
			time_t newinqueutime = cur_time+(m_iWaitTime);
			LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(sizeof(STRUC_MSG_HEADER),false);
			tmpMsgHeader->pConn = ptmp->pConn;
			tmpMsgHeader->iCurrsequence = ptmp->iCurrsequence;			
			m_timerQueuemap.insert(std::make_pair(newinqueutime,tmpMsgHeader)); //自动排序 小->大			
			m_cur_size_++;       
		}

		if(m_cur_size_ > 0) //这个判断条件必要，因为以后我们可能在这里扩充别的代码
		{
			m_timer_value_ = GetEarliestTime(); //计时队列头部时间值保存到m_timer_value_里
		}
		return ptmp;
	}
	return NULL;
}


//把指定用户tcp连接从timer表中抠出去  
void CSocket::DeleteFromTimerQueue(lpqc_connection_t pConn)
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos,posend;
	CMemory *p_memory = CMemory::GetInstance();

    CLock lock(&m_timequeueMutex);

    //因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下我们遍历整个队列找 一圈，而不是找到一次就拉倒，以免出现什么遗漏
lblMTQM:
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for(; pos != posend; ++pos)	
	{
		if(pos->second->pConn == pConn)
		{			
			p_memory->FreeMemory(pos->second);  //释放内存
			m_timerQueuemap.erase(pos);
			--m_cur_size_; //减去一个元素，必然要把尺寸减少1个;								
			goto lblMTQM;
		}		
	}
	if(m_cur_size_ > 0)
	{
		m_timer_value_ = GetEarliestTime();
	}
    return;  
}
//清理时间队列中所有内容
void CSocket::clearAllFromTimerQueue()
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos,posend;

	CMemory *p_memory = CMemory::GetInstance();	
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();    
	for(; pos != posend; ++pos)	
	{
		p_memory->FreeMemory(pos->second);		
		--m_cur_size_; 		
	}
	m_timerQueuemap.clear();
}

//时间队列监视线程，处理到期不发心跳包的用户踢出的线程
void* CSocket::ServerTimerQueueMonitorThread(void *threadData)
{
	ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
	CSocket *pSocketObj = pThread->_pThis;

	time_t absolute_time, cur_time;
	int err;

	while(g_stopEvent == 0)
	{
		//初步判断
		if(pSocketObj->m_cur_size_ > 0)
		{
			//最近发生事件的时间放absolute_time
			absolute_time = pSocketObj->m_timer_value_;
			cur_time = time(nullptr);
			if(absolute_time < cur_time)
			{
				//时间到
				std::list<LPSTRUC_MSG_HEADER> m_lsIdleList;//保存要处理的内容
				LPSTRUC_MSG_HEADER result;

				err = pthread_mutex_lock(&pSocketObj->m_timequeueMutex);
				if(err != 0) qc_log_stderr(err,"CSocket::ServerTimerQueueMonitorThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);
				while ( (result = pSocketObj->GetOverTimeTimer(cur_time)) != nullptr)
				{
					m_lsIdleList.push_back(result);
				}

				err = pthread_mutex_unlock(&pSocketObj->m_timequeueMutex);
				if(err != 0) qc_log_stderr(err,"CSocket::ServerTimerQueueMonitorThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
				LPSTRUC_MSG_HEADER tmpmsg;
				while (!m_lsIdleList.empty())
				{
					tmpmsg = m_lsIdleList.front();
					m_lsIdleList.pop_front();
					pSocketObj->procPingTimeOutChecking(tmpmsg, cur_time);
				}
			
			}
		}
		usleep(500 * 1000);
	}

	return (void*)0;
}


//心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作
void CSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time)
{
	CMemory *p_memory = CMemory::GetInstance();
	p_memory->FreeMemory(tmpmsg);
}