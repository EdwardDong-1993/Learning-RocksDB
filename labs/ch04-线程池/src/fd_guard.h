// fd_guard.h —— RAII 的第一次实战：把裸 fd 包成"自动关闭"的类
// 第 3 章配套代码。规则：资源在构造函数里获得，在析构函数里释放。

#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <utility>

class FdGuard {
public:
    FdGuard() = default;
    explicit FdGuard(int fd) : fd_(fd) {}

    // RAII 的核心：析构自动释放
    ~FdGuard() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // 文件描述符是"独占资源"：禁止拷贝（两个对象各 close 同一个 fd 是灾难）
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    // 但允许"转移所有权"（移动）：源对象交出 fd 后失效，全程只有一个所有者
    FdGuard(FdGuard&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }  // if (fd) { ... }

    // 交出所有权（之后本对象不再负责 close）
    int release() { return std::exchange(fd_, -1); }

    // 关掉旧的，接管新的
    void reset(int fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;  // -1 表示"不持有"，类内始终处于合法状态
};
