#include "room_workers.hpp"
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace iron::coop {
struct RoomWorkers::Impl {
    unsigned count;
    std::mutex mutex;
    std::condition_variable ready,done;
    bool stopping=false;
    size_t generation=0,remaining=0;
    std::atomic<size_t> cursor{0};
    std::span<std::function<void()>> jobs;
    std::exception_ptr failure;
    std::vector<std::thread> threads;
    explicit Impl(unsigned n):count(n){
        if(!n||n>16)throw std::invalid_argument("Workers must be between 1 and 16");
        if(n==1)return;
        try{for(unsigned i=0;i<n;++i)threads.emplace_back([this]{
            size_t seen=0;
            for(;;){
                std::unique_lock lock(mutex);
                ready.wait(lock,[&]{return stopping||generation!=seen;});
                if(stopping)return;
                seen=generation;auto batch=jobs;lock.unlock();
                // A failed room must not prevent other jobs from completing.
                for(size_t at;(at=cursor.fetch_add(1,std::memory_order_relaxed))<batch.size();){
                    try{batch[at]();}catch(...){std::lock_guard guard(mutex);if(!failure)failure=std::current_exception();}
                }
                lock.lock();if(--remaining==0)done.notify_one();
            }
        });}catch(...){stop();throw;}
    }
    void stop(){
        {std::lock_guard lock(mutex);stopping=true;}
        ready.notify_all();for(auto& t:threads)if(t.joinable())t.join();
    }
    ~Impl(){stop();}
};
RoomWorkers::RoomWorkers(unsigned n):impl(std::make_unique<Impl>(n)){}
RoomWorkers::~RoomWorkers()=default;
unsigned RoomWorkers::count()const{return impl->count;}
void RoomWorkers::run(std::span<std::function<void()>> jobs){
    if(jobs.size()>32)throw std::length_error("Maximum 32 room jobs per tick");
    if(jobs.empty())return;
    if(impl->count==1||jobs.size()==1){std::exception_ptr failure;for(auto& job:jobs)try{job();}catch(...){if(!failure)failure=std::current_exception();}if(failure)std::rethrow_exception(failure);return;}
    auto& p=*impl;std::unique_lock lock(p.mutex);p.jobs=jobs;p.failure=nullptr;p.cursor=0;p.remaining=p.threads.size();++p.generation;p.ready.notify_all();
    p.done.wait(lock,[&]{return p.remaining==0;});p.jobs={};if(p.failure)std::rethrow_exception(p.failure);
}
}
