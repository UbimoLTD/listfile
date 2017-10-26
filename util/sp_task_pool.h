// Copyright 2015, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
#ifndef _UTIL_SP_TASK_POOL_H
#define _UTIL_SP_TASK_POOL_H

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "base/ProducerConsumerQueue.h"
#pragma GCC diagnostic pop

#include <pthread.h>

#include "base/event_count.h"
#include "base/type_traits.h"
#include "base/walltime.h"  // for GetMonotonicJiffies

/*
  Example:
  struct MyTask {
    struct SharedData {
      mutex* m;
      Output* o;
    };

    void operator(const string& str, SharedData& shared) {
      // Do something with str.
      std::lock_guard<mutex> lock(*shared.m);
      o->Write(...);
    }
  };

  using TaskPool = util::SingleProducerTaskPool<MyTask, string>;
  TaskPool pool("pool", 10);

  pool.SetSharedData(&mutex, &output);
  pool.Launch();
  ...
  pool.RunTask(some_string1);
  pool.RunTask(some_string2);
  ....
  pool.WaitForTasksToComplete();

  // Optionally if task has function Finalize():
  pool.Finalize()

*/


namespace util {

namespace internal {

/*
   Single producer task pool.
*/
class SingleProducerTaskPoolBase {
public:
  // Does not take ownership over shared_data.
  // per_thread_capacity - is queue capacity per each thread.
  SingleProducerTaskPoolBase(std::string name, unsigned per_thread_capacity,
                             unsigned num_threads = 0);
  virtual ~SingleProducerTaskPoolBase();

  // This function blocks until the pool in the state where each thread was in the state of
  // not having eny tasks to run at least one. It does not guarantee that tasks were added later.
  // It's for responsibility of the calling thread not to run tasks while waiting on
  // WaitForTasksToComplete.
  void WaitForTasksToComplete();
  unsigned FindMostFreeThread() const;

  unsigned thread_count() const { return thread_count_; }

  // Returns Maximum{the current queue size of a thread}.
  unsigned QueueSize() const;

  // Returns average queue delay of this taskpool in micro seconds.
  uint64 AverageDelayUsec() const;

  uint64 emplace_count() const { return emplace_count_;}
  uint64 wake_count() const { return wake_count_;}
private:

  struct RoutineConfig;
  static void * ThreadRoutine(void* config);

protected:
  void LaunchThreads();
  void JoinThreads();
  static void PrintSlowTask(uint32 ms_delay, MicrosecondsInt64 start);

  // We use this Interface in order to separate work pool base code from c++ template wrapping
  // logic.
  struct ThreadLocalInterface {
    virtual bool RunTask() = 0;
    virtual bool IsQueueEmpty() const = 0;
    virtual unsigned QueueSize() const = 0;
    virtual ~ThreadLocalInterface();

    uint64 queue_delay_jiffies = 0;  // total delay in jiffies (100usec).
    uint64 queue_delay_count = 0;
  };

  // Makes the task pool thread safe on writes from multiple threads.
  // Please note that write operation (TryRunTask) does not scale well with lots of writing threads
  // and this class is not designed for multiple threads writing into it.
  std::mutex emplace_mutex_;
  std::string base_name_;
  std::atomic_bool start_cancel_;
  unsigned per_thread_capacity_, thread_count_;
  uint64 emplace_count_ = 0, wake_count_ = 0;

  struct ThreadInfo {
    struct Data {
      pthread_t thread_id = 0;
      folly::EventCount ev_non_empty, ev_task_finished;

      std::atomic_bool has_tasks;
    } d;

    ThreadInfo() {
      d.has_tasks = false;
    }

    ThreadInfo(const ThreadInfo&) {}  // copy-ctor to allow vector resizing.

    void Join();

    bool Wake() {
      return d.ev_non_empty.notify();
    }

    // Eliminate false sharing.
    char padding[CACHE_LINE_PAD(sizeof(d))];
  };

  std::vector<ThreadInfo> threads_;
  std::vector<ThreadLocalInterface*> thread_interfaces_;
};

GENERATE_TYPE_MEMBER_WITH_DEFAULT(SharedDataOrEmptyTuple, SharedData, std::tuple<>);
template<typename T> using SharedDataOrEmptyTuple_t = typename SharedDataOrEmptyTuple<T>::type;

}  // namespace internal

template<typename Task,
         typename SharedTuple = internal::SharedDataOrEmptyTuple_t<Task> >
class SingleProducerTaskPool
    : public internal::SingleProducerTaskPoolBase {

  template<class T> using PCQ = folly::ProducerConsumerQueue<T>;
  using EmptyTupleTag = typename std::is_same<SharedTuple, std::tuple<>>::type;

  // When tasks do not have shared data.
  static void InitShared(std::true_type, Task& , SharedTuple&) {}

  // When tasks do have shared data.
  static void InitShared(std::false_type, Task& task, SharedTuple& s) {
    task.InitShared(s);
  }

  using TaskArgs = typename base::DecayedTupleFromParams<Task>::type;

  struct QueueItem {
    TaskArgs args;
    int64 ts;

    template<typename ...Args> QueueItem(Args&&... a) : args(std::forward<Args>(a)...) {
      ts = base::GetMonotonicJiffies();
    }
    QueueItem() {}
  };

  class QueueTaskImpl : public ThreadLocalInterface {
    PCQ<QueueItem> queue_;
    SharedTuple& shared_data_;
    Task task_;

    friend class SingleProducerTaskPool;

public:
    template<typename ...Args> QueueTaskImpl(unsigned size, SharedTuple& shared,
                                                   Args&&... args)
        : queue_(size), shared_data_(shared), task_(std::forward<Args>(args)...) {
      InitShared(EmptyTupleTag(), task_, shared);
    }

    virtual bool RunTask() {
      QueueItem item;
      if (!queue_.read(item)) return false;

      auto delta = base::GetMonotonicJiffies() - item.ts;
      queue_delay_jiffies += delta;
      ++queue_delay_count;

      // 10 Jiffies in ms.
      /*if (delta/10 > 20) {
        PrintSlowTask(delta/10, item.ts*100);
      }*/
      base::Apply(task_, std::move(item.args));
      return true;
    }

    bool IsQueueEmpty() const override { return queue_.isEmpty(); };
    unsigned QueueSize() const override  { return queue_.sizeGuess();}

    void Finalize() { task_.Finalize(); }
  };

public:
  // per_thread_capacity should be greater or equal to 2.
  SingleProducerTaskPool(const char* name, unsigned per_thread_capacity,
                         unsigned num_threads = 0)
      : internal::SingleProducerTaskPoolBase(name, per_thread_capacity, num_threads) {
  }

  ~SingleProducerTaskPool() {
  }

  // Semi-nonblocking routine. Returns true in case it succeeds to add the task to the pool.
  template<typename ...Args> bool TryRunTask(Args&&... args) {

    unsigned index = FindMostFreeThread();

    // Start critical section in order to provide safe enqueing from multiple producers.
    //
    emplace_mutex_.lock();
    if (++emplace_count_ > 20000) {
      emplace_count_ /= 2;
      wake_count_ /= 2;
    }

    QueueTaskImpl* t = reinterpret_cast<QueueTaskImpl*>(thread_interfaces_[index]);

    if (t->queue_.write(std::forward<Args>(args)...)) {
      // Close critical section.
      emplace_mutex_.unlock();

      auto& ti = threads_[index];
      wake_count_ += int(ti.Wake());

      return true;
    }

    // Close critical section.
    emplace_mutex_.unlock();
    return false;
  }

  template<typename ...Args> void RunTask(Args&&... args) {
    if (TryRunTask(std::forward<Args>(args)...))
      return;
    // We use TaskArgs to allow element by element initialization of the arguments.
    RunInline(std::forward<Args>(args)...);
  }

  template<typename ...Args> void RunInline(Args&&... args) {
    (*calling_thread_task_)(std::forward<Args>(args)...);
  }

  template<typename ...Args> void SetSharedData(Args&&... args) {
    shared_data_ = SharedTuple{std::forward<Args>(args)...};
  }

  template<typename ...Args> void Launch(Args&&... args) {
    if (calling_thread_task_)
      return;
    calling_thread_task_.reset(new Task(std::forward<Args>(args)...));
    InitShared(EmptyTupleTag{}, *calling_thread_task_, shared_data_);

    thread_interfaces_.resize(thread_count());
    for (auto& ti : thread_interfaces_) {
      ti = new QueueTaskImpl(per_thread_capacity_,
                                   shared_data_, std::forward<Args>(args)...);
    }
    LaunchThreads();
  }

  void Finalize() {
    if (!calling_thread_task_) return;
    calling_thread_task_->Finalize();
    for (auto ti : thread_interfaces_) {
      reinterpret_cast<QueueTaskImpl*>(ti)->Finalize();
    }
  }

 private:
  std::unique_ptr<Task> calling_thread_task_;
  SharedTuple shared_data_;
};

}  // namespace util

#endif  // _UTIL_SP_TASK_POOL_H
