#include "Thread.h"
#include "CurrentThread.h"

#include <semaphore.h>

// 静态成员变量定义：记录已创建线程总数（原子操作，线程安全）
std::atomic_int Thread::numCreated_(0);

Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false)
    , joined_(false)
    , thread_(nullptr)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    if(started_ && !joined_)
    {
        // 如果线程已启动但没有被 join，就设为 detach（分离状态）
        // 分离后线程结束时资源自动释放，不需要其他线程 join 它
        thread_->detach();
    }
}

// 启动线程
void Thread::start()
{
    started_ = true;

    // 1. 初始化信号量，初始值为 0

    // 使用 POSIX 信号量实现启动同步：
    // 保证 start() 返回时新线程已经开始执行（tid_ 已经拿到）
    sem_t sem;
    sem_init(&sem, false, 0);  // 初始值为 0，表示"尚未就绪"

    // 创建新线程
    thread_ = std::shared_ptr<std::thread>(new std::thread([&]() {
        // 新线程：获取自己的 TID
        tid_ = CurrentThread::tid();
        // 通知主线程：我已经启动了      获取完之后，给信号量 +1，通知主线程
        sem_post(&sem);
        // 执行用户指定的线程函数
        func_();
    }));

    // 2. 主线程阻塞在这里，等待信号量大于 0

    // 主线程：等待新线程启动完成
    // 这确保了 start() 返回后 tid() 返回的是有效值
    sem_wait(&sem);
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

// 自动命名：Thread1, Thread2, Thread3...
void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if(name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf, sizeof(buf), "Thread%d", num);
        name_ = buf;
    }
}
