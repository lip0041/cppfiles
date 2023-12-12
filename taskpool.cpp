#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std;

int32_t OnEvent(std::string& event)
{
    stringstream ss;
    ss << this_thread::get_id();
    std::string msg = "***thread id " + ss.str() + ", event come:" + event;
    std::cout << msg << endl << flush;
    int sum = 0;
    // 注意这段代码可能会被某些默认是O2的编译脚本优化掉，要去掉优化
    for (int i = 0; i < 1e9; ++i) {
        sum += i;
    }
    return 0;
}

class TaskPool {
public:
    using Task      = int32_t(const std::string&);
    using BoundTask = int32_t();
    TaskPool();
    ~TaskPool();
    virtual void    PushTask(std::packaged_task<BoundTask>& task);
    virtual void    Stop();
    virtual int32_t Start(int32_t threadNum);

protected:
    virtual void TaskMainWorker();

    bool                                      isRunning_ = false;
    std::mutex                                taskMutex_;
    std::vector<std::thread>                  threads_;
    std::condition_variable                   hasTask_;
    std::condition_variable                   acceptNewTask_;
    std::chrono::microseconds                 timeoutInterval_;
    std::deque<std::packaged_task<BoundTask>> tasks_;
};
TaskPool::TaskPool()
{
    cout << "task pool ctor\n" << flush;
}
TaskPool::~TaskPool()
{
    if (isRunning_) {
        Stop();
    }
    cout << "task pool dtor\n" << flush;
}

int32_t TaskPool::Start(int32_t threadNum)
{
    if (!threads_.empty()) {
        return -1;
    }
    isRunning_ = true;
    threads_.reserve(threadNum);
    for (int i = 0; i < threadNum; ++i) {
        threads_.push_back(std::thread(&TaskPool::TaskMainWorker, this));
#ifdef __linux__
        auto name = "thread" + std::to_string(i);
        pthread_setname_np(threads_.back().native_handle(), name.c_str());
#endif
    }
    return 0;
}

void TaskPool::Stop()
{
    {
        std::unique_lock<std::mutex> lock(taskMutex_);
        isRunning_ = false;
        hasTask_.notify_all();
    }
    for (auto& t : threads_) {
        t.join();
    }
}

void TaskPool::PushTask(std::packaged_task<BoundTask>& task)
{
    if (threads_.empty()) {
    } else {
        std::unique_lock<std::mutex> lock(taskMutex_);
        while (tasks_.size() >= 10) {
            cout << "task pool overload\n" << flush;
            acceptNewTask_.wait(lock);
        }
        tasks_.emplace_back(std::move(task));
        hasTask_.notify_one();
    }
}

void TaskPool::TaskMainWorker()
{
    while (isRunning_) {
        std::unique_lock<std::mutex> lock(taskMutex_);
        if (tasks_.empty() && isRunning_) {
            hasTask_.wait(lock);
        } else {
            std::packaged_task<BoundTask> task = std::move(tasks_.front());
            tasks_.pop_front();
            stringstream ss1;
            ss1 << this_thread::get_id();
            std::string msg1 = "***thread id " + ss1.str() + ", pop task\n";
            std::cout << msg1 << flush;
            acceptNewTask_.notify_one();
            lock.unlock();
            stringstream ss;
            ss << this_thread::get_id();
            std::string msg = "***thread id " + ss.str() + ", exec task\n";
            std::cout << msg << flush;
            task();
        }
    }
}
class EventManager : public TaskPool {
public:
    EventManager() { cout << "event manager ctor\n" << flush; }
    ~EventManager() { cout << "event manager dtor\n" << flush; }
    void StopEventLoop()
    {
        Stop();
        eventThread_.join();
    }
    int32_t StartEventLoop()
    {
        int ret = Start(10);
        if (ret != 0) {
            return ret;
        }
        eventThread_ = std::thread(&EventManager::ProcessEvent, this);
#ifdef __linux__
        pthread_setname_np(eventThread_.native_handle(), "eventloop");
#endif
        return 0;
    }
    int32_t PushEvent(const std::string& event)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        events_.emplace(event);
        hasEvent_.notify_one();
        return 0;
    }

private:
    void ProcessEvent()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (isRunning_) {
            if (events_.empty()) {
                hasEvent_.wait(lock);
            } else {
                auto event = events_.front();
                events_.pop();
                std::packaged_task<BoundTask> task(std::bind(&OnEvent, event));
                // auto future = task.get_future(); // sync mode
                // std::cout << "==========push event: " << event << endl << flush;
                PushTask(task);
                // sync mode
                // if (future.wait_for(std::chrono::milliseconds(1000 * 5)) == std::future_status::ready) {
                //     std::cout << "=========future: " << future.get() << endl << flush;
                // } else {

                //     std::cout << "=========future: fail " << endl << flush;
                // }
            }
        }
    }

private:
    std::mutex                   mutex_;
    std::thread                  eventThread_;
    std::condition_variable      hasEvent_;
    std::queue<std::string>      events_;
    std::unique_ptr<std::thread> eventLoop_ = nullptr;
};

void Test()
{
    EventManager* emgr = new EventManager();
    emgr->StartEventLoop();

    for (int i = 0; i < 100; ++i) {
        emgr->PushEvent(std::to_string(i));
    }
}
int main()
{
    cout << "******enter for start\n" << flush;
    cin.get();
    std::thread t(&Test);
#ifdef __linux__
    pthread_setname_np(t.native_handle(), "test");
#endif
    if (t.joinable()) {
        t.join();
    }
    cout << "******run done, entor for exit\n" << flush;
    cin.get();
    // vector<uint8_t> vec{1, 2, 3};
    // const char* p = reinterpret_cast<const char*>(vec.data());
    // cout << *p;
    // cout << true;
    return 0;
}
