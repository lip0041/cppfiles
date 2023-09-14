// g++ eventfd.cpp -o eventfd -lpthread
// eventfd: 进行线程之间的事件通知
// timerfd: 可实现定时器的功能
// signalfd: 可将信号抽象为一个文件描述符

#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <error.h>
#include <iostream>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define TIMEFD_VERSION
struct ThreadInfo {
    pthread_t threadId;
    int       rank;
    int       epollFd;
};
constexpr int32_t MAX_EVENTS_SIZE = 8;
constexpr int32_t NUM_CONSUMERS   = 4;
constexpr int32_t NUM_PRODUCERS   = 2;
static void*      ConsumerRoutine(void* data)
{
    ThreadInfo*  c = (ThreadInfo*)data;
    epoll_event* events;
    int          epollFd = c->epollFd;
    int          nfds    = -1;
    int          i       = -1;
    uint64_t     result;

    std::cout << "Greetings from [consumer-" << c->rank << "]\n";
    events = (epoll_event*)calloc(MAX_EVENTS_SIZE, sizeof(epoll_event));
    if (events == nullptr) {
        std::cerr << "calloc epoll events error\n";
    }

    for (;;) {
        nfds = epoll_wait(epollFd, events, MAX_EVENTS_SIZE, 1000);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].events & EPOLLIN) {
                std::cout << "[consumer-" << c->rank << "] got event from fd-" << events[i].data.fd << "\n";
                read(events[i].data.fd, &result, sizeof(uint64_t));
#ifdef EVENTFD_VERSION
                close(events[i].data.fd);
#endif
            }
        }
    }
}
static void* ProducerRoutine(void* data)
{
    ThreadInfo* p = (ThreadInfo*)data;
    epoll_event event;
    int         epollFd = p->epollFd;
    int         ret     = -1;

    std::cout << "Greetings from [producer-" << p->rank << "]\n";
#ifdef TIMEFD_VERSION
    int        tfd = -1;
    itimerspec its;
    its.it_value.tv_sec     = 1; // initial expiration
    its.it_value.tv_nsec    = 0;
    its.it_interval.tv_sec  = 3; // interval
    its.it_interval.tv_nsec = 0;

    tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (tfd == -1) {
        std::cerr << "timefd creae " << strerror(errno);
    }
    event.data.fd = tfd;
    event.events  = EPOLLIN | EPOLLET;
    ret           = epoll_ctl(epollFd, EPOLL_CTL_ADD, tfd, &event);
    if (ret != 0) {
        std::cerr << "epoll_ctl error\n";
    }
    ret = timerfd_settime(tfd, 0, &its, nullptr);
    if (ret != 0) {
        std::cerr << "timerfd settime\n";
    }
    return (void*)0;
#endif
#ifdef EVENTFD_VERSION
    int efd = -1;
    while (1) {
        sleep(1);
        efd = eventfd(1, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd == -1) {
            std::cerr << "eventfd creae " << strerror(errno);
        }
        event.data.fd = efd;

        event.events = EPOLLIN | EPOLLET;
        ret          = epoll_ctl(epollFd, EPOLL_CTL_ADD, efd, &event);
        if (ret != 0) {
            std::cerr << "epoll_ctl error\n";
        }
        write(efd, (void*)0xffffffff, sizeof(uint64_t));
    }
#endif
}

int main(int argc, char* argv[])
{
    ThreadInfo *plist = nullptr, *clist = nullptr;
    int         epollFd = -1;
    int         ret = -1, i = -1;
    epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd == -1) {
        std::cerr << "epoll_create1 error\n";
    }
    std::cout << "epollFd: " << epollFd << std::endl;
    // producers
    plist = (ThreadInfo*)calloc(NUM_PRODUCERS, sizeof(struct ThreadInfo));
    if (!plist) {
        std::cerr << "calloc error\n";
    }
    for (i = 0; i < NUM_PRODUCERS; ++i) {
        plist[i].rank    = i;
        plist[i].epollFd = epollFd;
        ret              = pthread_create(&plist[i].threadId, nullptr, ProducerRoutine, &plist[i]);
        if (ret != 0) {
            std::cerr << "pthread_create error\n";
        }
    }
    // consumers
    clist = (ThreadInfo*)calloc(NUM_CONSUMERS, sizeof(ThreadInfo));
    if (!plist) {
        std::cerr << "calloc error\n";
    }
    for (i = 0; i < NUM_CONSUMERS; ++i) {
        clist[i].rank    = i;
        clist[i].epollFd = epollFd;
        ret              = pthread_create(&clist[i].threadId, nullptr, ConsumerRoutine, &clist[i]);
        if (ret != 0) {
            std::cerr << "pthread_create error\n";
        }
    }
    // join and exit
    for (i = 0; i < NUM_PRODUCERS; ++i) {
        ret = pthread_join(plist[i].threadId, nullptr);
        if (ret != 0) {
            std::cerr << "pthread_join error\n";
        }
    }
    for (i = 0; i < NUM_CONSUMERS; ++i) {
        ret = pthread_join(clist[i].threadId, nullptr);
        if (ret != 0) {
            std::cerr << "pthread_join error\n";
        }
    }
    free(plist);
    free(clist);
    return EXIT_SUCCESS;
}