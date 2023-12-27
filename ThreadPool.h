#include <thread>
#include <queue>
#include <condition_variable>

class Task
{
public:
    void (*funcptr)(void *);
    void *funcargs;

public:
    Task() {}
    Task(void (*funcptr)(void *), void *funcargs) : funcptr(funcptr), funcargs(funcargs) {}
    ~Task() {}
};

class ThreadPool
{
private:
    std::queue<Task> TaskQueue;
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

public:
    const int PerThreadnum = 2; // The number of threads created\destroyed each time
    ThreadPool(int MinThreadnum, int MaxThreadnum, int Taskmax);

    void addTask(void (*functionptr)(void *), void *functionargs);
    void WorkerFunction();
    void MangerFunction();

    ~ThreadPool();
};
