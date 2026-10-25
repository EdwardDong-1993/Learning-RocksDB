// thread_pool.h —— 第 4 章实战：一个 RAII 风格的线程池
// 后续 Compaction 后台线程直接复用这个组件。
// 三件套：mutex(护队列) + condition_variable(通知) + RAII(析构收尾)

#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t thread_num) {
        workers_.reserve(thread_num);
        for (size_t i = 0; i < thread_num; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() { shutdown(); }

    // RAII 精神延续：线程池也是独占资源，禁拷贝、禁赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务。返回后任务可能还没执行（异步）
    void submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stopping_) return;  // 已关闭，静默丢弃（存储引擎里要不要丢？思考题）
            tasks_.push(std::move(task));
        }
        cv_.notify_one();  // 叫醒一个睡着的工作线程
    }

    size_t pending() const {
        std::lock_guard<std::mutex> lk(mu_);
        return tasks_.size();
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mu_);
                // 谓词版 wait：解决"虚假唤醒"，也解决"醒早了没活干"
                cv_.wait(lk, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) return;  // 关闭且干完活，退出
                task = std::move(tasks_.front());
                tasks_.pop();
            }  // 注意：执行任务时已放锁（思考题：不放会怎样？）
            task();
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stopping_) return;
            stopping_ = true;
        }
        cv_.notify_all();  // 叫醒所有人，让它们看到 stopping_ 后有序退出
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stopping_ = false;  // 只在持有 mu_ 时读写——为什么这样就不用 atomic？思考题
};
