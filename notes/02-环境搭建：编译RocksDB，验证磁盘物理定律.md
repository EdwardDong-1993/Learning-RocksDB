# 02 | 环境搭建：编译 RocksDB，亲手验证磁盘物理定律

> 第 1 章说：顺序写几乎免费，随机写很贵。这一章，我们**用自己机器的数据证明它**——顺便把开发环境和实验工程骨架搭起来。

## 一、上篇思考题解答

**Q1：先写 MemTable 再写 WAL 行不行？**

不行，这正是 "Write-**Ahead** Log"（写**前**日志）名字的含义。反过来写的话，崩溃若发生在"内存已写、日志未写"之间：内存重启后清空，日志里也没有——这条数据**无声丢失**。而先写日志，最坏情况是"日志有、内存没有"，重启时重放日志就能恢复。谁先谁后，本质是崩溃时刻的数据安全边界。

**Q2：v1 在旧 SST、v2 在新 SST，读的时候怎么知道用哪个？**

给每个写入打上**全局递增的序号**（RocksDB 里叫 Sequence Number）。v2 的序号必然大于 v1，读取时取"序号最大的那条"。同一个 key 的多版本共存、按序号新旧取值——这就是 MVCC（多版本并发控制）的雏形，第 9 章展开。

**Q3：墓碑一直留着 vs 过早扔掉，各出什么问题？**

留着：磁盘空间被"已删除"的数据越占越多（空间放大），读取时还要带着墓碑一路判断（读放大）。
过早扔掉：更老的 SST 里 `v1` 还活着，墓碑一删，`v1` **诈尸**——删掉的数据复活了。
所以墓碑必须等它"下沉"到确认没有更老版本的地方（比如最底层）才能安全丢弃。这个 GC 时机是 LSM 最经典的坑，第 20 章专门讲。

## 二、本章三件事

1. **工具链**：一套能编译、调试 C++ 的环境
2. **编译 RocksDB**：把"标准答案"放到手边，随时翻看
3. **建实验工程骨架**：跑第一个实验，验证磁盘物理定律

## 三、工具链清单

本专栏全程在 Linux 下进行（存储引擎的主战场）。一条命令装齐：

```bash
# Ubuntu / Debian
sudo apt install -y build-essential cmake gdb git \
    libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev
```

各工具的角色：

| 工具 | 角色 |
|---|---|
| g++ / clang | 编译器，需支持 C++17 |
| cmake | 工程构建（我们的实验工程也用它） |
| gdb | 调试器，读引擎代码时"跑起来看"比"盯着看"高效十倍 |
| gflags / snappy / zstd 等 | RocksDB 的可选依赖，装上避免编译报错 |

## 四、编译 RocksDB 本体

```bash
cd ~
git clone https://github.com/facebook/rocksdb.git
cd rocksdb
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) db bench
```

两个提前打的预防针：

1. **编译很慢**：全量编译 20~60 分钟很正常，教程专栏等它一次就够了。也可以只编译工具：`make -j$(nproc) db_bench`。
2. **内存不足会失败**：编译 RocksDB 峰值可能吃几个 GB 内存，虚拟机至少给 4GB。

编译成功后，先跑一发官方压测工具 `db_bench`，直观感受一下"为写而生"是什么意思：

```bash
./db_bench --benchmarks=fillseq,readrandom --num=1000000
# 输出（节选，不同机器数值不同）：
# fillseq   : 2.1 micros/op 470000 ops/sec  (写入)
# readrandom: 3.5 micros/op 280000 ops/sec  (点查)
```

100 万次写入每秒几十万次——这就是 LSM 顺序追加的威力。db_bench 的参数和指标解读，第 26 章调优篇再展开。

## 五、建立实验工程骨架

从本章起，仓库里的每个实验都是独立可编译的小工程（本章完整代码见 `Learning-RocksDB/labs/ch02-磁盘物理定律实验/`）：

```
labs/ch02-磁盘物理定律实验/
├── CMakeLists.txt
├── .gitignore
└── src/
    └── main.cpp     # 暂时是个实验程序，后续章节基于它逐步长出引擎部件
```

`CMakeLists.txt` 全文（每一行后面 29 章都要背书）：

```cmake
cmake_minimum_required(VERSION 3.16)
project(lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # 给 clangd / 编辑器用

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(lab src/main.cpp)
```

三个值得注意的点：

- **C++17**：现代存储引擎的主流选择（RocksDB 自身已迁移到 C++17/20）
- **CMAKE_EXPORT_COMPILE_COMMANDS**：生成 `compile_commands.json`，VSCode/clangd 的智能提示靠它
- **`-Wall -Wextra`**：从第一天就开着警告。存储引擎里一个被忽略的警告，可能是将来的一个数据丢失

编译三连，形成肌肉记忆：

```bash
cmake -S . -B build          # 生成构建目录
cmake --build build -j4      # 编译
./build/lab                 # 运行
```

## 六、本章实验：亲手验证磁盘物理定律

### 实验设计

第 1 章的结论需要证据。实验很朴素：**写入总量固定（64MB、4KB 粒度），只改变落盘方式**——

- 实验 A：从文件头到尾**顺序追加**
- 实验 B：同样的数据量，写在**随机位置**

唯一变量是"写入位置是否随机"，吞吐差就是"随机写的代价"。

### 踩坑实录：被页缓存"骗"了一次

第一版实验用的是普通的 `std::ofstream`，跑出来的结果是这样的：

```
顺序写: 256 MB, 吞吐 1100.2 MB/s
随机写: 256 MB, 吞吐 1366.8 MB/s   ← 随机写居然更快？！
```

**随机写比顺序写还快**——这不可能，问题出在哪？

答案：**页缓存（Page Cache）**。Linux 对普通文件写入的默认行为是"先写进内存里的缓存，由操作系统择机异步刷盘"。256MB 的数据全部被内存兜底，`seekp` 到随机位置只是在缓存里挪指针——**我们测的是内存，不是磁盘**。

> 这个坑本身就是极好的教学：**"写了"和"落盘了"是两回事**。第 5 章讲 WAL 可靠性时会正面攻这个问题（fsync、页缓存、断电），这里先用工程手段绕过——

### 修正：用 O_DIRECT 直写介质

`O_DIRECT` 标志让写入**绕过页缓存直接落盘**，是压测和数据库领域的标准做法。核心代码：

```cpp
#include <fcntl.h>
#include <unistd.h>

constexpr uint64_t kTotal = 64ULL << 20;   // 64 MB
constexpr size_t   kChunk = 4096;          // 4 KB 粒度

// O_DIRECT 要求：缓冲区地址、大小、偏移都按逻辑块对齐
alignas(4096) static char g_buf[kChunk];

// 顺序追加
int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
for (uint64_t i = 0; i < kTotal / kChunk; ++i) {
    write(fd, g_buf, kChunk);              // 逐块往后追加
}
fdatasync(fd);                             // 确保真正落盘再停表

// 随机写（另一个实验）：同样的量，位置随机
std::mt19937_64 rng(42);
std::uniform_int_distribution<uint64_t> dist(0, kTotal / kChunk - 1);
for (uint64_t i = 0; i < kTotal / kChunk; ++i) {
    pwrite(fd, g_buf, kChunk, dist(rng) * kChunk);
}
fdatasync(fd);
```

### 真实结果

在我的开发机上（注意：绝对值和机器强相关，**比值才是重点**）：

```
=== 磁盘物理定律实验 (O_DIRECT 直写介质) ===
顺序写: 64 MB, 吞吐 9.8 MB/s
随机写: 64 MB(4KB 粒度), 吞吐 2.3 MB/s
```

**同样的数据量，随机写吞吐只有顺序写的约 1/4**。而这还是在写一个文件——B+ 树的真实写入是"先随机读一个 16KB 页上来，改完再随机写回去"，读写双向放大。LSM-Tree"只追加、不修改"的设计动机，现在是你亲手测出来的事实了。

> 顺带一提：`alignas(4096)`、`pwrite`、`fdatasync` 这些词眼，后面章节会反复出现——它们是写存储引擎的基本功。

## 七、本章小结

- 环境就绪：工具链 + 编译好的 RocksDB + 实验工程骨架
- 亲手验证了磁盘物理定律：随机写显著慢于顺序写
- 踩到并理解了第一个工业级坑：**页缓存让"写入"看起来免费**——这是第 5 章（WAL 与数据可靠性）的伏笔

## 八、思考题

1. 实验 B 里我们先 `ftruncate` 预分配了文件大小，再随机写。如果不预分配，测出来会更接近哪种结果？为什么？
2. `O_DIRECT` 为什么强制要求"缓冲区地址、大小、偏移"三者对齐？（提示：磁盘是按扇区/块为单位工作的，内核无法替你"拼凑"一个未对齐的块）
3. 本章代码里出现了 `alignas`、`steady_clock`、`duration_cast`、`(void)n`——这些 C++ 特性你都能说清楚吗？说不全没关系，这正是下一篇的内容。

下一篇：**C++ 弹药库（上）**——RAII、智能指针、移动语义。写存储引擎的人，90% 的 bug 来自内存与资源管理，我们先把这把枪擦亮。

---
*学习笔记：`Learning-RocksDB/notes/`*
*本章实验：`Learning-RocksDB/labs/ch02-磁盘物理定律实验/`*
