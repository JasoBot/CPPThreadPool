#include "ThreadPool.h"
#include <sstream>
#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include <windows.h>
void task(void *args)
{
    int arg = *(int *)args;
    std::stringstream ss;
    ss << std::this_thread::get_id();
    std::string id = ss.str();
    printf("thread %s is do something , num=%d \n", id.c_str(), arg);
}

int main()
{
    ThreadPool *pool = new ThreadPool(3, 10, 100);
    for (int i = 0; i < 100; i++)
    {
        int *num = new int(i + 100); 
        Sleep(30);
        pool->addTask(task, num);
    }
    sleep(10);
    delete pool;
    printf("ThreadPool shutdown safely\n");
    return 0;
}