# Learning-RocksDB

> 从 0 到 1 学习 RocksDB：**以原理笔记为主线，以动手实验为验证**。

- **`notes/`**：学习笔记（连载中），从 LSM-Tree 的动机一路讲到 RocksDB 的源码与调优
- **`labs/`**：每章配套的动手实验——**亲手实现 RocksDB 的每个部件**（跳表、WAL、SST、Compaction……），再回头对照工业实现看差距

约定：每个 lab 目录**自包含**（含自己的 CMakeLists），且包含截至该章的全部代码——后面的实验在前面实验的基础上演进，想跟做某一章，clone 后直接进对应目录即可，无需回溯。

为什么自己写一遍？读源码是"看懂"，写一遍是"懂为什么"。每章实验完成后与 RocksDB 真实实现做部件级对照，是本仓库的核心学习方法：

| 部件 | 本仓库实验实现 | RocksDB 工业实现 |
|---|---|---|
| WAL | 8B 头 + CRC32 | 7B 头 + crc32c + 32KB block 分段 |
| MemTable | 跳表（手写） | 跳表（无锁读 + arena） |
| SST | 自定义格式 | BlockBasedTable |
| Compaction | Leveled | Leveled / Tiered / FIFO / Universal |

## 当前进度

- [x] ch01 为什么是 LSM-Tree（原理：写/读/空间三大放大）
- [x] ch02 磁盘物理定律实验（顺序写 vs 随机写，O_DIRECT 实测）
- [x] ch03 RAII（`FdGuard`：手写 fd 版 `unique_ptr`）
- [x] ch04 线程池（mutex + condition_variable + atomic）
- [x] ch05 WAL 实验（CRC 校验 + 断电撕裂恢复）
- [ ] ch06 跳表（MemTable 骨架）
- [ ] ch07 Arena 内存池
- [ ] ch08~10 WAL 接入引擎 / 门面 API / 列族
- [ ] ch11~16 SST 落盘全家桶（格式 / Block / 布隆 / Manifest / Flush）
- [ ] ch17~21 Compaction 与读放大治理
- [ ] ch22~26 对齐 RocksDB 源码 + 调优
- [ ] ch27~30 事务 / 终局总结

## 快速开始

**环境要求**：Linux、支持 C++17 的编译器（GCC ≥ 8 / Clang ≥ 7）、CMake ≥ 3.16

每个实验独立可编译运行，进入对应章节目录即可：

```bash
git clone <你的仓库地址>
cd Learning-RocksDB/labs/ch05-WAL雏形
cmake -S . -B build
cmake --build build -j4
./build/lab
```

## 章节导航

| 章 | 笔记 | 实验 | 演示什么 |
|---|---|---|---|
| 01 | notes/01-为什么是LSM-Tree.md | —（纯原理章） | LSM 动机与三大放大 |
| 02 | notes/02-环境搭建：编译RocksDB，验证磁盘物理定律.md | labs/ch02-磁盘物理定律实验/ | 本机实测随机写慢 4 倍+ |
| 03 | notes/03-C++弹药库（上）：RAII、智能指针、移动语义.md | labs/ch03-RAII重构/ | fd 生命周期自动化 |
| 04 | notes/04-C++弹药库（下）：并发三件套与线程池.md | labs/ch04-线程池/ | 8 任务 4 线程并发调度 |
| 05 | notes/05-CRC、fsync与页缓存：写了不等于落盘.md | labs/ch05-WAL雏形/ | 断电撕裂与位翻转的恢复 |

## 致谢

- [RocksDB](https://github.com/facebook/rocksdb) & [LevelDB](https://github.com/google/leveldb)：所有对照学习的"标准答案"
- CMU 15-445 / MIT 6.5840：教学思路的来源之一

## License

MIT
