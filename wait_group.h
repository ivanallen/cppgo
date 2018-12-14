// Copyright 2018 authors. All Rights Reserved.
// Author: allen(ivan_allen@qq.com)
//
// wait group 
#pragma once

#include <mutex>
#include <condition_variable>

class WaitGroup {
public:
    WaitGroup() : _count(0) {}
    WaitGroup(const WaitGroup&) = delete;
    WaitGroup& operator=(const WaitGroup&) = delete;
    WaitGroup& operator=(WaitGroup&&) = delete;

    void wait() {
        std::unique_lock<std::mutex> lock(_mutex);
        while(_count > 0) {
            _cond.wait(lock);
        }
    }

    void add(int delta) {
        std::lock_guard<std::mutex> lock(_mutex);
        _count += delta;
    }

    void done() {
        std::lock_guard<std::mutex> lock(_mutex);
        --_count;
        if (_count <= 0) {
            _cond.notify_all();
        }
    }

private:
    int _count;
    std::mutex _mutex;
    std::condition_variable _cond;
};
