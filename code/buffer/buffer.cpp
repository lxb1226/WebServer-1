/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */
#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

// TODO:什么意思
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

const char *Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

void Buffer::RetrieveUntil(const char *end) {
    assert(Peek() <= end);
    Retrieve(end - Peek());
}

// 清空
void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

// 将buffer中的内容转化成string
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

// 返回可写的位置常量指针
const char *Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

char *Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

// 将写的位置+len
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
}

// 将string添加进去
void Buffer::Append(const std::string &str) {
    Append(str.data(), str.length());
}

// 将空指针指向的数据添加进去
void Buffer::Append(const void *data, size_t len) {
    assert(data);
    Append(static_cast<const char *>(data), len);
}

// 添加数据
void Buffer::Append(const char *str, size_t len) {
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

// 添加另一个buff
void Buffer::Append(const Buffer &buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

// 确保可写，如果不够写，那么扩容
void Buffer::EnsureWriteable(size_t len) {
    if (WritableBytes() < len) {
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}

// 分散读
ssize_t Buffer::ReadFd(int fd, int *saveErrno) {
    char buff[65535];
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);
    if (len < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(len) <= writable) {
        // 如果读取的数据小于buffer中剩下的空间，那么直接写到buff中
        writePos_ += len;
    } else {
        // 不然，将buff中数据写入到buffer中
        writePos_ = buffer_.size();
        Append(buff, len - writable);
    }
    return len;
}

// 写
ssize_t Buffer::WriteFd(int fd, int *saveErrno) {
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if (len < 0) {
        *saveErrno = errno;
        return len;
    }
    readPos_ += len;
    return len;
}

char *Buffer::BeginPtr_() {
    return &*buffer_.begin();
}

const char *Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

void Buffer::MakeSpace_(size_t len) {
    if (WritableBytes() + PrependableBytes() < len) {
        // 如果剩下的空间不足，就扩容
        buffer_.resize(writePos_ + len + 1);
    } else {
        // 如果剩下的空间足够存放，那么就移动readPos-writePos到开头
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}