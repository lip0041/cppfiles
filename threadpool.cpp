// thread pool in c++11
// use packaged_task && future && function && thread && forward && template && etc..
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

const int TASK_MAX_THRESHOLD   = INT32_MAX;
const int THREAD_MAX_THRESHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;

enum class PoolMode {
    MODE_FIXED,  // 运行时不可修改
    MODE_CACHED, // 小而快的任务，任务处理比较紧急的情况，可以增加新的线程
};

class Thread {
public:
    using ThreadFunc = std::function<void(int)>;

    Thread(ThreadFunc func) : func_(func), threadId_(generateId_++) {}
    ~Thread() = default;

    void Start()
    {
        std::thread t(func_, threadId_);
        t.detach();
    }

    int32_t GetId() const { return threadId_; }

private:
    ThreadFunc     func_; // 执行func的线程对象，与一个自定义的threadId绑定
    static int32_t generateId_;
    int32_t        threadId_;
};
int32_t Thread::generateId_ = 0;

class ThreadPool {
public:
    ThreadPool()
        : initThreadSize_(4), taskSize_(0), idleThreadSize_(0), curThreadSize_(0),
          taskQueueMaxThreshold_(TASK_MAX_THRESHOLD), threadSizeThreshold_(THREAD_MAX_THRESHOLD),
          poolMode_(PoolMode::MODE_FIXED), isPoolRunning_(false)
    {
    }

    ~ThreadPool()
    {
        isPoolRunning_.store(false);
        std::unique_lock<std::mutex> lock(taskQueueMutex_);
        notEmpty_.notify_all();
        exitCond_.wait(lock, [&]() -> bool { return threads_.size() == 0; });
    }

    void SetMode(PoolMode mode)
    {
        if (CheckRunningState()) {
            return;
        }
        poolMode_ = mode;
    }

    void SetTaskQueueMaxThreshold(int32_t threshold)
    {
        if (CheckRunningState()) {
            return;
        }
        taskQueueMaxThreshold_ = threshold;
    }

    void SetThreadSizeThreshold(int32_t threshold)
    {
        if (CheckRunningState()) {
            return;
        }
        if (poolMode_ == PoolMode::MODE_CACHED) {
            threadSizeThreshold_ = threshold;
        }
    }

    template <typename Func, typename... Args>
    auto SubmitTask(Func&& func, Args&&... args) -> void
    {
        // 将函数func和参数args打包成 RetType()类型的函数
        using RetType = decltype(func(args...));
        auto task     = std::make_shared<std::packaged_task<RetType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

        // std::future<RetType> result = task->get_future();

        std::unique_lock<std::mutex> lock(taskQueueMutex_);
        if (!notFull_.wait_for(lock, std::chrono::seconds(1),
                               [&]() -> bool { return taskQueue_.size() < (size_t)taskQueueMaxThreshold_; })) {
            std::cerr << "task queue is full, submit task failed" << std::endl /*std::flush*/;
            // 任务池已满，到达最最大值，即达到了cached模式下的上限
            // 返回默认值
            auto task = std::make_shared<std::packaged_task<RetType()>>([]() -> RetType { return RetType(); });
            (*task)();
            // return task->get_future();
            return;
        }
        // 将task进一步转化成void()类型的函数
        taskQueue_.emplace([task]() { (*task)(); });
        ++taskSize_;
        // 任务队列不空，唤醒线程执行
        notEmpty_.notify_all();

        if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ &&
            curThreadSize_ < threadSizeThreshold_) {
            // cached模式下，且已无idle线程，且未达到threshold上限，新建线程用于处理小而快的任务
            std::cout << ">>> create new thread..." << std::endl /*std::flush*/;

            auto    ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
            int32_t threadId = ptr->GetId();
            threads_.emplace(threadId, std::move(ptr));
            threads_[threadId]->Start();
            ++curThreadSize_;
            ++idleThreadSize_;
        }
        // return result;
    }

    void Start(int32_t initThreadSize = std::thread::hardware_concurrency())
    {
        isPoolRunning_.store(true);

        initThreadSize_ = initThreadSize;
        curThreadSize_  = initThreadSize;

        for (size_t i = 0; i < initThreadSize_; ++i) {
            // 建立初始化数量的线程
            auto    ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
            int32_t threadId = ptr->GetId();
            threads_.emplace(threadId, std::move(ptr));
        }

        for (size_t i = 0; i < initThreadSize_; ++i) {
            threads_[i]->Start();
            ++idleThreadSize_;
        }
    }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    // 线程池线程执行体
    void ThreadFunc(int32_t threadId)
    {
        auto lastTime = std::chrono::system_clock::now();

        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(taskQueueMutex_);
                std::cout << "tid: " << std::this_thread::get_id() << ", try to get task..."
                          << std::endl /*std::flush*/;
                // 进到这里面说明，至少该线程是空闲的
                while (taskQueue_.size() == 0) {
                    if (!isPoolRunning_) {
                        threads_.erase(threadId);
                        std::cout << "threadId: " << std::this_thread::get_id() << " exit!" << std::endl /*std::flush*/;
                        exitCond_.notify_all();
                        return;
                    }

                    if (poolMode_ == PoolMode::MODE_CACHED) {
                        if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                            auto now = std::chrono::system_clock::now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                            if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
                                // cached模式下，若该线程是另创建新创建的线程，且60s内未接收新任务，则回收该新创建的线程
                                threads_.erase(threadId);
                                --curThreadSize_;
                                --idleThreadSize_;
                                std::cout << "threadId: " << std::this_thread::get_id() << " exit!" << std::endl
                                    /*std::flush*/;
                                return;
                            }
                        }
                    } else {
                        notEmpty_.wait(lock);
                    }
                }
                --idleThreadSize_;
                std::cout << "tid: " << std::this_thread::get_id() << ", get task successfully..." << std::endl
                    /*std::flush*/;
                // 拿任务，若仍有任务剩余，通知其他线程
                task = taskQueue_.front();
                taskQueue_.pop();
                --taskSize_;

                if (taskQueue_.size() > 0) {
                    notEmpty_.notify_all();
                }

                notFull_.notify_all();
            }

            if (task) {
                task();
            }
            ++idleThreadSize_;
            lastTime = std::chrono::system_clock::now();
        }
    }

    bool CheckRunningState() const { return isPoolRunning_; }

    std::unordered_map<int32_t, std::unique_ptr<Thread>> threads_;

    size_t              initThreadSize_;
    int32_t             threadSizeThreshold_;
    std::atomic_int32_t curThreadSize_;
    std::atomic_int32_t idleThreadSize_;

    using Task = std::function<void()>;
    std::queue<Task>    taskQueue_;
    std::atomic_int32_t taskSize_;
    int32_t             taskQueueMaxThreshold_;

    std::mutex              taskQueueMutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable exitCond_;

    PoolMode         poolMode_;
    std::atomic_bool isPoolRunning_;
};

int sum1(int a, int b)
{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return a + b;
}

int sum2(int a, int b, int c)
{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return a + b + c;
}

// void test1()
// {
//     ThreadPool pool;
//     pool.Start(4);
//     std::future<int> r1 = pool.SubmitTask(sum1, 10, 20);
//     std::future<int> r2 = pool.SubmitTask(sum2, 10, 20, 30);
//     std::future<int> r3 = pool.SubmitTask(
//         [](int b, int e) -> int {
//             int sum = 0;
//             for (int i = b; i < e; ++i)
//                 sum += i;
//             return sum;
//         },
//         1, 1e8);

//     std::cout << r1.get() << std::endl /*std::flush*/;
//     std::cout << r2.get() << std::endl /*std::flush*/;
//     std::cout << r3.get() << std::endl /*std::flush*/;
// }

// void test2()
// {
//     ThreadPool pool;
//     pool.SetMode(PoolMode::MODE_CACHED);
//     pool.Start(2);
//     std::future<int> r1 = pool.SubmitTask(sum1, 10, 20);
//     std::future<int> r2 = pool.SubmitTask(sum2, 10, 20, 30);
//     std::future<int> r3 = pool.SubmitTask(
//         [](int b, int e) -> int {
//             int sum = 0;
//             for (int i = b; i < e; ++i)
//                 sum += i;
//             return sum;
//         },
//         1, 1e8);

//     std::cout << r1.get() << std::endl /*std::flush*/;
//     std::cout << r2.get() << std::endl /*std::flush*/;
//     std::cout << r3.get() << std::endl /*std::flush*/;
// }

void test3()
{
    ThreadPool pool;

    pool.SetMode(PoolMode::MODE_CACHED);
    auto f1 = [](int a, int b) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return a + b;
    };
    auto f2 = [](int a, int b, int c) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return a + b + c;
    };
    pool.Start(2);
    pool.SubmitTask(sum1, 10, 20);
    pool.SubmitTask(sum2, 10, 20, 30);
   pool.SubmitTask(
        [](int b, int e) -> int {
        std::this_thread::sleep_for(std::chrono::seconds(2));
            int sum = 0;
            for (int i = b; i <= e; i++)
                sum += i;
            return sum;
        },
        1, 100);

     pool.SubmitTask(
        [](int b, int e) -> int {
        std::this_thread::sleep_for(std::chrono::seconds(2));
            int sum = 0;
            for (int i = b; i <= e; i++)
                sum += i;
            return sum;
        },
        1, 100);

    pool.SubmitTask(
        [](int b, int e) -> int {
        std::this_thread::sleep_for(std::chrono::seconds(2));
            int sum = 0;
            for (int i = b; i <= e; i++)
                sum += i;
            return sum;
        },
        1, 100);
    // std::future<int> r6 = pool.SubmitTask(sum1, 10, 20);
    // std::future<int> r7 = pool.SubmitTask(sum1, 10, 20);
    // std::future<int> r8 = pool.SubmitTask(sum1, 10, 20);
    // std::future<int> r9 = pool.SubmitTask(sum1, 10, 20);
    // std::future<int> r10 = pool.SubmitTask(sum1, 10, 20);
    pool.SubmitTask(sum1, 10, 20);
    pool.SubmitTask(sum1, 10, 20);
    pool.SubmitTask(sum1, 10, 20);
    pool.SubmitTask(sum1, 10, 20);
    pool.SubmitTask(sum1, 10, 20);
    // std::cout << r1.get() << std::endl /*std::flush*/;
    // std::cout << r2.get() << std::endl /*std::flush*/;
    // std::cout << r3.get() << std::endl /*std::flush*/;
    // std::cout << r4.get() << std::endl /*std::flush*/;
    // std::cout << r5.get() << std::endl /*std::flush*/;
    // std::cout << r6.get() << std::endl /*std::flush*/;
    // std::cout << r7.get() << std::endl /*std::flush*/;
    // std::cout << r8.get() << std::endl /*std::flush*/;
    // std::cout << r9.get() << std::endl /*std::flush*/;
    // std::cout << r10.get() << std::endl /*std::flush*/;
}

int main()
{
    // test1();
    // getchar();
    // test2();
    // getchar();
    test3();
    return 0;
}