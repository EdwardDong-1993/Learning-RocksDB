# 03 | C++ 弹药库（上）：RAII、智能指针、移动语义

> 上篇思考题解答 → 为什么存储工程师的 bug 多数是"资源管理" → RAII 实战 → 智能指针选型 → 移动语义。本章动手任务：把第 2 章实验程序的裸 `fd` 重构成 `FdGuard`。

## 一、上篇思考题解答

**Q1：随机写实验不预分配（ftruncate）会怎样？**

不预分配时，随机位置的写入会触发文件系统**按需分配空间**：ext4 会为稀疏空洞动态分配 extent，"写数据"之外叠加了"随机分配空间"的元数据开销，实验测的不再纯粹是"数据落盘路径"。预分配把**空间分配与数据写入解耦**，保证唯一变量是写入位置——这是做性能对比实验的基本修养：控制变量。

**Q2：O_DIRECT 为什么强制对齐？**

`O_DIRECT` 的本意是让磁盘控制器（DMA）**直接**读写用户缓冲区，跳过内核中转。而磁盘按扇区/逻辑块工作，一次传输必须是"整数个块"。如果你的缓冲区地址、大小、偏移任意，内核就得替你拷贝拼接一个对齐的临时块——那又变回了缓冲 IO。所以内核干脆拒绝：三者必须按逻辑块大小对齐（`alignas(4096)` 的由来）。

**Q3：`alignas` / `steady_clock` / `duration_cast` / `(void)n`？**

一句话版：`alignas(4096)` 强制变量按 4096 地址对齐（服务 O_DIRECT）；`steady_clock` 是单调时钟，不受系统改时间影响，测耗时专用；`duration_cast` 把时间长度换算单位；`(void)n` 是"我知道返回值没用了，故意丢弃"，避免编译警告。本章及后续会反复见到它们。

## 二、为什么先讲资源管理

写存储引擎的人有句经验之谈：**代码里 bug 的分布，大致是资源管理（内存、fd、锁）占一半，业务逻辑占一半的一半，剩下的才是算法**。原因很朴素：

- 引擎是长驻进程，一个 fd 漏关，跑七天就是七个 fd 泄漏
- 引擎的代码路径充满"提前返回、异常、并发竞争"，`new` 了之后谁来 `delete`、`open` 了之后谁来 `close`，人工约定极易出错
- 存储引擎管着用户数据——资源 bug 的尽头是数据丢失

C++ 给出的系统性答案，就是 **RAII**。

## 三、RAII：把资源变成"对象的生命周期"

**RAII**（Resource Acquisition Is Initialization，资源获取即初始化）核心只有两条：

1. **构造函数里获取资源**（打开 fd、分配内存、加锁）
2. **析构函数里释放资源**（关闭 fd、释放内存、解锁）

而 C++ 保证：**离开作用域时，析构函数一定被调用**——无论是正常返回、提前 return，还是抛异常。于是"资源何时释放"这个需要人工追踪的问题，被编译器接管了。

### 动手：写第一个 RAII 类

第 2 章的实验程序里，`fd` 是裸的 int，靠人工 `close`。本章重构为 `FdGuard`（完整代码见仓库 `src/fd_guard.h`）：

```cpp
class FdGuard {
public:
    FdGuard() = default;
    explicit FdGuard(int fd) : fd_(fd) {}

    ~FdGuard() {                       // ① RAII 核心：析构自动关闭
        if (fd_ >= 0) close(fd_);
    }

    FdGuard(const FdGuard&) = delete;  // ② 独占资源，禁止拷贝
    FdGuard& operator=(const FdGuard&) = delete;

    FdGuard(FdGuard&& other) noexcept  // ③ 允许移动（所有权转移）
        : fd_(std::exchange(other.fd_, -1)) {}

    int get() const { return fd_; }

private:
    int fd_ = -1;                      // ④ 类内不变量：-1 = 不持有
};
```

四个要点，逐个说透：

**① 析构兜底**。持有 fd 的对象离开作用域，fd 必然被关。提前 return？关。抛异常？关。忘写 close？不存在"忘"这个选项了。

**② 为什么要删掉拷贝**。想象拷贝被允许：`FdGuard b = a;` 之后 `a`、`b` 各自析构，同一个 fd 被 `close` 两次——第二次是 UB（还可能关到别人后来打开的文件上，酿成数据事故）。**独占资源的类，必须禁拷贝**。

**③ 移动 = 转移所有权**。`std::exchange(other.fd_, -1)` 一行完成"我拿走 fd，你变回 -1"——全程只有一个所有者，交接后源对象立刻失效。这是移动语义的最小实例，第五节展开。

**④ 不变量**。`fd_` 永远要么是 -1 要么是有效 fd，任何成员函数都不需要"先检查是否初始化过"——好类的标志是**自己维护自己的不变量**。

重构后的实验代码（节选）：

```cpp
static bool test_seq_write(const char* path) {
    FdGuard fd;
    if (!open_direct(path, &fd)) return false;   // 提前返回？fd 会被 close

    for (uint64_t i = 0; i < kWrites; ++i) {
        ssize_t n = write(fd.get(), g_buf, kChunk);
        if (n != static_cast<ssize_t>(kChunk)) {
            return false;                          // 错误返回？fd 会被 close
        }
    }
    fdatasync(fd.get());
    return true;                                   // 正常返回？fd 会被 close
}
```

对比第 2 章：所有错误路径都不再需要记得 `close`。这就是 RAII 的全部意义——**把"必须记得"变成"不可能忘"**。重构后重跑实验，吞吐不变（顺序 8.6 / 随机 2.4 MB/s），功能无损。

## 四、智能指针：标准库版 RAII

`FdGuard` 是我们手写的，而 C++ 标准库已经为最常用的资源——**内存**——准备了现成的 RAII 类：

| 智能指针 | 语义 | 存储引擎里的典型用法 |
|---|---|---|
| `std::unique_ptr<T>` | **独占**，禁拷贝、可移动 | 表对象、迭代器、Compaction 任务——绝大多数场景 |
| `std::shared_ptr<T>` | 共享，引用计数 | 多个组件同时持有某资源（如 BlockCache 里的块） |
| `std::weak_ptr<T>` | 观察不拥有 | 打破 `shared_ptr` 循环引用（Cache 里的句柄常见） |

选型口诀只有一条：**默认 `unique_ptr`，证明真的需要共享才升级 `shared_ptr`**。引用计数不是免费的（原子加减），更重要的是"共享所有权"会让对象的生命周期变得难以推理——对存储引擎是毒药。

`unique_ptr` 用起来零成本（编译后和裸指针一样快），还自动 `delete`：

```cpp
std::unique_ptr<MemTable> mem = std::make_unique<MemTable>();
// ... mem 离开作用域，MemTable 析构自动触发
```

对照 RocksDB：源码里 `std::unique_ptr` 数以千计，`TableCache`、`Version`、Compaction 结构几乎全是独占所有权。它在读路径的 `BlockCache` 里才用 `shared_ptr`——因为一个数据块可能同时被多个读请求引用，生命周期确实"共享"。**选型逻辑和我们完全一致**。

## 五、移动语义：为什么"转移"比"复制"高级

KV 引擎里到处是"把一大块数据交给另一个人"的场景：MemTable 刷盘时把 64MB 的有序数据交给 SST Writer。**复制**它（ memcpy 64MB）显然浪费——原主人马上就不用了。我们要的是**搬家**。

移动语义就是搬家的语法化：对一个即将销毁的对象，允许"拿走它的内脏，留个空壳"。

```cpp
std::string a = std::string(64 * 1024 * 1024, 'x');  // 64MB
std::string b = std::move(a);   // 移动：3 个指针的交接，O(1)
                               // 此后 a 处于"有效但未指定"状态，别再用它
```

`std::move` 本身不移动任何东西——它只是**类型转换**，把左值标记成"可以被搬走的右值"，从而让重载决议选中**移动构造/移动赋值**。真正搬内脏的是 `FdGuard(FdGuard&&)` 那样的移动构造函数。

对 `FdGuard` 而言，移动 + 禁拷贝组合起来，恰好表达了"独占资源、可交接"的语义——这也是 `std::unique_ptr` 的实现结构。你手写完 `FdGuard`，等于亲手实现了一个 fd 版的 `unique_ptr`。

> RocksDB 对照：写入路径的 `WriteBatch`、SST 构建时的 `BlockBuilder` 缓冲，交接全用移动。把 64MB 数据从 MemTable 转给 SST Writer 时复制一份，在引擎代码评审里是会被打回来的。

## 六、本章小结

- **RAII**：资源 = 对象生命周期，把"记得释放"变成"不可能忘"
- **智能指针**：内存的现成 RAII；默认 `unique_ptr`，确需共享才 `shared_ptr`
- **移动语义**：独占资源的合法交接方式，`FdGuard` / `unique_ptr` 的基石
- 配套代码：`src/fd_guard.h`（手写 RAII 类），`main.cpp` 已完成 RAII 重构

## 七、思考题

1. `FdGuard` 如果声明 `explicit operator bool()` 不存在（删掉这行），`if (fd)` 还能编译吗？会发生什么？（提示：int 到 bool 的隐式转换）
2. `shared_ptr` 的引用计数为什么必须是**原子**操作？（下一篇并发篇的地基）
3. `std::move(a)` 之后 `a` 还能用吗？RocksDB 里大量 `std::move(memtable)` 之后代码继续使用该变量——是 bug 吗？什么情况下安全？

下一篇：**C++ 弹药库（下）**——mutex、atomic、条件变量、线程池。Compaction 是后台线程跑的，"多线程共享一份内存数据"从那时起成为常态，我们将给 MemTable 的雏形加上第一个读写锁。

---
*学习笔记：`Learning-RocksDB/notes/`*
*本章实验：`Learning-RocksDB/labs/ch03-RAII重构/`*
