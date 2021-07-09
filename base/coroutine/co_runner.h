/*
 * Copyright 2021 <name of copyright holder>
 * Author: Huan.Gong <gonghuan.dev@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef BASE_COROUTINE_SCHEDULER_H_H_
#define BASE_COROUTINE_SCHEDULER_H_H_

#include <cinttypes>
#include <vector>

#include <base/base_micro.h>
#include <base/message_loop/message_loop.h>

namespace base {

/*
 * same thing like go language Goroutine CSP model,
 * But with some aspect diffirence
 *
 * G: CoroTask
 * M: Coroutine
 * P: CoroRunner <-bind-> MessageLoop
 *
 * __go is a utility for schedule a CoroTask(G) Bind To
 * Current(can also bind to a specific native thread) CoroRunner(P)
 *
 * when invoke the G(CoroTask), P(CoroRunner) will choose a suitable
 * M to excute the task
 * */
class Coroutine;

class CoroRunner : public PersistRunner {
public:

  typedef struct _go {
    _go(const char* func, const char* file, int line)
      : location_(func, file, line),
        target_loop_(MessageLoop::Current()) {}

    /* specific a loop for this task*/
    inline _go& operator-(MessageLoop* loop) {
      target_loop_ = loop;
      return *this;
    }

    /* schedule a corotine task to remote_queue_, and weakup a
     * target loop to run this task, but this task can be invoke
     * by other loop, if you want task invoke in specific loop
     * use `CO_GO &loop << Functor`
     * */
    template <typename Functor>
    inline void operator-(Functor func) {
      CoroRunner::Publish(CreateClosure(location_, func));
      loop()->WakeUpIfNeeded();
    }

    // here must make sure all things wrapper(copy) into closue,
    // becuase __go object will destruction before task closure run
    template <typename Functor>
    inline void operator<<(Functor closure_fn) {
      //NOTE: this is more slow than publish a task
      auto func = [closure_fn](const Location& location) {
        CoroRunner& runner = CoroRunner::instance();
        runner.AppendTask(CreateClosure(location, closure_fn));
      };
      loop()->PostTask(location_, func, location_);
    }

    inline MessageLoop* loop() {
      return target_loop_ ? target_loop_ : CoroRunner::backgroup();
    }
    Location location_;
    MessageLoop* target_loop_ = nullptr;
  } _go;
public:
  friend class _go;

  // run other task if current task occupy cpu too long
  // if task running time > us, runner will give up cpu
  // and then run a task in pending queue
  static void Sched(int64_t us = 5000);

  static void Yield();

  static bool Yieldable();

  static LtClosure MakeResumer();

  static void Sleep(uint64_t ms);

  static bool Publish(TaskBasePtr&& task);

  /* here two ways register as runner worker
   * 1. implicit call CoroRunner::instance()
   * 2. call RegisteRunner manually
   * */
  static void RegisteRunner(MessageLoop*);

public:
  virtual ~CoroRunner();

  /* override from MessageLoop::PersistRunner
   * load(bind) task(cargo) to coroutine(car) and run(transfer)
   * */
  void Run() override;

  /* override from MessageLoop::PersistRunner*/
  void LoopGone(MessageLoop* loop) override;

  void ScheduleTask(TaskBasePtr&& tsk) override;

  void AppendTask(TaskBasePtr&& task, bool front = false);

  size_t task_count() const;

  /* from stash list got a coroutine or create new one*/
  Coroutine* Spawn();

  void FreeCoros();
protected:
  static CoroRunner& instance();

  static MessageLoop* backgroup();

  CoroRunner();

  /* switch call stack from different coroutine*/
  void switch_context(Coroutine* next);

  bool in_main_coro() const { return current_ == main_; }

  Coroutine* main_;
  Coroutine* current_;

  MessageLoop* bind_loop_;

  // resume task pending to this
  TaskQueue ready_queue_;

  // scheduled task list
  TaskQueue remote_sched_;
  // nested task
  std::vector<TaskBasePtr> sched_tasks_;

  /* every thread max resuable coroutines*/
  size_t max_parking_count_;
  std::list<Coroutine*> parking_coros_;

  std::vector<Coroutine*> cached_gc_coros_;
  DISALLOW_COPY_AND_ASSIGN(CoroRunner);
};

}  // namespace base

// NOTE: co_yield,co_await,co_resume has been a part of keywords in c++20,
// crying!!! so rename co_xxx.... to avoid conflicting
#define CO_GO ::base::CoroRunner::_go(__FUNCTION__, __FILE__, __LINE__) -
#define CO_YIELD ::base::CoroRunner::Yield()
#define CO_RESUMER ::base::CoroRunner::MakeResumer()
#define CO_CANYIELD ::base::CoroRunner::Yieldable()
#define CO_SLEEP(ms) ::base::CoroRunner::Sleep(ms)
#define __co_sched_here__ ::base::CoroRunner::Sched();

// sync task in coroutine context
#define CO_SYNC(func)                                                 \
  do {                                                                \
    auto loop = ::base::MessageLoop::Current();                       \
    loop->PostTask(                                                   \
        ::base::CreateTaskWithCallback(FROM_HERE, func, CO_RESUMER)); \
    ::base::CoroRunner::Yield();                                      \
  } while (0)

#endif
