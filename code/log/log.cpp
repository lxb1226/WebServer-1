/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */
#include "log.h"

using namespace std;

// 构造函数，给一些类成员赋值
Log::Log() {
    lineCount_ = 0;
    isAsync_ = false;
    writeThread_ = nullptr;
    deque_ = nullptr;
    toDay_ = 0;
    fp_ = nullptr;
}

// 析构函数，需要关闭写线程
Log::~Log() {
    if (writeThread_ && writeThread_->joinable()) {
        while (!deque_->empty()) {
            deque_->flush();
        };
        deque_->Close();
        writeThread_->join();
    }
    if (fp_) {
        lock_guard <mutex> locker(mtx_);
        flush();
        fclose(fp_);
    }
}

int Log::GetLevel() {
    lock_guard <mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level) {
    lock_guard <mutex> locker(mtx_);
    level_ = level;
}

// 初始化
void Log::init(int level = 1, const char *path, const char *suffix,
               int maxQueueSize) {
    // 初始化部分类成员
    isOpen_ = true;
    level_ = level;
    if (maxQueueSize > 0) {
        isAsync_ = true;
        if (!deque_) {
            unique_ptr <BlockDeque<std::string>> newDeque(new BlockDeque<std::string>);
            deque_ = move(newDeque);

            std::unique_ptr <std::thread> NewThread(new thread(FlushLogThread));
            writeThread_ = move(NewThread);
        }
    } else {
        isAsync_ = false;
    }

    lineCount_ = 0;

    // 时间以及log文件所在路径以及文件名
    time_t timer = time(nullptr);
    struct tm *sysTime = localtime(&timer);
    struct tm t = *sysTime;
    path_ = path;
    suffix_ = suffix;
    char fileName[LOG_NAME_LEN] = {0};
    // 文件名：path/year_mon_day.suffix
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
             path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
    toDay_ = t.tm_mday;

    // 将buff中数据写进文件，再重新打开一个文件，这是为了确保到了第二天，重新写一个新的日志文件
    {
        lock_guard <mutex> locker(mtx_);
        buff_.RetrieveAll();
        if (fp_) {
            flush();
            fclose(fp_);
        }

        fp_ = fopen(fileName, "a");
        if (fp_ == nullptr) {
            mkdir(path_, 0777);
            fp_ = fopen(fileName, "a");
        }
        assert(fp_ != nullptr);
    }
}

void Log::write(int level, const char *format, ...) {

    // 获取当前时间
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    // 可变参数
    va_list vaList;

    /* 日志日期 日志行数 */
    // 如果不是当天或当前行超出了允许的最大行数
    if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_ % MAX_LINES == 0))) {
        unique_lock <mutex> locker(mtx_);
        // TODO:这里为什么解锁
        locker.unlock();

        char newFile[LOG_NAME_LEN];
        // tail = year_mon_day
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);


        if (toDay_ != t.tm_mday) {
            // 到了第二天
            // log文件名：path_/tail.suffix
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        } else {
            // 超出了最大行数
            // log文件名:path_/tail-第几次.suffix_
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_ / MAX_LINES), suffix_);
        }

        locker.lock();
        flush();
        fclose(fp_);
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    {
        // 写一行日志
        unique_lock <mutex> locker(mtx_);
        lineCount_++;
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);

        buff_.HasWritten(n);
        AppendLogLevelTitle_(level);

        va_start(vaList, format);
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        va_end(vaList);

        buff_.HasWritten(m);
        buff_.Append("\n\0", 2);

        // 如果要异步写，那么将字符串加入队列，让异步写线程去写进日志文件
        if (isAsync_ && deque_ && !deque_->full()) {
            deque_->push_back(buff_.RetrieveAllToStr());
        } else {
            // 不然直接写入日志文件中
            fputs(buff_.Peek(), fp_);
        }
        buff_.RetrieveAll();
    }
}

// 添加日志等级title
void Log::AppendLogLevelTitle_(int level) {
    switch (level) {
        case 0:
            buff_.Append("[debug]: ", 9);
            break;
        case 1:
            buff_.Append("[info] : ", 9);
            break;
        case 2:
            buff_.Append("[warn] : ", 9);
            break;
        case 3:
            buff_.Append("[error]: ", 9);
            break;
        default:
            buff_.Append("[info] : ", 9);
            break;
    }
}

// 刷新
// TODO:设计的有问题
void Log::flush() {
    if (isAsync_) {
        deque_->flush();
    }
    fflush(fp_);
}

// 异步写函数
void Log::AsyncWrite_() {
    string str = "";
    while (deque_->pop(str)) {
        lock_guard <mutex> locker(mtx_);
        fputs(str.c_str(), fp_);
    }
}

// 单例函数，返回一个静态变量
Log *Log::Instance() {
    static Log inst;
    return &inst;
}

void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}