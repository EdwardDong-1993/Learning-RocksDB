#include "wal.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "crc32.h"

namespace {
constexpr size_t kHeaderSize = 8;              // length(4) + crc(4)
constexpr uint32_t kMaxPayload = 32u << 20;    // 32MB 防线：头被写坏时不至于疯读
}  // namespace

WalWriter::WalWriter(const std::string& path) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::perror("WalWriter: open 失败");
        return;
    }
    fd_.reset(fd);
    valid_ = true;
}

bool WalWriter::append(const std::string& payload) {
    if (!valid_) return false;

    uint32_t len = static_cast<uint32_t>(payload.size());
    uint32_t crc = crc32(payload.data(), payload.size());

    // 单次 write 写完整条 record：
    // ① 顺序追加是 LSM 的命根子；② 尽量避免"半个头先落盘"的窗口
    std::string rec;
    rec.reserve(kHeaderSize + payload.size());
    rec.append(reinterpret_cast<const char*>(&len), 4);   // 教学版：本机字节序
    rec.append(reinterpret_cast<const char*>(&crc), 4);   // 思考题：有什么隐患？
    rec.append(payload);

    ssize_t n = ::write(fd_.get(), rec.data(), rec.size());
    if (n != static_cast<ssize_t>(rec.size())) {
        std::perror("WalWriter: write 失败");
        valid_ = false;
        return false;
    }
    return true;
}

bool WalWriter::sync() {
    if (!valid_) return false;
    if (::fdatasync(fd_.get()) != 0) {
        std::perror("WalWriter: fdatasync 失败");
        valid_ = false;
        return false;
    }
    return true;
}

std::vector<std::string> WalReader::replay(const std::string& path) {
    std::vector<std::string> out;
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return out;
    FdGuard guard(fd);

    for (;;) {
        // 1. 读头
        char header[kHeaderSize];
        ssize_t n = ::read(guard.get(), header, kHeaderSize);
        if (n == 0) break;                                // 干净 EOF：日志完整
        if (n != static_cast<ssize_t>(kHeaderSize)) break; // 头被撕裂 → 停

        uint32_t len = 0, crc = 0;
        std::memcpy(&len, header, 4);
        std::memcpy(&crc, header + 4, 4);
        if (len > kMaxPayload) break;                     // 头被写坏 → 停

        // 2. 读 payload（可能分多次 read 才凑满）
        std::string payload(len, '\0');
        size_t got = 0;
        while (got < len) {
            n = ::read(guard.get(), &payload[got], len - got);
            if (n <= 0) return out;                       // payload 撕裂 → 停
            got += static_cast<size_t>(n);
        }

        // 3. CRC 守门：位翻转在这里被拦下
        if (crc32(payload.data(), payload.size()) != crc) break;

        out.push_back(std::move(payload));
    }
    return out;
}
