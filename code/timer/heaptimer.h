/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */
#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h>
#include <functional>
#include <assert.h>
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

// 定时器结点
struct TimerNode {
    int id;     // 结点id
    TimeStamp expires;      // 有效时间
    TimeoutCallBack cb; // 回调函数

    bool operator<(const TimerNode &t) {
        return expires < t.expires;
    }
};

class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }

    ~HeapTimer() { clear(); }

    // 重新调整结点id的有效时间
    void adjust(int id, int newExpires);

    // 增加一个新的结点
    void add(int id, int timeOut, const TimeoutCallBack &cb);

    // 删除节点，并触发回调函数
    void doWork(int id);

    // 清空定时器
    void clear();

    // 清除超时结点
    void tick();

    // 弹出一个结点
    void pop();

    int GetNextTick();

private:
    void del_(size_t i);

    void siftup_(size_t i);

    bool siftdown_(size_t index, size_t n);

    void SwapNode_(size_t i, size_t j);

    std::vector <TimerNode> heap_;

    // 这个代表什么？？ 映射 结点在vector中的下标和其id的映射
    std::unordered_map<int, size_t> ref_;
};

#endif //HEAP_TIMER_H