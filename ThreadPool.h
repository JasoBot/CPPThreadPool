#include <thread>
#include <queue>
#include <condition_variable>
#include <unistd.h>
#include <stdlib.h>
#include <sstream>
#include <iostream>
#include <windows.h>
using callback=void (*)(void* args);

template <class T>
class Task
{
public:
    callback function;
    T* funcargs;

public:
    Task() {
        function=nullptr;
        funcargs=nullptr;
    }
    Task(void (*funcptr)(void *), void *funcargs) : function(funcptr), funcargs((T*)funcargs) {}
    ~Task() {}
};

template <class T>
class ThreadPool
{
private:
    std::queue<Task<T>> TaskQueue;
    int CurrentTasknum; // Current task count
    int Taskmax;        // Maximum task number

    std::vector<std::thread> Workers; // Worker threads
    std::thread Manger;               // Manger thread
    int MaxThreadnum;
    int MinThreadnum;
    int LiveThreadnum;
    int WorkingThreadnum;
    int Destroynum;
    bool DestroyThreadPool;

    std::mutex mutexpool;
    std::mutex mutexWorkingnum;
    std::condition_variable TaskFull;  // Block the producer, whether the task queue is full
    std::condition_variable TaskEmpty; // Blocked consumer, task queue is empty

    std::mutex completion_mutex;
    std::condition_variable completion_cond;
    void WorkerFunction();
    void MangerFunction();

public:
    const int PerThreadnum = 2; // The number of threads created\destroyed each time
    ThreadPool(int MinThreadnum, int MaxThreadnum, int Taskmax);

    void addTask(void (*functionptr)(void *), void *functionargs);

    ~ThreadPool();
};

template <class T>
ThreadPool<T>::ThreadPool(int MinThreadnum, int MaxThreadnum, int Taskmax)
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
        this->Workers[i] = std::thread(WorkerFunction, this);
        auto threadId = Workers[i].native_handle();
        std::cout << "Worker thread " << threadId << " is created" << std::endl;
    }
    this->Manger = std::thread(MangerFunction, this);
    auto threadId = Manger.native_handle();
    std::cout << "Manger thread " << threadId << " is created" << std::endl;
}
template <class T>
ThreadPool<T>::~ThreadPool()
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
template <class T>
void ThreadPool<T>::WorkerFunction()
{
    int tempWorkingnum;
    Task<T> task;
    while (1)
    {
        //{
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
        task = std::move(TaskQueue.front()); // get task
        TaskQueue.pop();
        CurrentTasknum = TaskQueue.size();
       // }
         // release lock
        {
            std::lock_guard<std::mutex> lock(this->mutexWorkingnum);
            WorkingThreadnum++;
        }
        // execute task
        task.function(task.funcargs);
        {
            std::lock_guard<std::mutex> lock(this->mutexWorkingnum);
            WorkingThreadnum--;
        }
        /*
        funcargs is of type void*, and freeing memory using delete may 
        cause memory space not to be fully freed

        Because when delete is used to free the memory space pointed to by 
        a void* pointer, the main problem is that the compiler cannot determine 
        how to free the memory properly. This is especially important when the 
        memory is actually occupied by an instance of a class.

        1. Destructors are not called: In C++, deleting an object not only frees 
        memory, but also calls the object's destructor. With void* Pointers, the 
        compiler has no way of knowing the exact type to point to and therefore 
        cannot call the appropriate destructor. If the object manages other resources 
        (such as dynamically allocated memory, file handles, etc.), these resources 
        may not be released properly, resulting in resource leaks

        2, type information loss: Since void* is a generic pointer, it does not 
        carry any information about the type of the object. Therefore, when using 
        delete to free the memory pointed to by void*, the compiler cannot determine 
        which destructor to call or whether it should be called at all

        3. Can't check for type matches: Since void* is a generic pointer type, 
        the compiler can't check if the memory that delete uses to free is really 
        allocated by new. Using mismatched new and delete is undefined behavior 
        that can result in memory corruption or program crash

        Therefore templates should be used
        */
        delete task.funcargs;
        task.funcargs=nullptr;
        TaskFull.notify_one();
    }

Exit:
    std::stringstream ss;
    std::string id_str;
    ss << std::this_thread::get_id();
    id_str = ss.str();
    std::cout << "Worker Thread " << id_str << " exited !" << std::endl;
}

template <class T>
void ThreadPool<T>::MangerFunction()
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
template <class T>
void ThreadPool<T>::addTask(void (*functionptr)(void *), void *functionargs)
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