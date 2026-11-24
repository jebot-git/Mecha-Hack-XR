#pragma once
#include <functional>
#include <memory>
#include <span>
namespace iron::coop {
// A synchronous, bounded batch. No room job survives the tick barrier.
class RoomWorkers {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit RoomWorkers(unsigned count=1);
    ~RoomWorkers();
    RoomWorkers(const RoomWorkers&)=delete;
    unsigned count() const;
    void run(std::span<std::function<void()>> jobs);
};
}
