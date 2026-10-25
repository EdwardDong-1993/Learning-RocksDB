// 第 3 章定稿快照：用 FdGuard(RAII) 接管 fd 生命周期
// 对比第 2 章快照（../ch02-磁盘物理定律实验/）：所有 close 交给析构，
// 异常/提前 return 也不漏 fd。第 4 章起主线又新增了线程池，看最新版请回根目录。

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

#include "fd_guard.h"

using namespace std::chrono;

constexpr uint64_t kTotal = 64ULL << 20;   // 总写入量 64 MB
constexpr size_t   kChunk = 4096;          // 4 KB 粒度
constexpr uint64_t kWrites = kTotal / kChunk;

alignas(4096) static char g_buf[kChunk];

static double mb_per_sec(uint64_t bytes, nanoseconds dur) {
    double sec = duration_cast<duration<double>>(dur).count();
    return (bytes / 1024.0 / 1024.0) / sec;
}

static bool open_direct(const char* path, FdGuard* out) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (fd < 0) {
        std::perror("open(O_DIRECT) 失败（文件系统不支持？）");
        return false;
    }
    out->reset(fd);
    return true;
}

static bool test_seq_write(const char* path) {
    FdGuard fd;
    if (!open_direct(path, &fd)) return false;

    auto t0 = steady_clock::now();
    for (uint64_t i = 0; i < kWrites; ++i) {
        ssize_t n = write(fd.get(), g_buf, kChunk);
        if (n != static_cast<ssize_t>(kChunk)) {   // 这次认真检查返回值
            std::perror("write 失败");
            return false;
        }
    }
    fdatasync(fd.get());
    auto t1 = steady_clock::now();

    std::printf("顺序写: %llu MB, 吞吐 %.1f MB/s\n",
                (unsigned long long)(kTotal >> 20), mb_per_sec(kTotal, t1 - t0));
    return true;   // fd 的 close 由 FdGuard 析构负责，无需手动
}

static bool test_rand_write(const char* path) {
    FdGuard fd;
    if (!open_direct(path, &fd)) return false;
    ftruncate(fd.get(), static_cast<off_t>(kTotal));

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<uint64_t> dist(0, kWrites - 1);

    auto t0 = steady_clock::now();
    for (uint64_t i = 0; i < kWrites; ++i) {
        off_t off = static_cast<off_t>(dist(rng) * kChunk);
        ssize_t n = pwrite(fd.get(), g_buf, kChunk, off);
        if (n != static_cast<ssize_t>(kChunk)) {
            std::perror("pwrite 失败");
            return false;
        }
    }
    fdatasync(fd.get());
    auto t1 = steady_clock::now();

    std::printf("随机写: %llu MB(4KB 粒度), 吞吐 %.1f MB/s\n",
                (unsigned long long)(kTotal >> 20), mb_per_sec(kTotal, t1 - t0));
    return true;
}

int main() {
    std::printf("=== 磁盘物理定律实验 (RAII 重构版) ===\n");
    bool ok = test_seq_write("mr_seq_test.bin");
    ok = ok && test_rand_write("mr_rand_test.bin");
    if (!ok) return 1;
    unlink("mr_seq_test.bin");
    unlink("mr_rand_test.bin");
    return 0;
}
