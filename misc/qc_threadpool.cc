#include <stdarg.h>
#include <unistd.h>

#include "qc_public.h"
#include "qc_memory.h"
#include "qc_threadpool.h"
#include "qc_macro.h"

// 静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER; // #define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER;    // #define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_shutdown = false;                                    // 刚开始标记整个线程池的线程是不退出的

CThreadPool::CThreadPool()
{
    m_iRunningThreadNum = 0;
    m_iLastEmgTime = 0;
    m_iRecvMsgQueueCount = 0;
}
CThreadPool::~CThreadPool()
{
    clearMsgRecvQueue();
}
// 创建该线程池中的所有线程
bool CThreadPool::Create(int threadNum)
{
    ThreadItem *pNew;
    int err;

    m_iThreadNum = threadNum;

    for (int i = 0; i < m_iThreadNum; ++i)
    {
        m_threadVector.push_back(pNew = new ThreadItem(this));
        err = pthread_create(&pNew->_Handle, nullptr, ThreadFunc, pNew);
        if (err != 0)
        {
            // 创建线程有错
            qc_log_stderr(err, "CThreadPool::Create()创建线程%d失败，返回的错误码为%d!", i, err);
            return false;
        }
        else
        {
            // 创建线程成功
            // qc_log_stderr(0,"CThreadPool::Create()创建线程%d成功,线程id=%d",pNew->_Handle);
        }
    }
    // 我们必须保证每个线程都启动并运行到pthread_cond_wait()，本函数才返回，只有这样，这几个线程才能进行后续的正常工作
    std::vector<ThreadItem *>::iterator iter;
lblfor:
    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); ++iter)
    {
        if ((*iter)->ifrunning == false)
        {
            // 说明还有没完全启动的线程
            usleep(100 * 1000); // 等待100毫秒
            goto lblfor;
        }
    }
    return true;
}
// 使线程池中的所有线程退出
void CThreadPool::StopAll()
{
    //(1)已经调用过，就不要重复调用了
    if (m_shutdown == true)
    {
        return;
    }
    m_shutdown = true;

    //(2)唤醒等待该条件【卡在pthread_cond_wait()的】的所有线程，一定要在改变条件状态以后再给线程发信号
    int err = pthread_cond_broadcast(&m_pthreadCond);
    if (err != 0)
    {
        // 这肯定是有问题，要打印紧急日志
        qc_log_stderr(err, "CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!", err);
        return;
    }

    //(3)等等线程，让线程真返回
    std::vector<ThreadItem *>::iterator iter;
    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); // 等待一个线程终止
    }

    // 流程走到这里，那么所有的线程池中的线程肯定都返回了；
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);

    //(4)释放一下new出来的ThreadItem【线程池中的线程】
    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if (*iter)
            delete *iter;
    }
    m_threadVector.clear();

    qc_log_stderr(0, "CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");
    return;
}
// 收到一个完整消息后，入消息队列，并触发线程池中线程来处理该消息
void CThreadPool::inMsgRecvQueueAndSignal(char *buf)
{
    // 互斥
    int err = pthread_mutex_lock(&m_pthreadMutex);
    if (err != 0)
    {
        qc_log_stderr(err, "CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_lock()失败，返回的错误码为%d!", err);
    }

    m_MsgRecvQueue.push_back(buf); // 入消息队列
    ++m_iRecvMsgQueueCount;        // 收消息队列数字+1，个人认为用变量更方便一点，比 m_MsgRecvQueue.size()高效

    // 取消互斥
    err = pthread_mutex_unlock(&m_pthreadMutex);
    if (err != 0)
    {
        qc_log_stderr(err, "CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_unlock()失败，返回的错误码为%d!", err);
    }

    // 可以激发一个线程来干活了
    Call();
    return;
}

// 来任务了，调一个线程池中的线程下来干活
void CThreadPool::Call()
{
    int err = pthread_cond_signal(&m_pthreadCond); // 唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
    if (err != 0)
    {
        // 这是有问题啊，要打印日志啊
        qc_log_stderr(err, "CThreadPool::Call()中pthread_cond_signal()失败，返回的错误码为%d!", err);
    }

    //(1)如果当前的工作线程全部都忙，则要报警
    // bool ifallthreadbusy = false;
    if (m_iThreadNum == m_iRunningThreadNum) // 线程池中线程总量，跟当前正在干活的线程数量一样，说明所有线程都忙碌起来，线程不够用了
    {
        // 线程不够用了
        // ifallthreadbusy = true;
        time_t currtime = time(NULL);
        if (currtime - m_iLastEmgTime > 10) // 最少间隔10秒钟才报一次线程池中线程不够用的问题；
        {
            // 两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
            m_iLastEmgTime = currtime; // 更新时间
            // 写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
            qc_log_stderr(0, "CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    } // end if

    return;
}
// 新线程的线程回调函数
void *CThreadPool::ThreadFunc(void *threadData)
{

}
// 清理消息队列
void CThreadPool::clearMsgRecvQueue()
{
    char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();

	//尾声阶段，需要互斥？该退的都退出了，该停止的都停止了，应该不需要退出了
	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();		
		m_MsgRecvQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}
}