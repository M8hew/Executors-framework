#include <executors.h>

void Task::AddDependency(std::shared_ptr<Task> dep) {
    std::unique_lock lock(mutex_);
    dependencies_.push_back(std::move(dep));
}

void Task::AddTrigger(std::shared_ptr<Task> dep) {
    std::unique_lock lock(mutex_);
    triggers_.push_back(std::move(dep));
}

void Task::SetTimeTrigger(std::chrono::system_clock::time_point at) {
    std::unique_lock lock(mutex_);
    deadline_ = at;
}

bool Task::CanBeExecuted() {
    std::unique_lock lock(mutex_);

    for (auto& dependency : dependencies_) {
        if (dependency && !dependency->IsFinished()) {
            return false;
        }
    }

    if (std::chrono::system_clock::now() < deadline_) {
        return false;
    }

    for (auto& trigger : triggers_) {
        if (trigger && trigger->IsFinished()) {
            return true;
        }
    }
    return triggers_.empty();
}

bool Task::IsCompleted() {
    std::unique_lock lock(mutex_);
    return status_ == TaskStatus::kCompleted;
}

bool Task::IsFailed() {
    std::unique_lock lock(mutex_);
    return status_ == TaskStatus::kFailed;
}

bool Task::IsCanceled() {
    std::unique_lock lock(mutex_);
    return status_ == TaskStatus::kCanceled;
}

bool Task::IsFinished() {
    std::unique_lock lock(mutex_);
    return status_ != TaskStatus::kPending;
}

std::exception_ptr Task::GetError() {
    std::unique_lock lock(mutex_);
    return e_ptr_;
}

void Task::Cancel() {
    std::unique_lock lock(mutex_);
    if (status_ == TaskStatus::kPending) {
        status_ = TaskStatus::kCanceled;
        wait_.notify_all();
    }
}

void Task::Wait() {
    std::unique_lock lock(mutex_);
    while (status_ == TaskStatus::kPending) {
        wait_.wait(lock);
    }
}

void Task::SaveError(std::exception_ptr e_ptr) {
    std::unique_lock lock(mutex_);
    e_ptr_ = e_ptr;
    status_ = TaskStatus::kFailed;
    wait_.notify_all();
}

void Task::CompleteTask() {
    std::unique_lock lock(mutex_);
    status_ = TaskStatus::kCompleted;
    wait_.notify_all();
}

std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads) {
    return std::make_shared<Executor>(num_threads);
}

Executor::~Executor() {
    task_queue_.Close();
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

Executor::Executor(int num_threads) {
    workers_.reserve(num_threads);
    while (num_threads-- > 0) {
        workers_.emplace_back([this] { RunTask(); });
    }
}

void Executor::Submit(std::shared_ptr<Task> task) {
    if (task_queue_.IsClosed()) {
        task->Cancel();
        return;
    }
    if (!task->IsCanceled()) {
        task_queue_.Put(std::move(task));
    }
}

void Executor::StartShutdown() {
    task_queue_.Close();
}

void Executor::WaitShutdown() {
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void Executor::RunTask() {
    while (auto task = task_queue_.Take()) {
        if (!task || !task->CanBeExecuted()) {
            task_queue_.Put(task);
            continue;
        }
        if (task->IsCanceled()) {
            continue;
        }
        try {
            task->Run();
            task->CompleteTask();
        } catch (...) {
            std::exception_ptr e_ptr = std::current_exception();
            task->SaveError(e_ptr);
        }
    }
}

template <class T>
FuturePtr<T> Executor::Invoke(std::function<T()> fn) {
    auto task = std::make_shared<Future<T>>(fn);
    Submit(task);
    return task;
}
template <class Y, class T>
FuturePtr<Y> Executor::Then(FuturePtr<T> input, std::function<Y()> fn) {
    auto task = std::make_shared<Future<Y>>(fn);
    std::dynamic_pointer_cast<Task>(task)->AddDependency(input);
    Submit(task);
    return task;
}
template <class T>
FuturePtr<std::vector<T>> Executor::WhenAll(std::vector<FuturePtr<T>> all) {
    auto funk = [all] {
        std::vector<T> resulting_vector;
        resulting_vector.reserve(all.size());
        for (FuturePtr<T> task : all) {
            resulting_vector.emplace_back(task->Get());
        }
        return resulting_vector;
    };
    return Invoke<std::vector<T>>(funk);
}

template <class T>
FuturePtr<T> Executor::WhenFirst(std::vector<FuturePtr<T>> all) {
    auto funk = [all] {
        for (FuturePtr<T> task : all) {
            if (task->IsFinished()) {
                return task->Get();
            }
        }
    };
    auto task = std::make_shared<Future<T>>(funk);

    for (FuturePtr<T> elem : all) {
        task->AddTrigger(elem);
    }
    Submit(task);
    return task;
}

template <class T>
FuturePtr<std::vector<T>> Executor::WhenAllBeforeDeadline(
    std::vector<FuturePtr<T>> all, std::chrono::system_clock::time_point deadline) {

    auto funk = [all] {
        std::vector<T> finished_tasks_vector;
        finished_tasks_vector.reserve(all.size());
        for (FuturePtr<T> task : all) {
            if (task->IsFinished()) {
                finished_tasks_vector.emplace_back(task->Get());
            }
        }
        return finished_tasks_vector;
    };

    auto task = std::make_shared<Future<std::vector<T>>>(funk);
    task->SetTimeTrigger(deadline);

    Submit(task);
    return task;
}

template <class T>
T Future<T>::Get() {
    Wait();
    if (IsFailed()) {
        rethrow_exception(GetError());
    }
    return value_;
}
