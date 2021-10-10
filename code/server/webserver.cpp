/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

using namespace std;

WebServer::WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger,
        int sqlPort, const char *sqlUser, const char *sqlPwd,
        const char *dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize) :
        port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
        timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller()) {
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    InitEventMode_(trigMode);
    if (!InitSocket_()) { isClose_ = true; }

    if (openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if (isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                     (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    // 为什么要free掉，并没有创建或者malloc
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode) {
        case 0:
            break;
        case 1:
            connEvent_ |= EPOLLET;
            break;
        case 2:
            listenEvent_ |= EPOLLET;
            break;
        case 3:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        default:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

// 开始函数
void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if (!isClose_) { LOG_INFO("========== Server start =========="); }
    while (!isClose_) {
        if (timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();
        }
        int eventCnt = epoller_->Wait(timeMS);
        for (int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            // 如果是监听事件，则处理监听
            if (fd == listenFd_) {
                DealListen_();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 关闭连接事件
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            } else if (events & EPOLLIN) {
                // 读事件
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            } else if (events & EPOLLOUT) {
                // 写事件
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

// 发送错误消息
void WebServer::SendError_(int fd, const char *info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

// 关闭客户端连接
void WebServer::CloseConn_(HttpConn *client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    // 将客户端对应的fd从epoll中删除
    epoller_->DelFd(client->GetFd());
    client->Close();
}

// 增加客户端
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr);
    if (timeoutMS_ > 0) {
        // 添加定时结点
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    // 将该事件加入到epoll中
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    // 设置fd非阻塞
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

// 处理监听事件
void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        // 接受客户端连接
        int fd = accept(listenFd_, (struct sockaddr *) &addr, &len);
        if (fd <= 0) { return; }
        else if (HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        // 添加客户端
        AddClient_(fd, addr);
    } while (listenEvent_ & EPOLLET);
}

// 处理读事件
void WebServer::DealRead_(HttpConn *client) {
    assert(client);
    ExtentTime_(client);
    // 添加到线程池中进行处理
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

// 处理写事件
void WebServer::DealWrite_(HttpConn *client) {
    assert(client);
    ExtentTime_(client);
    // 添加写事件
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 扩展客户端事件
void WebServer::ExtentTime_(HttpConn *client) {
    assert(client);
    if (timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

// 读取事件处理函数
void WebServer::OnRead_(HttpConn *client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    // 客户端读取数据
    ret = client->read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}

// TODO：这里什么意思？？
void WebServer::OnProcess(HttpConn *client) {
    if (client->process()) {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

// 写事件处理函数
void WebServer::OnWrite_(HttpConn *client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    // 客户端写数据
    ret = client->write(&writeErrno);
    if (client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if (client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* Create listenFd */
// 初始化服务器监听socket
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if (port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!", port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = {0};
    if (openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    // 设置listenfd属性
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int));
    if (ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    // 绑定fd
    ret = bind(listenFd_, (struct sockaddr *) &addr, sizeof(addr));
    if (ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 监听，设置最大监听数为6
    ret = listen(listenFd_, 6);
    if (ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    // 将listen_fd加入到epoll进行监听
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    // 设置事件非堵塞
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

// 设置fd不堵塞
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


