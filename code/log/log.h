/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */
#ifndef LOG_H
#define LOG_H

#include <mutex>
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end
#include <assert.h>
#include <sys/stat.h>         //mkdir
#include "blockqueue.h"
#include "../buffer/buffer.h"

class Log {
public:
    void init(int level, const char *path = "./log",
              const char *suffix = ".log",
              int maxQueueCapacity = 1024);

    // 单例模式
    static Log *Instance();

    // 异步刷新线程执行函数
    static void FlushLogThread();

    // 写日志
    void write(int level, const char *format, ...);

    // 刷新日志
    void flush();

    int GetLevel();

    void SetLevel(int level);

    bool IsOpen() { return isOpen_; }

private:
    Log();

    // 添加日志等级title
    void AppendLogLevelTitle_(int level);

    virtual ~Log();

    void AsyncWrite_();

private:
    static const int LOG_PATH_LEN = 256;
    static const int LOG_NAME_LEN = 256;
    static const int MAX_LINES = 50000;

    const char *path_;  // 日志文件路径
    const char *suffix_;    // 日志文件后缀

    int MAX_LINES_;     // 最大行数

    int lineCount_;     // 当前行数
    int toDay_;         // 当前日期

    bool isOpen_;

    Buffer buff_;   // 缓冲区
    int level_;     // 当前日志输出等级
    bool isAsync_;  // 是否开启异步写

    FILE *fp_;      // 日志文件描述符
    std::unique_ptr <BlockDeque<std::string>> deque_;   // 队列
    std::unique_ptr <std::thread> writeThread_;     // 异步写线程
    std::mutex mtx_;
};

// 宏
#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::Instance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush();\
        }\
    } while(0);

#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

#endif //LOG_H