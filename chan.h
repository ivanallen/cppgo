// Copyright 2018 authors. All Rights Reserved.
// Author: allen(ivan_allen@qq.com)
//
// channel
#pragma once

#include <unistd.h>
#include <mutex>
#include <queue>
#include <iostream>
#include <condition_variable>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

template<typename T, size_t N = 0>
class Chan_ {
public:
	Chan_() {
        // EFD_SEMAPHORE (since Linux 2.6.30)
		_fd_empty = eventfd(N, EFD_SEMAPHORE);
		_fd_full = eventfd(0, EFD_SEMAPHORE);
        _closed = false;
	}
    ~Chan_() {
		std::lock_guard<std::mutex> lk(_mu);
        ::close(_fd_full);
        ::close(_fd_empty);
        _closed = true;
    }

    // write
	bool operator<<(const T& data) {
        uint64_t r = 0;
        uint64_t w = 1;
		std::unique_lock<std::mutex> lk(_mu);
		_cv_empty.wait(lk, [this]{return this->_q.size() < N;});
        if (_closed) {
            return false;
        }
        // update fd
        int ret = read(_fd_empty, &r, sizeof(uint64_t));
        if (ret < 0) {
            return false;
        }
        write(_fd_full, &w, sizeof(uint64_t));

		_q.push(data);

		_cv_full.notify_all();
		return true;
	}

    // read
	bool operator>>(T& data) {
        uint64_t r = 0;
        uint64_t w = 1;
		std::unique_lock<std::mutex> lk(_mu);
		_cv_full.wait(lk, [this]{return this->_q.size() > 0;});
        if (_closed) {
            return false;
        }
        // update fd
        int ret = read(_fd_full, &r, sizeof(uint64_t));
        if (ret < 0) {
            return false;
        }
        write(_fd_empty, &w, sizeof(uint64_t));

		data = _q.front();
		_q.pop();

		_cv_empty.notify_all();
		return true;
	}

    operator int() {
        return rfd();
    }

    int rfd() {
        return _fd_full;
    }

    int wfd() {
        return _fd_empty;
    }

	void close() {
        uint64_t u = 1;
		std::lock_guard<std::mutex> lk(_mu);
        _closed = true;
        // std::cout << "close:" << _fd_full << std::endl;
        // std::cout << "close:" << _fd_empty << std::endl;
        write(_fd_full, &u, sizeof(uint64_t));
        write(_fd_empty, &u, sizeof(uint64_t));
		_cv_empty.notify_all();
		_cv_full.notify_all();
	}

private:
	std::mutex _mu;
	std::condition_variable _cv_full;
	std::condition_variable _cv_empty;
	std::queue<T> _q;
	int _fd_full;
	int _fd_empty;
    bool _closed;
};

template<typename T>
class Chan_<T, 0> {
public:
    Chan_() {
        pipe(_fds); 
        //std::cout << "pipe:" << _fds[0] << "," << _fds[1]<< std::endl;
    }
	bool operator<<(const T& data) {
		std::unique_lock<std::mutex> lk(_mu);
		_cv_empty.wait(lk, [this]{return !this->_has_value;});
        int ret = write(_fds[1], "1", 1);
        if (ret < 0) {
            perror("write");
            return false;
        }
		_data = data;
		_has_value = true;
		_cv_full.notify_all();
		return true;
	}
	bool operator>>(T& data) {
        char c;
		std::unique_lock<std::mutex> lk(_mu);
		_cv_full.wait(lk, [this]{return this->_has_value;});
        int ret = read(_fds[0], &c, 1);
        if (ret = 0) {
            return false;
        }
		data = _data;
		_has_value = false;
		_cv_empty.notify_all();
		return true;
	}

    operator int() {
        return rfd();
    }

    int rfd() {
        return _fds[0];
    }

    int wfd() {
        return _fds[1];
    }

    void close() {
		std::lock_guard<std::mutex> lk(_mu);
        // std::cout << "close:" << _fds[0] << std::endl;
        // std::cout << "close:" << _fds[1] << std::endl;
        ::close(_fds[0]);
        ::close(_fds[1]);
		_cv_empty.notify_all();
		_cv_full.notify_all();
    }

private:
	std::mutex _mu;
	std::condition_variable _cv_full;
	std::condition_variable _cv_empty;
	T _data;
	bool _has_value;
	int _fds[2];
};

template<typename T, size_t N=0>
using Chan = std::shared_ptr<Chan_<T, N>>;

template<typename T, size_t N = 0>
Chan<T, N> make_chan() {
	return std::make_shared<Chan_<T, N>>();
}

/*
template<typename T>
Chan<T, 0> make_chan() {
	return std::make_shared<Chan_<T, 0>>();
}
*/

std::vector<bool> choose(const std::vector<int>& fds) {
    std::unique_ptr<struct pollfd[]> pfds(new struct pollfd[fds.size()]);
    for (int i = 0; i < fds.size(); ++i) {
        // std::cout << "fd = " << fds[i] << std::endl;
        pfds[i].fd = fds[i];
        pfds[i].events = POLLIN | POLLERR;
        pfds[i].revents = 0;
    }

    int ret = poll(pfds.get(), fds.size(), -1);
    if (ret < 0) {
        perror("poll");
    } else if (ret == 0) {
        std::cout << "timeout" << std::endl;
    }

    std::vector<bool> res(fds.size(), false);
    for (int i = 0; i < fds.size(); ++i) {
        // std::cout << "revents:" << pfds[i].revents << std::endl;
        if (pfds[i].revents & POLLIN || pfds[i].revents & POLLNVAL) {
            res[i] = true;
        }
    }
    return res;

    /*
    std::map<int, int> m;
    int epfd = epoll_create(1);
    std::unique_ptr<struct epoll_event[]> evts(new struct epoll_event[fds.size()]);
    struct epoll_event ev;
    for(int i = 0; i < fds.size(); ++i) {
        m.emplace(fds[i], i);
        std::cout << "listen:" << fds[i] << std::endl;
        ev.data.fd = fds[i];
        ev.events = EPOLLIN | EPOLLRDHUP;
        int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i], &ev);
        if (ret < 0) {
            perror("epoll_ctl");
        }
    }

    std::vector<bool> res(fds.size(), false);

    int nready = epoll_wait(epfd, evts.get(), fds.size(), -1);
    for(int i = 0; i < nready; ++i) {
        res[m[evts[i].data.fd]] = true;    
    }

    return res;
    */
}
