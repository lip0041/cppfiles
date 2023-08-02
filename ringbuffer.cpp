#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

template <class T> class RingBuffer {
public:
    RingBuffer(unsigned size) : m_size(size), m_front(0), m_rear(0) { m_data = new T[size]; }

    ~RingBuffer()
    {
        if (m_data != nullptr) {
            delete[] m_data;
            m_data = nullptr;
        }
    }

    inline bool isEmpty() const { return m_front == m_rear; }

    inline bool isFull() const { return m_front == (m_rear + 1) % m_size; }

    bool push(T& val)
    {
        if (isFull()) {
            return false;
        }
        // m_data[m_rear] = std::move(val);
        m_data[m_rear] = val;
        m_rear         = (m_rear + 1) % m_size;
        return true;
    }

    inline bool pop(T& value)
    {
        if (isEmpty()) {
            return false;
        }
        // value   = std::move(m_data[m_front]);

        value   = m_data[m_front];
        m_front = (m_front + 1) % m_size;
        return true;
    }

    inline unsigned int front() const { return m_front; }

    inline unsigned int rear() const { return m_rear; }

    inline unsigned int size() const { return m_size; }

private:
    unsigned int m_size;
    int          m_front;
    int          m_rear;
    T*           m_data;
};

class Test {
public:
    Test(int id = 0, int value = 0)
    {

        // printf("+++++++\n");
        this->id    = id;
        this->value = value;
        data        = new char[128];
        sprintf(data, "id = %d, value = %d\n", this->id, this->value);
    }
    ~Test()
    {
        // printf("------\n");
        if (data != nullptr) {
            delete [] data;
            data = nullptr;
        }
    }
    void     display() { printf("%s", data); }
    Test& operator=(Test&& rhs)
    {
        // printf("=======\n");
        this->id    = rhs.id;
        this->value = rhs.value;
        this->data  = rhs.data;
        rhs.data = nullptr;
        return *this;
    }

    Test& operator=(const Test& rhs)
    {
        // printf("=======\n");
        this->id    = rhs.id;
        this->value = rhs.value;
        strcpy(this->data, rhs.data);
        return *this;
    }

    // Test(Test&& t)
    // {
    //     data   = t.data;
    //     id     = t.id;
    //     value  = t.value;
    //     t.data = nullptr;
    // }

private:
    int   id;
    int   value;
    char* data = nullptr;
    // char data[128];
};

double getDeltaTimeofDay(struct timeval* begin, struct timeval* end)
{
    return (end->tv_sec + end->tv_usec * 1.0 / 1000000) - (begin->tv_sec + begin->tv_usec * 1.0 / 1000000);
}

RingBuffer<Test> queue(1 << 12);
#define N (10 * (1 << 20))
void produce()
{
    struct timeval begin, end;
    gettimeofday(&begin, nullptr);
    unsigned int i = 0;
    // printf("[][][][][]\n");
    while (i < N) {
        Test test(i % 1024, i);
        if (queue.push(test)) {
            ++i;
        }
        // if (queue.push(Test(i % 1024, i))) {
        //     ++i;
        // }
    }
    gettimeofday(&end, nullptr);
    double tm = getDeltaTimeofDay(&begin, &end);
    printf("producer tid = %lu, %lf MB/s, %f msg/sm elapsed = %f size = %u\n", std::this_thread::get_id(),
           N * (sizeof(Test) + 128) * 1.0 / (tm * 1024 * 1024), N * 1.0 / tm, tm, i);
}

void consume()
{
    sleep(1);
    Test           test;
    struct timeval begin, end;
    gettimeofday(&begin, nullptr);
    unsigned int i = 0;
    while (i < N) {
        if (queue.pop(test)) {
            // test.display();
            ++i;
        }
    }
    gettimeofday(&end, nullptr);
    double tm = getDeltaTimeofDay(&begin, &end);
    printf("consumer tid = %lu, %lf MB/s, %f msg/sm elapsed = %f size = %u\n", std::this_thread::get_id(),
           N * (sizeof(Test) + 128) * 1.0 / (tm * 1024 * 1024), N * 1.0 / tm, tm, i);
}

int main()
{
    std::thread prod(produce);
    std::thread cons(consume);
    prod.join();
    cons.join();
    return 0;
}