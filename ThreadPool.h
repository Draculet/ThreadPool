#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <unistd.h>
#include <thread>
#include <vector>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <mutex>
#include <functional>
#include <atomic>
#include <assert.h>
#include <map>

class ThreadLoop{
    public:
    ThreadLoop(){
        wakeupfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        epollfd = epoll_create1(0);
        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = wakeupfd;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, wakeupfd, &ev);
    }
    ~ThreadLoop(){
        Stop();
        t->join();
        close(wakeupfd);
        close(epollfd);
    }

    void Start(){
        t = new std::thread([this]{
            int eventsize = 20;
            epoll_event ev, events[eventsize];
            while (!quit){
                int nfds = epoll_wait(epollfd, events, eventsize, -1);
                if (quit) break;
                if (nfds < 0) {
                    quit = true;
                    return;
                } else if (nfds == 0) {
                    continue;
                }
                for (int n = 0; n < nfds; n++){
                    if (events[n].data.fd == wakeupfd){
                        uint64_t n = 0;
                        int ret = read(wakeupfd, &n, 8);
                        assert(ret == 8);
                    } else {
                        assert(events[n].data.fd == wakeupfd);
                    }
                }
                doTask();
            }
        });
        //t->detach();
    }

    void doTask(){
        std::vector<std::function<void()>> tmp;
        {
            std::unique_lock<std::mutex> lk(m);
            tmp.swap(tasks);
        }
        for (auto task : tmp){
            task();
        }
    }

    //支持跨线程调用
    void InsertTask(std::function<void()> task){
        {
            std::unique_lock<std::mutex> lk(m);
            tasks.push_back(task);
        }
        wakeup();
    }

    void wakeup(){
        uint64_t n = 6;
        int total = 0;
        int ret = 0;
        char *ptr = (char *)&n;
        while (total != 8){
            ret = write(wakeupfd, ptr + total, 8 - total);
            total += ret;
        }
    }

    void Stop(){
        quit = true;
        wakeup();
    }

    private:
    std::atomic<bool> quit = {false};
    int wakeupfd;
    std::mutex m;
    std::thread *t;
    std::vector<std::function<void()>> tasks;
    int epollfd;
};

class ThreadPool{
    public:
    ThreadPool(int threadnums = 8)
        :threadnum(threadnums){
        for (int i = 0; i < threadnum; i++){
            threads.push_back(new ThreadLoop());
        }
    }

    void Start(){
        for (auto loop: threads)
            loop->Start();
    }

    ~ThreadPool(){
        for (auto loop : threads){
            delete loop;
        }
        threads.clear();
        groups.clear();
    }

    void push_task(std::function<void()> task, std::string flag = ""){
        if (flag.size() == 0){
            ThreadLoop *loop = threads[index];
            index = (index + 1) % threadnum;
            loop->InsertTask(task);
        } else {
            if (groups.find(flag) != groups.end()){
                ThreadLoop *loop = groups[flag];
                loop->InsertTask(task);
            } else {
                ThreadLoop *loop = threads[index];
                index = (index + 1) % threadnum;
                loop->InsertTask(task);
                groups[flag] = loop;
            }
        }
    }

    private:
    int threadnum;
    int index = 0;
    std::vector<ThreadLoop *> threads;
    std::map<std::string, ThreadLoop *> groups;
};

#endif