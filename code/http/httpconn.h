/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn {
public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in &addr);

    ssize_t read(int *saveErrno);

    ssize_t write(int *saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char *GetIP() const;

    sockaddr_in GetAddr() const;

    bool process();

    int ToWriteBytes() {
        return iov_[0].iov_len + iov_[1].iov_len;
    }

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    static bool isET;
    static const char *srcDir;  // 资源地址
    static std::atomic<int> userCount;  // 用户数量

private:

    int fd_;    // http连接对应的fd
    struct sockaddr_in addr_;  // 网络地址

    bool isClose_;  // 是否关闭

    int iovCnt_;    // 用于分散写以及分散读
    struct iovec iov_[2];

    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    HttpRequest request_;   // http请求
    HttpResponse response_; // http响应
};


#endif //HTTP_CONN_H