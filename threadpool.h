#include <queue>
#include <vector>
#include <atomic>
#include <functional>
#include <memory>
#include <iostream>
#include <future>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
using namespace std;

const int TASK_MAX_THRESHHOLD = 30;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;

enum class PoolMode
{
    MODE_FIXED,
    MODE_CACHED,
};

class Thread
{
public:
    using ThreadFunc = function<void(int)>;

    Thread(ThreadFunc func) : func_(func), threadId_(generateId_++) {}
    ~Thread() = default;

    void start()
    {
        thread t(func_, threadId_);
        t.detach();
    }

    int getId()
    {
        return threadId_;
    }

private:
    ThreadFunc func_;
    int threadId_;
    static int generateId_;
};

int Thread::generateId_ = 0;

class ThreadPool
{
public:
    ThreadPool()
        : initThreadSize_(0), taskSize_(0), idleThreadSize_(0), curThreadSize_(0), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD), taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), poolMode_(PoolMode::MODE_FIXED), isPoolRunning_(false) {}
    ~ThreadPool()
    {
        isPoolRunning_ = false;

        std::unique_lock<mutex> lock(taskQueMtx_);
        notEmpty_.notify_all();
        exitCond_.wait(lock, [&]()
                       { return curThreadSize_ == 0; });
    }
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    void setmode(PoolMode mode)
    {
        if (!isPoolRunning_)
        {
            return;
        }
        poolMode_ = mode;
    }

    void setTaskQueMaxThreshHold(int threshhold)
    {
        if (!isPoolRunning_)
        {
            return;
        }
        taskQueMaxThreshHold_ = threshhold;
    }

    void setThreadSizeThreshHold(int threshhold)
    {
        if (!isPoolRunning_)
        {
            return;
        }
        threadSizeThreshHold_ = threshhold;
    }

    void start(int initThreadSize = thread::hardware_concurrency())
    {
        isPoolRunning_ = true;

        initThreadSize_ = initThreadSize;
        curThreadSize_ = initThreadSize;

        for (int i = 0; i < initThreadSize_; ++i)
        {
            auto ptr = make_unique<Thread>(bind(&ThreadPool::threadFunc, this, placeholders::_1));
            int threadId = ptr->getId();
            threads_.emplace(threadId, move(ptr));
        }
        for (int i = 0; i < initThreadSize_; ++i)
        {
            threads_[i]->start();
            idleThreadSize_++;
        }
    }

    template <typename Func, typename... Args>
    auto submitTask(Func &&func, Args &&...args) -> std::future<decltype(func(args...))>
    {
        using Rtype = decltype(func(args...));
        auto task = make_shared<packaged_task<Rtype()>>(bind(forward<Func>(func), forward<Args>(args)...));
        future<Rtype> result = task->get_future();

        unique_lock<mutex> lock(taskQueMtx_);
        if (!notFull_.wait_for(lock, chrono::milliseconds(100), [&]()
                               { return taskQue_.size() < (size_t)TASK_MAX_THRESHHOLD; }))
        {
            cerr << "任务提交失败." << endl;
            auto task = make_shared<packaged_task<Rtype()>>([]()
                                                            { return Rtype(); });
            (*task)();
            return task->get_future();
        }

        taskQue_.emplace([task]()
                         { (*task)(); });
        taskSize_++;
        notEmpty_.notify_all();

        if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
        {
            cout << ">>> create new thread... <<<" << endl;

            int createingThreadSize = min(threadSizeThreshHold_ - curThreadSize_, taskSize_ - idleThreadSize_) / 2 + 1;
            for (int i = 0; i < createingThreadSize; ++i)
            {
                auto ptr = make_unique<Thread>(bind(&ThreadPool::threadFunc, this, placeholders::_1));
                int threadId = ptr->getId();
                threads_.emplace(threadId, move(ptr));
                threads_[threadId]->start();

                curThreadSize_++;
                idleThreadSize_++;
            }
        }

        return result;
    }

    void threadFunc(int threadid)
    {
        auto lasttime = chrono::high_resolution_clock().now();

        for (;;)
        {
            Task task;
            {
                unique_lock<mutex> lock(taskQueMtx_);

                while (taskQue_.size() == 0)
                {
                    if (!isPoolRunning_)
                    {
                        // 线程池要结束了
                        curThreadSize_--;
                        idleThreadSize_--;
                        threads_.erase(threadid);
                        cout << "tid: " << this_thread::get_id() << "退出!" << endl;
                        exitCond_.notify_all();
                        return;
                    }
                    if (poolMode_ == PoolMode::MODE_CACHED)
                    {
                        if (cv_status::timeout == notEmpty_.wait_for(lock, chrono::seconds(1)))
                        {
                            auto now = chrono::high_resolution_clock().now();
                            auto dur = chrono::duration_cast<chrono::seconds>(now - lasttime);
                            if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                            {
                                threads_.erase(threadid);
                                curThreadSize_--;
                                idleThreadSize_--;
                                cout << "tid: " << this_thread::get_id() << "退出!" << endl;
                                return;
                            }
                        }
                    }
                    else
                    {
                        notEmpty_.wait(lock);
                    }
                }

                idleThreadSize_--;
                cout << "tid: " << this_thread::get_id() << "获取任务成功" << endl;
                task = taskQue_.front();
                taskQue_.pop();
                taskSize_--;

                if (taskQue_.size() > 0)
                {
                    notEmpty_.notify_all();
                }

                notFull_.notify_all();
            }

            if (task != nullptr)
            {
                cout << "tid: " << this_thread::get_id() << "正在执行任务" << endl;
                task();
                cout << "tid: " << this_thread::get_id() << "完成任务,开始获取下一任务" << endl;
            }
            idleThreadSize_++;
            auto lasttime = chrono::high_resolution_clock().now();
        }
    }

private:
    int initThreadSize_;
    int threadSizeThreshHold_;
    atomic_int curThreadSize_;
    atomic_int idleThreadSize_;

    using Task = function<void()>;
    queue<Task> taskQue_;
    atomic_int taskSize_;
    int taskQueMaxThreshHold_;

    unordered_map<int, unique_ptr<Thread>> threads_;

    mutex taskQueMtx_;
    condition_variable notFull_;
    condition_variable notEmpty_;
    condition_variable exitCond_;

    PoolMode poolMode_;
    atomic_bool isPoolRunning_;
};
