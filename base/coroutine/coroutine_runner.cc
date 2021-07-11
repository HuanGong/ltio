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

#include "coroutine_runner.h"

#include <base/closure/closure_task.h>
#include <base/memory/lazy_instance.h>
#include "bstctx_impl.hpp"
#include "glog/logging.h"

namespace {
std::once_flag backgroup_once;

const size_t kMaxStealOneSched = 64;

constexpr int kMaxReuseCoroutineNumbersPerThread = 500;

base::ConcurrentTaskQueue remote_queue_;

}  // namespace

namespace base {

class CoroRunnerImpl;
thread_local base::CoroRunnerImpl* _runner = NULL;

class CoroRunnerImpl : public CoroRunner {
public:
  CoroRunnerImpl() : CoroRunner() { _runner = this; }

  static void CoroMain(fcontext_transfer_t from) {
    Coroutine* coro = (Coroutine*)from.data;
    _runner->main_->co_ctx_ = from;
  AGAIN:
    do {
      TaskBasePtr task;
      while (_runner->PickTask(task)) {
        task->Run();
      }
      task.reset();
      _runner->no_more_task_ = true;
      _runner->GC(coro);
      _runner->Yield();
    } while (0);
    /* this make a coroutine can reuse without reset callstack, just
     * call co->Resume() to reuse this coro context, but!, after testing
     * reset a call stack can more fast than reuse current stack status
     * see: Coroutine::Reset(fn), so when use parking coro, we reset it,
     * instead of just call coro->Resume(); about 1/15 perfomance improve*/
    goto AGAIN;
  }

  /* a callback function using for resume a kPaused coroutine */

  /* 如果本身是在一个子coro里面,
   * 需要在重新将resume调度到MainCoro内
   * 如果本身是MainCoro，则直接进行切换.
   * 如果不是在调度的线程里,则调度到目标Loop去Resume*/
  void Resume(std::weak_ptr<Coroutine>& coro, uint64_t id) {
    if (bind_loop_->IsInLoopThread() && in_main_coro()) {
      return do_resume(coro, id);
    }
    auto f = std::bind(&CoroRunnerImpl::do_resume, this, coro, id);
    CHECK(bind_loop_->PostTask(NewClosure(f)));
  }

  /* do resume a coroutine from main_coro*/
  void do_resume(WeakCoroutine& weak, uint64_t id) {
    DCHECK(in_main_coro());
    RefCoroutine coro = weak.lock();
    if (!coro) {
      return;
    }
    if (coro->ResumeID() == id) {
      return switch_context(coro.get());
    }
    LOG(ERROR) << "already resumed, want:" << id
               << " real:" << coro->ResumeID();
  }
};  // end CoroRunnerImpl

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
    RegisteAsCoroWorker(loop.ptr());
  });
  return loop.ptr();
}

// static
void CoroRunner::RegisteAsCoroWorker(MessageLoop* l) {
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
  _runner->switch_context(_runner->main_);
}

// static
void CoroRunner::Sleep(uint64_t ms) {
  CHECK(Yieldable()) << "co_sleep only work on coro context";
  MessageLoop* loop = _runner->bind_loop_;
  CHECK(loop->PostDelayTask(NewClosure(MakeResumer()), ms));
  Yield();
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

// static
bool CoroRunner::ScheduleTask(TaskBasePtr&& task) {
  return remote_queue_.enqueue(std::move(task));
}

CoroRunner::CoroRunner()
  : bind_loop_(MessageLoop::Current()),
    max_parking_count_(kMaxReuseCoroutineNumbersPerThread) {
  CHECK(bind_loop_);
  main_ = Coroutine::New()->SelfHolder();
  current_ = main_;

  bind_loop_->InstallPersistRunner(this);
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

bool CoroRunner::PickTask(TaskBasePtr& task) {
  if (coro_tasks_.size()) {
    task = std::move(coro_tasks_.front());
    coro_tasks_.pop_front();
    return true;
  }
  if (remote_cnt_ < kMaxStealOneSched && remote_queue_.try_dequeue(task)) {
    remote_cnt_++;
    return true;
  }
  return false;
}

void CoroRunner::AppendTask(TaskBasePtr&& task) {
  sched_tasks_.push_back(std::move(task));
}

void CoroRunner::LoopGone(MessageLoop* loop) {
  bind_loop_ = nullptr;
  LOG(ERROR) << "loop gone, CoroRunner feel not good";
}

void CoroRunner::Run() {
  remote_cnt_ = 0;
  no_more_task_ = false;
  VLOG_EVERY_N(GLOG_VTRACE, 1000) << " coroutine runner enter";
  // P(CoroRunner) take a M(Corotine) do work(TaskBasePtr)
  // after here, any nested task will be handled next loop
  coro_tasks_.swap(sched_tasks_);
  while (coro_tasks_.size()) {
    switch_context(Spawn());
  }

  while (!no_more_task_) {
    switch_context(Spawn());
  }
  FreeCoros();
}

void CoroRunner::switch_context(Coroutine* next) {
  CHECK(next != current_) << "bad context switch";
  current_ = next;
  next->Resume();
}

size_t CoroRunner::task_count() const {
  return coro_tasks_.size() + remote_queue_.size_approx();
}

Coroutine* CoroRunner::Spawn() {
  Coroutine* coro = nullptr;
  while (parking_coros_.size()) {
    coro = parking_coros_.front();
    parking_coros_.pop_front();
    if (coro) {
      // reset the coro context stack make next time resume more fast
      // bz when context swich can with out copy any data, just jump
      // coro->Reset(CoroRunnerImpl::CoroMain);
      return coro;
    }
  }
  return Coroutine::New(CoroRunnerImpl::CoroMain)->SelfHolder();
}

void CoroRunner::GC(Coroutine* co) {
  if (parking_coros_.size() < max_parking_count_) {
    parking_coros_.push_back(co);
    return;
  }
  cached_gc_coros_.emplace_back(co);
};

void CoroRunner::FreeCoros() {
  for (auto& coro : cached_gc_coros_) {
    coro->ReleaseSelfHolder();
  }
  cached_gc_coros_.clear();
}

}  // namespace base
