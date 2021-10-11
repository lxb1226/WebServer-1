/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <cstring>   //perror
#include <iostream>
#include <unistd.h>  // write
#include <sys/uio.h> //readv
#include <vector> //readv
#include <atomic>
#include <assert.h>

// 缓冲区类 使用vector实现
// 参考的moduo的设计
class Buffer {
public:
    Buffer(int initBuffSize = 1024);

    ~Buffer() = default;

    size_t WritableBytes() const;

    size_t ReadableBytes() const;

    size_t PrependableBytes() const;

    const char *Peek() const;

    void EnsureWriteable(size_t len);

    void HasWritten(size_t len);

    // 恢复
    void Retrieve(size_t len);

    void RetrieveUntil(const char *end);

    void RetrieveAll();

    std::string RetrieveAllToStr();

    const char *BeginWriteConst() const;

    char *BeginWrite();

    // 添加不同类型的数据
    void Append(const std::string &str);

    void Append(const char *str, size_t len);

    void Append(const void *data, size_t len);

    void Append(const Buffer &buff);

    // 分散写以及分散读
    ssize_t ReadFd(int fd, int *Errno);

    ssize_t WriteFd(int fd, int *Errno);

private:
    char *BeginPtr_();

    const char *BeginPtr_() const;

    void MakeSpace_(size_t len);

    std::vector<char> buffer_;
    std::atomic <std::size_t> readPos_;  // 已读的位置
    std::atomic <std::size_t> writePos_; // 已写的位置
};

#endif //BUFFER_H