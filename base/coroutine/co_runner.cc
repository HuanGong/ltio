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

#include "co_runner.h"

#include <base/closure/closure_task.h>
#include <base/memory/lazy_instance.h>
#include "bstctx_impl.hpp"
#include "glog/logging.h"

namespace {
std::once_flag backgroup_once;

constexpr size_t kMaxStealOneSched = 64;

constexpr int kMaxReuseCoroutineNumbersPerThread = 500;

base::TaskQueue g_remote;

using TaskList = std::vector<base::TaskBasePtr>;

// fill before run, clear after run
class OneRoundCtx final {
public:
  enum Type {
    TaskList = 0x01,
    TaskQueue = 0x02,
  };

  OneRoundCtx() { Reset(); };

  void Reset() {
    task_idx = 0;
    total_cnt = 0;
    tasks.clear();

    max_dequeue = 0;
    tsk_queue = nullptr;
  }

  bool PopTask(base::TaskBasePtr& task) {
    while (task_idx < total_cnt) {
      task = std::move(tasks[task_idx++]);
      if (task)
        return true;
    }
    while (max_dequeue > 0) {
      if (tsk_queue->try_dequeue(task)) {
        max_dequeue--;
        return true;
      }
      max_dequeue = 0;
    }
    return false;
  }

  size_t Remain() const { return (total_cnt - task_idx) + max_dequeue; }

  size_t PumpFromQueue(base::TaskQueue& tq, size_t batch) {
    tq.try_dequeue_bulk(std::back_inserter(tasks), batch);
    return total_cnt = tasks.size();
  }

  size_t SwapFromTaskList(::TaskList& list) {
    tasks.swap(list);
    return total_cnt = tasks.size();
  }

  size_t WithLimitedDequeue(base::TaskQueue* tq, size_t max) {
    tsk_queue = tq;
    max_dequeue = max;
    return max_dequeue;
  }

private:
  size_t task_idx;
  size_t total_cnt;
  ::TaskList tasks;

  size_t max_dequeue;
  base::TaskQueue* tsk_queue;
  DISALLOW_COPY_AND_ASSIGN(OneRoundCtx);
};

}  // namespace

namespace base {

class CoroRunnerImpl;
thread_local base::CoroRunnerImpl* _runner = NULL;

template<typename Rec>
fcontext_transfer_t context_exit(fcontext_transfer_t from) noexcept {
  Rec* rec = static_cast<Rec*>(from.data);
  return from;
}

/* Recorder is a coroutine control struct that represent
 * some coro_context's infomation use for context switching
 *
 * Recorder::RecordCtx(fcontext_transfer_t from)
 * Recorder::CoroMain();
 * Recorder::ExitCurrent();
 * */
template<typename Rec>
void context_entry(fcontext_transfer_t from) noexcept {
    Rec* rec = static_cast<Rec*>(from.data);
    CHECK(rec);
    rec->RecordCtx(from);
LOOP:
    rec->CoroMain();

    rec->ExitCurrent();

    /* this make a coroutine can reuse without reset callstack, just
     * call co->Resume() to reuse this coro context, but!, after testing
     * reset a call stack can more fast than reuse current stack status
     * see: Coroutine::Reset(fn), so when use parking coro, we reset it,
     * instead of just call coro->Resume(); about 1/15 perfomance improve*/
goto LOOP;
}

class CoroRunnerImpl : public CoroRunner {
public:
  CoroRunnerImpl() : CoroRunner() { _runner = this; }

  ~CoroRunnerImpl() {}

  void RecordCtx(fcontext_transfer_t co_zero) {
    main_->co_ctx_.ctx = co_zero.ctx;
  }

  void CoroMain() {
    do {
      TaskBasePtr task;
      while (run_.PopTask(task)) {
        task->Run();
      }
      task.reset(); //ensure task destruction
    } while (0);
  }

  void ExitCurrent() {
    GC(current_);
    switch_context(main_);
    //main_->ResumeWith(context_exit<CoroRunnerImpl>);
  }

  /* a callback function using for resume a kPaused coroutine */

  /* 如果本身是在一个子coro里面,
   * 需要在重新将resume调度到MainCoro内
   * 如果本身是MainCoro，则直接进行切换.
   * 如果不是在调度的线程里,则调度到目标Loop去Resume*/
  void Resume(std::weak_ptr<Coroutine>& weak, uint64_t id) {
    /* do resume a coroutine from main_coro*/
    auto do_resume = [weak, id, this]() {
      RefCoroutine coro = weak.lock();
      if (coro && coro->ResumeID() == id) {
        return switch_context(coro.get());
      }
      LOG(ERROR) << "already resumed, want:" << id
                 << " real:" << coro->ResumeID();
    };
    if (bind_loop_->IsInLoopThread()) {
      ctrl_task_.push_back(NewClosure(do_resume));
    } else {
      ready_queue_.enqueue(NewClosure(std::move(do_resume)));
    }
    bind_loop_->WakeUpIfNeeded();
  }

  /* append to a cached-list waiting for gc */
  void GC(Coroutine* co) {
    // free list here to improve perfomance and shared from threads
    if (parking_coros_.size() < max_parking_count_) {
      parking_coros_.push_back(co);
      return;
    }
    cached_gc_coros_.emplace_back(co);
  };

  OneRoundCtx run_;

  TaskList ctrl_task_;
};  // end CoroRunnerImpl

// static
CoroRunner& CoroRunner::instance() {
  static thread_local CoroRunnerImpl runner;
  return runner;
}

// static
MessageLoop* CoroRunner::backgroup() {
  static LazyInstance<MessageLoop> loop = LAZY_INSTANCE_INIT;
  std::call_once(backgroup_once, [&]() {
    loop->SetLoopName("co_background");
    loop->Start();
    // initialized a runner binded to this loop
    RegisteRunner(loop.ptr());
  });
  return loop.ptr();
}

// static
void CoroRunner::RegisteRunner(MessageLoop* l) {
  if (l->IsInLoopThread()) {
    instance();
    return;
  }
  l->PostTask(FROM_HERE, &CoroRunner::instance);
}

// static
bool CoroRunner::Yieldable() {
  return _runner ? (!_runner->in_main_coro()) : false;
}

// static
void CoroRunner::Sched(int64_t us) {
  if (!Yieldable()) {
    return;
  }
  Coroutine* coro = _runner->current_;
  if (_runner->task_count() == 0 || coro->ElapsedTime() < us) {
    return;
  }
  MessageLoop* loop = _runner->bind_loop_;
  CHECK(loop->PostTask(NewClosure(MakeResumer())));

  // yield to main coro; but without call yield avoid check yield able
  _runner->switch_context(_runner->main_);
}

// static
void CoroRunner::Yield() {
  CHECK(Yieldable());
  // add current_ into a linkedlist to tracking
  // system-range coroutine status
  _runner->switch_context(_runner->main_);
}

// static
void CoroRunner::Sleep(uint64_t ms) {
  CHECK(Yieldable()) << "co_sleep only work on coro context";
  MessageLoop* loop = _runner->bind_loop_;
  CHECK(loop->PostDelayTask(NewClosure(MakeResumer()), ms));
  _runner->switch_context(_runner->main_);
}

// static
LtClosure CoroRunner::MakeResumer() {
  if (!Yieldable()) {
    LOG(WARNING) << "make resumer on main_coro";
    return nullptr;
  }
  auto weak_coro = _runner->current_->AsWeakPtr();
  uint64_t resume_id = _runner->current_->ResumeID();
  return std::bind(&CoroRunnerImpl::Resume, _runner, weak_coro, resume_id);
}

// static publish a task for any runner
bool CoroRunner::Publish(TaskBasePtr&& task) {
  return g_remote.enqueue(std::move(task));
}

CoroRunner::CoroRunner()
  : bind_loop_(MessageLoop::Current()),
    max_parking_count_(kMaxReuseCoroutineNumbersPerThread) {
  CHECK(bind_loop_);
  bind_loop_->InstallPersistRunner(this);
  current_ = main_ = Coroutine::New()->SelfHolder();
  VLOG(GLOG_VINFO) << "CoroutineRunner@" << this << " initialized";
}

CoroRunner::~CoroRunner() {
  FreeCoros();
  for (auto coro : parking_coros_) {
    coro->ReleaseSelfHolder();
  }
  parking_coros_.clear();
  main_->ReleaseSelfHolder();
  current_ = nullptr;
  VLOG(GLOG_VINFO) << "CoroutineRunner@" << this << " gone";
}

void CoroRunner::ScheduleTask(TaskBasePtr&& tsk) {
  if (bind_loop_->IsInLoopThread()) {
    sched_tasks_.push_back(std::move(tsk));
  } else {
    remote_sched_.enqueue(std::move(tsk));
  }
}

void CoroRunner::AppendTask(TaskBasePtr&& task, bool front) {
  sched_tasks_.push_back(std::move(task));
}

void CoroRunner::LoopGone(MessageLoop* loop) {
  bind_loop_ = nullptr;
  LOG(ERROR) << "loop gone, CoroRunner feel not good";
}

// P(CoroRunner) take a M(Corotine) do work(TaskBasePtr)
// after here, any nested task will be handled next loop
void CoroRunner::Run() {
  VLOG_EVERY_N(GLOG_VTRACE, 1000) << " coroutine runner enter";
  CHECK(current_->IsCoroZero()) << current_->CoInfo();

  // ====>    resume ready coros
  TaskList ctrl_task = std::move(_runner->ctrl_task_);
  _runner->ctrl_task_.clear();
  do {
    for (TaskBasePtr& task : ctrl_task) {
      task->Run();
    }
    ctrl_task.clear();
  } while (ready_queue_.try_dequeue_bulk(std::back_inserter(ctrl_task), 1024));

  // ====> run scheduled task
  _runner->run_.Reset();
  _runner->run_.WithLimitedDequeue(&remote_sched_, remote_sched_.size_approx());
  while (_runner->run_.Remain()) {
    switch_context(Spawn());
  }
  FreeCoros();

  // ====>    run scheduled nested tasks
  _runner->run_.Reset();
  _runner->run_.SwapFromTaskList(sched_tasks_);
  while (_runner->run_.Remain()) {
    switch_context(Spawn());
  }
  FreeCoros();

  // ====>    steal remote task
  _runner->run_.Reset();
  int max_steal_size = std::max(size_t(128), g_remote.size_approx());
  _runner->run_.WithLimitedDequeue(&g_remote, max_steal_size);
  while (_runner->run_.Remain()) {
    switch_context(Spawn());
  }
  FreeCoros();
}

//NOTE: ensure this function without any heap memory inside this
void CoroRunner::switch_context(Coroutine* next) {
  DCHECK(next != current_);

  current_ = next;
  next->Resume();
}

size_t CoroRunner::task_count() const {
  return sched_tasks_.size() +
    g_remote.size_approx() +
    remote_sched_.size_approx() +
    ready_queue_.size_approx();
}

Coroutine* CoroRunner::Spawn() {
  Coroutine* coro = nullptr;
  while (parking_coros_.size()) {
    coro = parking_coros_.front();
    parking_coros_.pop_front();
    if (coro) {
      // reset the coro context stack make next time resume more fast
      // bz when context switch can with out copy any data, just jump
      // coro->Reset(CoroRunnerImpl::RunnerMain, this);
      return coro;
    }
  }
  return Coroutine::New(context_entry<CoroRunnerImpl>, this)->SelfHolder();
}

/* release the coroutine memory */
void CoroRunner::FreeCoros() {
  for (auto& coro : cached_gc_coros_) {
    coro->ReleaseSelfHolder();
  }
  cached_gc_coros_.clear();
}

}  // namespace base
