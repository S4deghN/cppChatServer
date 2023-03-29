#pragma once
#include <deque>
#include <mutex>

template <typename T>
class safe_deque {
    std::deque<T> deque;
    std::mutex mtx;

public:
    safe_deque() = default;
    safe_deque(safe_deque<T> const&) = delete;

    T const& front() {
        std::scoped_lock lock(mtx);
        return deque.front();
    }

    T const& back() {
        std::scoped_lock lock(mtx);
        return deque.back();
    }

    void push_back(T const& item) {
        std::scoped_lock lock(mtx);
        deque.push_back(item);
    }

    void push_front(T const& item) {
        std::scoped_lock lock(mtx);
        deque.push_front(item);
    }

    void pop_front() {
        std::scoped_lock lock(mtx);
        deque.pop_front();
    }

    void pop_back() {
        std::scoped_lock lock(mtx);
        deque.pop_back();
    }

    size_t size() {
        std::scoped_lock lock(mtx);
        return deque.size();
    }
};
