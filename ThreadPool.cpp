#include "ThreadPool.h"
#include <unistd.h>
#include <stdlib.h>
#include <sstream>
#include <iostream>
#include <windows.h>
// writen by JasonWang 23/12
ThreadPool::ThreadPool(int MinThreadnum, int MaxThreadnum, int Taskmax)
{
    this->Taskmax = Taskmax;
    this->MinThreadnum = MinThreadnum;
    this->MaxThreadnum = MaxThreadnum;
    this->Workers.resize(MaxThreadnum);
    this->LiveThreadnum = 0;
    this->WorkingThreadnum = 0;
    this->CurrentTasknum = 0;
    this->Destroynum = 0;
    this->DestroyThreadPool = false;
    std::stringstream ss;
    std::string id;
    // create threads
    for (int i = 0; i < MinThreadnum; i++)
    {
        this->Workers[i] = std::thread(&WorkerFunction, this);
        auto threadId = Workers[i].native_handle();

        std::cout << "Worker thread " << threadId << " is created" << std::endl;
    }
    this->Manger = std::thread(&MangerFunction, this);
    auto threadId = Manger.native_handle();
    std::cout << "Manger thread " << threadId << " is created" << std::endl;
}

ThreadPool::~ThreadPool()
{
    printf("Destroyiny ThreadPool\n");
    DestroyThreadPool = true;
    TaskEmpty.notify_all();
    TaskFull.notify_all();
    if (Manger.joinable())
    {
        Manger.join();
    }
    for (auto &Worker : Workers)
    {
        if (Worker.joinable())
        {
            Worker.join();
        }
    }
    Workers.clear();
}

void ThreadWorkerFunction(ThreadPool *pool)
{
    int tempWorkingnum;
    Task *temptask = new Task;
    while (1)
    {
        pool->GetMutexPool().lock();
        while (pool->GetCurrentTasknum() == 0 && !pool->GetDestroyThreadPool())
        {
            /* Block worker threads */
            std::unique_lock<std::mutex> lock(pool->GetMutexPool());
            pool->GetCondition_TaskEmpty().wait(lock);

            /* Try to destroy worker threads */
            if (pool->GetDestroynum() > 0)
            {
                int destroy = pool->GetDestroynum();
                pool->SetDestroynum(--destroy);
                if (pool->GetLiveThreadnum() > pool->GetMinThreadnum())
                {
                    int live = pool->GetLiveThreadnum();
                    pool->SetLiveThreadnum(--live);
                    pool->GetMutexPool().unlock();
                    goto Exit;
                }
            }
        }
        /* If threadPool is destroyed then destroy exitsting worker threads*/
        if (pool->GetDestroyThreadPool())
        {
            pool->GetMutexPool().unlock();
            break;
        }

        temptask->funcptr = pool->getTaskQueue().front().funcptr;
        temptask->funcargs = pool->getTaskQueue().front().funcargs;
        pool->getTaskQueue().pop();
        pool->setCurrentTasknum(pool->getTaskQueue().size());
        pool->GetMutexPool().unlock();

        pool->GetMutexWoringnum().lock();
        tempWorkingnum = pool->GetWorkingThreadnum();
        pool->SetWorkingThreadnum(++tempWorkingnum);
        pool->GetMutexWoringnum().unlock();

        pool->GetMutexPool().lock();
        printf("thread %ld is excuting task\n", std::this_thread::get_id());
        temptask->funcptr(temptask->funcargs);
        pool->GetMutexPool().unlock();
        printf("thread %ld finshed a task ! \n", std::this_thread::get_id());
        pool->GetMutexWoringnum().lock();
        tempWorkingnum = pool->GetWorkingThreadnum();
        pool->SetWorkingThreadnum(--tempWorkingnum);
        pool->GetMutexWoringnum().unlock();
        /* Wake up "addTask" function that is blocked because the number of tasks is full */
        pool->GetCondition_TaskFull().notify_all();
    }
Exit:
    delete temptask;
    pool->WorkerExit();
}

void ThreadMangerFunction(ThreadPool *pool)
{
    std::unique_lock<std::mutex> lock(pool->GetMutexPool());
    std::stringstream ss;
    std::string id_str;
    while (!pool->GetDestroyThreadPool())
    {
        sleep(3);
        /*
        If the number of task is 0, then the manger thread neeeds to
        be blocked until there is a task to wake up
        */
        if (pool->GetCurrentTasknum() == 0)
        {
            pool->GetCondition_TaskEmpty().wait(lock);
            if (pool->GetDestroyThreadPool())
            {
                break;
            }
        }

        pool->GetMutexPool().lock();
        int CurrentTasknum = pool->GetCurrentTasknum();
        int liveThreadnum = pool->GetLiveThreadnum();
        pool->GetMutexPool().unlock();

        pool->GetMutexWoringnum().lock();
        int WorkingThreadnum = pool->GetWorkingThreadnum();
        pool->GetMutexWoringnum().unlock();
        int cnt = 0;
        // Need to create threads
        printf("Current Tasknum: %d,  liveThreadnum: %d\n", CurrentTasknum, liveThreadnum);
        if (liveThreadnum < CurrentTasknum && liveThreadnum < pool->GetMaxThreadnum() && !pool->GetDestroyThreadPool())
        {
            for (int i = 0; i < pool->GetWorkers().size() && cnt < pool->PerThreadnum; i++)
            {
                if (!pool->GetWorkers()[i].joinable() && pool->GetLiveThreadnum() < pool->GetMaxThreadnum())
                {
                    pool->GetWorkers()[i] = std::thread(&ThreadWorkerFunction, pool);
                    cnt++;
                    ss << pool->GetWorkers()[i].get_id();
                    id_str = ss.str();
                    printf("A Worker thread added whose threadID is %s.\n", id_str.c_str());
                }
            }
        }

        // Need to destroy threads
        printf("Working Threadnum: %d,  liveThreadnum: %d\n", WorkingThreadnum, liveThreadnum);
        if (WorkingThreadnum > 0 && WorkingThreadnum * 2 < liveThreadnum && liveThreadnum > pool->GetMinThreadnum() && !pool->GetDestroynum())
        {
            pool->GetMutexPool().lock();
            pool->SetDestroynum(pool->PerThreadnum);
            pool->GetMutexPool().unlock();
            for (int i = 0; i < pool->PerThreadnum; i++)
            {
                pool->GetCondition_TaskEmpty().notify_one();
            }
        }
    }
}

void ThreadPool::addTask(void (*functionptr)(void *), void *functionargs)
{
    std::unique_lock<std::mutex> lock(this->mutexpool);
    while (this->CurrentTasknum == this->Taskmax && !DestroyThreadPool)
    {
        TaskFull.wait(lock);
    }
    if (DestroyThreadPool)
    {
        return; // 如果线程池正在销毁，不再添加新任务
    }
    /*
    Use "emplace" to create a Task object in TaskQueue directly,instead of
    creating a temporary "Task" object used only once and then copying a copy
    of it into "TaskQueue"
    */
    this->TaskQueue.emplace(functionptr, functionargs);
    this->CurrentTasknum = TaskQueue.size();
    printf("A task is added !\n");

    /* Wake up manager and worker threads that are blocked because the number of tasks is empty */
    TaskEmpty.notify_one();
}

void ThreadPool::WorkerExit()
{
    auto CurrentID = std::this_thread::get_id();
    /*
    Create an "std::stringstream" object, and then insert the
    thread ID into the "std::stringstream" object to convert the
    thread ID to a string.
    */
    std::stringstream ss;
    ss << std::this_thread::get_id();
    std::string id_str = ss.str();
    for (auto it = Workers.begin(); it != Workers.end(); it++)
    {
        if (CurrentID == it->get_id())
        {
            it->join();
            printf("Thread %d destroyed !\n", id_str.c_str());
        }
    }
}

int ThreadPool::GetTaskmax()
{
    return this->Taskmax;
}

int ThreadPool::GetMaxThreadnum()
{
    return this->MaxThreadnum;
}

int ThreadPool::GetMinThreadnum()
{
    return this->MinThreadnum;
}

void ThreadPool::SetLiveThreadnum(int liveThreadnum)
{
    this->LiveThreadnum = liveThreadnum;
}

int ThreadPool::GetLiveThreadnum()
{
    return this->LiveThreadnum;
}

void ThreadPool::SetWorkingThreadnum(int workingThreadnum)
{
    this->WorkingThreadnum = workingThreadnum;
}

int ThreadPool::GetWorkingThreadnum()
{
    return this->WorkingThreadnum;
}

int ThreadPool::GetCurrentTasknum()
{
    return this->CurrentTasknum;
}

void ThreadPool::setCurrentTasknum(int CunrrentTasknum)
{
    this->CurrentTasknum = CunrrentTasknum;
}

void ThreadPool::SetDestroynum(int Destroynum)
{
    this->Destroynum = Destroynum;
}

int ThreadPool::GetDestroynum()
{
    return this->Destroynum;
}

void ThreadPool::SetDestroyThreadPool(bool isDestroy)
{
    this->DestroyThreadPool = isDestroy;
}

bool ThreadPool::GetDestroyThreadPool()
{
    return this->DestroyThreadPool;
}

std::vector<std::thread> &ThreadPool::GetWorkers()
{
    return this->Workers;
}

std::queue<Task> &ThreadPool::getTaskQueue()
{
    return this->TaskQueue;
}

std::mutex &ThreadPool::GetMutexPool()
{
    return this->mutexpool;
}

std::mutex &ThreadPool::GetMutexWoringnum()
{
    return this->mutexWorkingnum;
}

std::condition_variable &ThreadPool::GetCondition_TaskFull()
{
    return this->TaskFull;
}

std::condition_variable &ThreadPool::GetCondition_TaskEmpty()
{
    return this->TaskEmpty;
}