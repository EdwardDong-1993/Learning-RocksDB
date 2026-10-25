// 第 2 章：顺序写 vs 随机写（O_DIRECT 版）
// 本文件是第 2 章定稿快照——主线代码在第 3 章已被 RAII 重构，
// 想看"裸 fd、人工 close"的原始版本，在这里。

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>

using namespace std::chrono;

constexpr uint64_t kTotal = 64ULL << 20;   // 总写入量 64 MB
constexpr size_t   kChunk = 4096;          // 4 KB 粒度
constexpr uint64_t kWrites = kTotal / kChunk;

alignas(4096) static char g_buf[kChunk];

static double mb_per_sec(uint64_t bytes, nanoseconds dur) {
    double sec = duration_cast<duration<double>>(dur).count();
    return (bytes / 1024.0 / 1024.0) / sec;
}

static int open_direct(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (fd < 0) {
        std::perror("open(O_DIRECT) 失败（文件系统不支持？）");
    }
    return fd;
}

static void test_seq_write(const char* path) {
    int fd = open_direct(path);
    if (fd < 0) return;

    auto t0 = steady_clock::now();
    for (uint64_t i = 0; i < kWrites; ++i) {
        ssize_t n = write(fd, g_buf, kChunk);
        (void)n;  // 演示代码，省略错误处理；正式引擎里每个返回值都要检查
    }
    fdatasync(fd);  // 确保真正落盘再计时结束
    auto t1 = steady_clock::now();
    close(fd);

    std::printf("顺序写: %llu MB, 吞吐 %.1f MB/s\n",
                (unsigned long long)(kTotal >> 20), mb_per_sec(kTotal, t1 - t0));
}

static void test_rand_write(const char* path) {
    int fd = open_direct(path);
    if (fd < 0) return;
    ftruncate(fd, static_cast<off_t>(kTotal));  // 预分配，避免"随机写"变"随机扩展"

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<uint64_t> dist(0, kWrites - 1);

    auto t0 = steady_clock::now();
    for (uint64_t i = 0; i < kWrites; ++i) {
        pwrite(fd, g_buf, kChunk, static_cast<off_t>(dist(rng) * kChunk));
    }
    fdatasync(fd);
    auto t1 = steady_clock::now();
    close(fd);

    std::printf("随机写: %llu MB(4KB 粒度), 吞吐 %.1f MB/s\n",
                (unsigned long long)(kTotal >> 20), mb_per_sec(kTotal, t1 - t0));
}

int main() {
    std::printf("=== 磁盘物理定律实验 (O_DIRECT 直写介质) ===\n");
    test_seq_write("mr_seq_test.bin");
    test_rand_write("mr_rand_test.bin");
    unlink("mr_seq_test.bin");
    unlink("mr_rand_test.bin");
    return 0;
}
