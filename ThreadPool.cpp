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

void ThreadPool::WorkerFunction()
{
    int tempWorkingnum;
    Task task;
    while (1)
    {
        {
            std::unique_lock<std::mutex> lock(this->mutexpool);
            while (CurrentTasknum == 0 && !DestroyThreadPool)
            {
                TaskEmpty.wait(lock);
                if (Destroynum > 0)
                {
                    --Destroynum;
                    if (LiveThreadnum > MinThreadnum)
                    {
                        --LiveThreadnum;
                        goto Exit;
                    }
                }
            }
            if (DestroyThreadPool)
            {
                break;
            }
            task = std::move(TaskQueue.front());
            TaskQueue.pop();
            CurrentTasknum = TaskQueue.size();
        } // release lock
        {
            std::lock_guard<std::mutex> lock(this->mutexWorkingnum);
            WorkingThreadnum++;
        }
        // execute task
        task.funcptr(task.funcargs);
        {
            std::lock_guard<std::mutex> lock(this->mutexWorkingnum);
            WorkingThreadnum--;
        }
        TaskFull.notify_one();
    }

Exit:
    std::stringstream ss;
    std::string id_str;
    ss << std::this_thread::get_id();
    id_str = ss.str();
    std::cout << "Worker Thread " << id_str << " exited !" << std::endl;
}

void ThreadPool::MangerFunction()
{
    std::stringstream ss;
    std::string id_str;
    int cnt;
    while (!DestroyThreadPool)
    {
        Sleep(30);
        /*
        If the number of task is 0, then the manger thread neeeds to
        be blocked until there is a task to wake up
        */

        std::unique_lock<std::mutex> lock(mutexpool);
        int taskCount = CurrentTasknum;
        int liveThreadCount = LiveThreadnum;
        int workingThreadCount = WorkingThreadnum;
        lock.unlock();

        cnt = 0;
        // Need to create threads
        if (LiveThreadnum < CurrentTasknum && LiveThreadnum < MaxThreadnum && !DestroyThreadPool)
        {
            for (int i = 0; i < Workers.size() && cnt < PerThreadnum; i++)
            {
                if (!Workers[i].joinable() && LiveThreadnum < MaxThreadnum)
                {
                    Workers[i] = std::thread(WorkerFunction, this);
                    auto threadID = Workers[i].native_handle();
                    std::cout << "Worker thread " << threadID << " is added !" << std::endl;
                    cnt++;
                }
            }
        }
        // Need to destroy threads
        if (WorkingThreadnum > 0 && WorkingThreadnum * 2 < LiveThreadnum && LiveThreadnum > MinThreadnum && !Destroynum)
        {
            mutexpool.lock();
            Destroynum = PerThreadnum;
            mutexpool.unlock();
            for (int i = 0; i < PerThreadnum; i++)
            {
                TaskEmpty.notify_one();
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
    // If the thread pool is being destroyed, no new tasks are added
    if (DestroyThreadPool)
    {
        return; 
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