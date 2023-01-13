#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unbounded_blocking_queue.h>
#include <vector>

class Executor;

class Task : public std::enable_shared_from_this<Task> {
public:
    using SysClock = std::chrono::system_clock;

    virtual ~Task() = default;

    virtual void Run() = 0;

    void AddDependency(std::shared_ptr<Task> dep);

    void AddTrigger(std::shared_ptr<Task> dep);

    void SetTimeTrigger(std::chrono::system_clock::time_point at);

    bool CanBeExecuted();

    // Task::run() completed without throwing exception
    bool IsCompleted();

    // Task::run() throwed exception
    bool IsFailed();

    // Task was Canceled
    bool IsCanceled();

    // Task either completed, failed or was Canceled
    bool IsFinished();

    std::exception_ptr GetError();

    void Cancel();

    void Wait();

private:
    friend Executor;

    void SaveError(std::exception_ptr e_ptr);

    void CompleteTask();

private:
    enum class TaskStatus { kPending, kCompleted, kFailed, kCanceled };

    std::mutex mutex_;
    std::condition_variable wait_;

    std::exception_ptr e_ptr_;
    TaskStatus status_ = TaskStatus::kPending;

    std::deque<std::shared_ptr<Task>> dependencies_;
    std::deque<std::shared_ptr<Task>> triggers_;

    SysClock::time_point deadline_ = std::chrono::system_clock::now();
};

template <class T>
class Future;

template <class T>
using FuturePtr = std::shared_ptr<Future<T>>;

struct Unit {};

class Executor {
public:
    ~Executor();

    Executor(int num_threads);

    void Submit(std::shared_ptr<Task> task);

    void StartShutdown();

    void WaitShutdown();

    template <class T>
    FuturePtr<T> Invoke(std::function<T()> fn);

    template <class Y, class T>
    FuturePtr<Y> Then(FuturePtr<T> input, std::function<Y()> fn);

    template <class T>
    FuturePtr<std::vector<T>> WhenAll(std::vector<FuturePtr<T>> all);

    template <class T>
    FuturePtr<T> WhenFirst(std::vector<FuturePtr<T>> all);

    template <class T>
    FuturePtr<std::vector<T>> WhenAllBeforeDeadline(std::vector<FuturePtr<T>> all,
                                                    std::chrono::system_clock::time_point deadline);

private:
    void RunTask();

private:
    UnboundedBlockingQueue<Task> task_queue_;
    std::vector<std::jthread> workers_;
};

std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads);

template <class T>
class Future : public Task {
public:
    Future(std::function<T()> fn) : fn_(std::move(fn)) {
    }

    ~Future() override = default;

    Future(const Future&) = default;
    Future(Future&&) = default;

    Future& operator=(const Future&) = default;
    Future& operator=(Future&&) = default;

    T Get();

    void Run() override;

private:
    T value_;
    std::function<T()> fn_;
};
