#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    SpinLock()                           = default;
    SpinLock(const SpinLock&)            = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    void      lock()
    {
        bool expected = false;
        while (flag.test_and_set()) {
        }
    }
    void unlock() { flag.clear(); }
};



// class SpinLock {
//     std::atomic_bool flag = ATOMIC_VAR_INIT(false);
// public:
//     SpinLock() = default;
//     SpinLock(const SpinLock&) = delete;
//     SpinLock& operator=(const SpinLock&) = delete;
//     void lock() {
//         bool expected = false;
//         while (!flag.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
//             expected = false;
//         }
//     }
//     void unlock() {
//         flag.store(false, std::memory_order_release);
//     }
// };

int      num = 0;
SpinLock mutex;
std::mutex mut;

void func1()
{
    // mutex.lock();
    std::lock_guard<SpinLock> lock(mutex);
    // std::lock_guard<std::mutex> lock(mut);
    num += 2;
    std::cout << "func1: " << num << std::endl << std::flush;
    // mutex.unlock();
}

void func2()
{
    // mutex.lock();
    std::lock_guard<SpinLock> lock(mutex);
    // std::lock_guard<std::mutex> lock(mut);
    num -= 1;
    std::cout << "func2: " << num << std::endl << std::flush;
    // mutex.unlock();
}

int main()
{
    std::thread t1(func1);
    std::thread t2(func2);
    t1.join();
    t2.join();
    // std::lock_guard<SpinLock> lock(mutex);
    std::cout << "main: " << num << std::endl << std::flush;
    // func1 和 func2 只会有两种情况
    // -1， 1
    // 2， 1
    return 0;
}