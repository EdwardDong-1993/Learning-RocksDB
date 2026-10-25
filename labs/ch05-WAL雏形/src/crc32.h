// crc32.h —— 第 5 章：循环冗余校验
// 检测磁盘上的位翻转。标准 CRC-32（反射式，多项式 0xEDB88320，同 zlib/PNG）。
// 逐位计算：慢但代码透明；工业实现用查表法（快 8 倍）或 SSE4.2 硬件指令（快数十倍）。

#pragma once

#include <cstddef>
#include <cstdint>

inline uint32_t crc32(const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;                 // 初值
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit) {     // 每个字节转 8 圈
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;                   // 终值取反
}
