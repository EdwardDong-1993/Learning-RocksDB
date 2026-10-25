// wal.h —— 第 5 章实战：WAL（写前日志）雏形
//
// 物理格式（8 字节头 + 变长 payload）：
//   +-------------+------------+---------------------+
//   | length (4B) | crc32 (4B) | payload (length B)  |
//   +-------------+------------+---------------------+
//
// 对比 LevelDB/RocksDB 的真实格式：7 字节头（crc 4 + len 2 + type 1）+
// 32KB 定长 block 分段 + FIRST/MIDDLE/LAST 跨块类型。我们暂不分段，
// 语义先立住：一条 record 要么完整读出，要么当作崩溃尾巴丢弃。

#pragma once

#include <string>
#include <vector>

#include "fd_guard.h"

class WalWriter {
public:
    explicit WalWriter(const std::string& path);

    // 追加一条记录：先在内存拼好完整 record，再一次 write() 顺序写
    bool append(const std::string& payload);

    // 把已写记录钉到物理磁盘（fdatasync）
    bool sync();

    bool ok() const { return valid_; }

private:
    FdGuard fd_;
    bool valid_ = false;
};

class WalReader {
public:
    // 崩溃恢复：逐条读取并校验 CRC。
    // 策略：遇到"读不满 / CRC 不对 / 疯狂长度"就地停止——
    // 尾部残留的撕裂记录被丢弃，之前已校验通过的记录全部保留。
    static std::vector<std::string> replay(const std::string& path);
};
