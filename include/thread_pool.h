#pragma once
#include <unordered_map>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <iostream>
#include <future>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // 单位：秒

//线程对象
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func) :func_(func), threadID(generaID_++)
	{}

	~Thread() = default;

	//启动线程
	void start()
	{
		std::thread t(func_, threadID);
		std::cout << "create thread" << t.get_id() << std::endl;
		t.detach();
	}

	int getId() const
	{
		return threadID;
	}

private:
	static int generaID_;
	int threadID;
	ThreadFunc func_;
};

int Thread::generaID_ = 0;

// 线程池支持的模式
enum class PoolMode
{
	MODE_FIXED,  // 固定数量的线程
	MODE_CACHED, // 线程数量可动态增长
};

class ThreadPool
{
public:
	ThreadPool() :
		currentTaskSize_(0),
		spareThreadSize_(0),
		MaxThreadSize_(THREAD_MAX_THRESHHOLD),
		MaxTaskSize_(TASK_MAX_THRESHHOLD),
		initThreadSize_(0),
		currentThreadSize_(0),
		poolMode_(PoolMode::MODE_FIXED),
		isPoolRunning_(false)
	{}
	~ThreadPool()
	{
		isPoolRunning_ = false;
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool {return thread_list_.size() == 0; });
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool* operator= (const ThreadPool&) = delete;

	//启动线程池
	void start(int threadsize = std::thread::hardware_concurrency())
	{
		//设置线程池为启动状态
		isPoolRunning_ = true;
		initThreadSize_ = threadsize;
		currentThreadSize_ = threadsize;
		//创建thread对象
		for (int i = 0; i < threadsize && i < MaxThreadSize_; ++i)
		{
			auto thread_ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int thread_id = thread_ptr->getId();
			thread_list_.emplace(thread_id, std::move(thread_ptr));
		}
		for (int i = 0; i < thread_list_.size(); ++i)
		{
			thread_list_[i]->start();
			++spareThreadSize_;
		}
	}

	// 设置线程池的工作模式
	void setMode(PoolMode mode)
	{
		poolMode_ = mode;
	}

	// 设置task任务队列上线阈值
	void setTaskQueMaxThreshHold(int threshhold)
	{
		MaxTaskSize_ = threshhold;
	}

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold)
	{
		MaxThreadSize_ = threshhold;
	}

	// 给线程池提交任务
	//Result submitTask(std::shared_ptr<Task> sp);
	template<typename Func, typename... Args>       //推导返回值类型
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//打包任务
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			//使用forward完美转发，保持参数的左右值特性
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		//get_future() 不会阻塞，它只是返回一个与 std::packaged_task 关联的 std::future 对象
		std::future<RType> result = task->get_future();

		std::unique_lock<std::mutex> lock(taskQueMtx_);
		//提交任务阻塞时长不超1秒
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return TaskQueue_.size() < MaxTaskSize_; }))
		{
			std::cerr << "task queue is full, submit task fail.---- time_out" << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>> (
				[]()->RType {return RType(); });
			(*task)();
			return task->get_future();
		}
		//走到这里说明有任务队列还有空间
		TaskQueue_.emplace([task]() {(*task)(); });
		currentTaskSize_++;

		//通知工作线程
		notEmpty_.notify_all();

		// cached模式 任务处理比较紧急  判断是否需要创建线程
		if (poolMode_ == PoolMode::MODE_CACHED
			&& spareThreadSize_ < currentTaskSize_	//空闲线程小于任务数
			&& currentThreadSize_ < MaxThreadSize_) //当前线程小于设定的线程上限
		{
			//创建一个Thread对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int thread_id = ptr->getId();
			thread_list_.emplace(thread_id, std::move(ptr));

			thread_list_[thread_id]->start();
			++spareThreadSize_;
			++currentThreadSize_;
		}
		return result;
	}

private:
	// 定义线程函数
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();
		for (;;)
		{
			Task task;
			{
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid:" << std::this_thread::get_id()
					<< "尝试获取任务..." << std::endl;

				while (TaskQueue_.size() == 0)
				{
					if (!isPoolRunning_)//线程池结束 回收线程资源
					{
						thread_list_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
						exitCond_.notify_all();
						return;//线程结束
					}

					if (poolMode_ == PoolMode::MODE_CACHED)
					{
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() > THREAD_MAX_IDLE_TIME
								&& currentThreadSize_ > initThreadSize_
								&& currentThreadSize_ > currentTaskSize_)
							{
								thread_list_.erase(threadid);
								std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
								--currentThreadSize_;
								--spareThreadSize_;
								return;//线程结束
							}
						}
					}
					else
					{
						notEmpty_.wait(lock);
					}
				}//end while(TaskQueue_.size() == 0)

				--currentThreadSize_;

				task = TaskQueue_.front();
				TaskQueue_.pop();
				--spareThreadSize_;
				std::cout << "tid:" << std::this_thread::get_id()
					<< "获取任务成功..." << std::endl;

				//看看还有没有任务，有的话通知别的线程
				if (TaskQueue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				notFull_.notify_all();//通知可以提交任务
			}

			// 当前线程负责执行这个任务
			if (task != nullptr)
			{
				// task->run(); // 执行任务；把任务的返回值setVal方法给到Result
				task();
			}

			++spareThreadSize_;
			lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完任务的时间

		}//end for (;;)
	}

	// 检查pool的运行状态
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}
private:
	//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> thread_list_;
	//当前线程数
	std::atomic_int currentThreadSize_;
	//空闲线程数
	std::atomic_int spareThreadSize_;
	//最大支持线程数
	int MaxThreadSize_;
	//默认线程数
	int initThreadSize_;

	//任务队列
	using Task = std::function<void()>;
	std::queue<Task> TaskQueue_;

	//当前任务数
	std::atomic_int currentTaskSize_;
	//任务数量最大阈值
	int MaxTaskSize_;

	//用于操作队列的锁
	std::mutex taskQueMtx_;
	//表示队列不满
	std::condition_variable notFull_;
	//表示队列不空
	std::condition_variable notEmpty_;
	// 等到线程资源全部回收
	std::condition_variable exitCond_;

	//线程池工作模式
	PoolMode poolMode_;
	//线程池是否在工作
	std::atomic_bool isPoolRunning_;


};