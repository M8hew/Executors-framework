# Executors framework
This is a learning task written as part of the course _Advanced C ++_ at HSE

## Introduction

Often when writing multi-threaded calculations, 2 independent actions are performed:

1. A large task is divided into small independent subtasks. Then
   subtask solutions are merged together. This code is specific to each algorithm.

2. The code decides how many threads to run, how to run them, and when
   complete them. This code is the same everywhere.

Let's look at the problems associated with the second point:
* The user cannot control how many threads will be launched.
  _The code acts selfishly and takes up all the cores on the machine._

* It is inconvenient to use such code inside another parallel algorithm.
  _For example, if at the first level we divide the task into 10 parts and 
  we want to solve each with the help of `Compute()`, then we will 
  start `10 * hardware concurrency` threads._

* You cannot cancel the calculation or monitor progress.

All problems arise from the fact that the code itself is engaged in the creation
threads. We want to take this decision to the highest level, and in
code to leave only the division into independent subtasks. 
Here is the implementation of the library to help you perform this separation.

### Executors и Tasks
* `Task` is some piece of calculations. The calculation code itself is in the run() method and is defined by the user.
* `Executor` is a thread-pool that can execute `Task`s.
* `Executor` starts threads in the constructor and no new threads are created while running.
* To start executing a `Task`, the user must send it to the `Executor` using the method
  `Submit()`.
* After that, the user can wait for the `Task` to complete by calling the `Task::Wait` method.

```c++
class MyPrimeSplittingTask : public Task {
    Params params_;
public:
    MyPrimeSplittingTask(Params params) : params_(params) {}
    bool is_prime = false;
    virtual void Run() {
        is_prime = check_is_prime(params_);
    }
}

bool DoComputation(std::shared_ptr<Executor> pool, Params params) {
    auto my_task = std::make_shared<MyPrimeSplittingTask>(params);
    pool->Submit(my_task);
    my_task.Wait();
    return my_task->is_prime;
}
```
* `Task` may complete successfully (`IsCompleted`), with an error
(`IsFailed`) and be canceled (`IsCanceled`). After being with him
one of these events occurred it is considered completed (`IsFinished`).

* The user can cancel the `Task` at any time using the method
`Cancel()`. In this case, if the execution of `Task` is not yet
started, it won't start.

* `Task` may have dependencies. The user can make one `Task` only 
  execute after some other `Task` has completed by calling the method
  `Task::AddDependency`.

* `Task` can have triggers (`Task::AddTrigger`). In this case, it will 
  start executing after at least one trigger has completed.

* `Task` can have one time trigger
  (`Task::SetTimeTrigger`). In this case it will start
  execution if `deadline` time has come.

* `Executor::Submit` does not start execution immediately, it will wait
  until the following conditions are met:
  * there are dependencies and all of them were executed
  * one of the triggers has become executed
  * `deadline` is set, and it is `deadline` time.
  <br> If the task has no dependencies, no triggers, and no deadline, then it can execute immediately.
  Until `Submit` is called on a task, it will not be executed.

* `Executor` provides an API for stopping execution.
  * `Executor::StartShutdown` - starts the shutdown process. Tasks that were sent after `StartShutdown` 
    immediately go into the Canceled state. The function can be called multiple times.
  * `Executor::WaitShutdown` - blocks until the Executor stops.
    The function can be called multiple times.
  * `Executor::~Executor` - implicitly does a shutdown and waits for the threads to finish.

### Futures
* `Future` is a `Task` that has a result (some value).
* `Invoke(callback)` - execute `callback` inside `Executor`, return result via `Future`.
* `Then(input, callback)` - execute `callback` after `input` ends. Returns a `Future` on the result of `cb` without waiting for `input` to complete.
* `WhenAll(vector<FuturePtr<T>> ) -> FuturePtr<vector<T>>` - collects the result of several `Future` into one.
* `WhenFirst(vector<FuturePtr<T>>) -> FuturePtr<T>` - returns the result that appears first.
* `WhenAllBeforeDeadline(vector<FuturePtr<T>>, deadline) -> FuturePtr<vector<T>>` - returns all results that had time to appear before the deadline.

### What to improve

At the moment, the main problem is the inefficient implementation of the unbounded_blocking_queue.
The unbounded_blocking_queue inside is just thread-safe FIFO.
It is possible to reduce the time on select the next `Task` for execution, 
if we replace the implementation with a thread-safe priority queue.