// 第 5 章：WAL 雏形三场景演示
//   ① 正常写入 → 重放全量恢复
//   ② 断电撕裂（最后一条写一半）→ 撕裂记录被丢弃
//   ③ 静默位翻转 → CRC 拦下损坏记录，重停在其后

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "wal.h"

static std::string make_rec(int i) {
    char buf[32];
    int m = std::snprintf(buf, sizeof(buf), "user_%04d=record_%d", i, i);
    return std::string(buf, m);
}

// 在 path 的指定偏移翻转一个 bit（模拟介质静默损坏）
static bool flip_one_byte(const char* path, off_t off) {
    int fd = ::open(path, O_RDWR);
    if (fd < 0) return false;
    FdGuard guard(fd);
    char c = '\0';
    if (::pread(guard.get(), &c, 1, off) != 1) return false;
    c ^= 0x20;
    return ::pwrite(guard.get(), &c, 1, off) == 1;
}

int main() {
    const std::string path = "mr_demo.wal";
    std::printf("=== WAL 雏形: 写入 / 崩溃恢复 / CRC 守门 ===\n");

    // ① 正常场景：写 5 条，钉盘，重放
    {
        WalWriter w(path);
        for (int i = 1; i <= 5; ++i) w.append(make_rec(i));
        w.sync();
    }
    std::printf("① 正常写入 5 条, 重放得到 %zu 条\n",
                WalReader::replay(path).size());

    // ② 崩溃场景：第 5 条只落盘了 3 字节 payload（模拟断电撕裂）
    {
        WalWriter w(path);
        size_t first4 = 0;  // 前 4 条的物理总长
        for (int i = 1; i <= 5; ++i) {
            std::string r = make_rec(i);
            w.append(r);
            if (i <= 4) first4 += 8 + r.size();
        }
        // 没有调用 sync，直接"断电"：文件被拦腰截断在第 5 条中间
        ::truncate(path.c_str(), static_cast<off_t>(first4 + 8 + 3));
    }
    std::printf("② 最后一条只写了 3 字节(模拟断电), 重放得到 %zu 条 → 撕裂记录被丢弃\n",
                WalReader::replay(path).size());

    // ③ 静默损坏场景：第 2 条 payload 中间被翻转一个 bit
    {
        WalWriter w(path);
        size_t rec1 = 0, rec2 = 0;
        for (int i = 1; i <= 5; ++i) {
            std::string r = make_rec(i);
            if (i == 1) rec1 = r.size();
            if (i == 2) rec2 = r.size();
            w.append(r);
        }
        w.sync();
        // 第 2 条 payload 的物理位置：8(rec1 头) + rec1 + 8(rec2 头) + rec2/2
        off_t poison = static_cast<off_t>(8 + rec1 + 8 + rec2 / 2);
        flip_one_byte(path.c_str(), poison);
    }
    std::printf("③ 第 2 条 payload 被翻转 1 bit, 重放得到 %zu 条 → 重停在损坏记录处\n",
                WalReader::replay(path).size());

    ::unlink(path.c_str());
    return 0;
}
