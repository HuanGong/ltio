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

#ifndef _BASE_MSG_EVENT_LOOP_H_H
#define _BASE_MSG_EVENT_LOOP_H_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include "base/base_constants.h"
#include "base/closure/closure_task.h"
#include "base/closure/location.h"
#include "base/memory/scoped_ref_ptr.h"
#include "base/memory/spin_lock.h"
#include "base/queue/task_queue.h"
#include "event_pump.h"
#include "fd_event.h"
#include "glog/logging.h"

namespace base {

enum LoopState { ST_INITING = 1, ST_STARTED = 2, ST_STOPED = 3 };

class MessageLoop;
class PersistRunner {
public:
  virtual void Run() = 0;
  virtual void LoopGone(MessageLoop* loop){};
};

class MessageLoop : public PumpDelegate, public FdEvent::Handler {
public:
  static MessageLoop* Current();

  typedef enum {
    TaskTypeDefault = 0,
    TaskTypeReply = 1,
    TaskTypeCtrl = 2
  } ScheduledTaskType;

  MessageLoop();
  MessageLoop(const std::string& name);
  virtual ~MessageLoop();

  static uint64_t GenLoopID();

  bool PostTask(TaskBasePtr&&);

  bool PostDelayTask(TaskBasePtr, uint32_t milliseconds);

  template <class Functor>
  bool PostTask(const Location& location, const Functor& closure) {
    return PostTask(CreateClosure(location, closure));
  }

  template <typename Functor, typename... Args>
  bool PostTask(const Location& location, Functor&& functor, Args&&... args) {
    return PostTask(CreateClosure(location, std::bind(functor, args...)));
  }

  /* Task will run in target loop thread,
   * reply will run in Current() loop if it exist,
   * otherwise the reply task run in target loop thread too*/
  template <class T, class R>
  bool PostTaskAndReply(const Location& location,
                        const T& task,
                        const R& reply) {
    return PostTaskAndReply(location, task, reply, this);
  }

  template <class T, class R>
  bool PostTaskAndReply(const Location& location,
                        const T& task,
                        const R& reply,
                        MessageLoop* reply_loop) {
    if (reply_loop == nullptr) {
      return PostTask(CreateTaskWithCallback(location, task, reply));
    }
    auto wrap_reply = [=]() -> void {
      reply_loop->PostTask(base::CreateClosure(location, reply));
    };
    return PostTask(CreateTaskWithCallback(location, task, wrap_reply));
  }

  void Start();
  bool IsInLoopThread() const;

  void WakeUpIfNeeded();
  void InstallPersistRunner(PersistRunner* runner);
  // t: millsecond for giveup cpu for waiting
  void WaitLoopEnd(int32_t t = 1);
  void SetLoopName(std::string name);
  const std::string& LoopName() const { return loop_name_; }

  void QuitLoop();
  EventPump* Pump() { return &event_pump_; }

  uint64_t PumpTimeout() override;

private:
  void ThreadMain();
  void SetThreadNativeName();

  void RunCommandTask(ScheduledTaskType t);

  size_t PendingTasksCount() const;

  // nested task: post another task in current loop
  // override from pump for nested task;
  void RunNestedTask() override;
  void RunScheduledTask();
  void RunTimerClosure(const TimerEventList&) override;

  int Notify(int fd, const void* data, size_t count);

  bool HandleRead(FdEvent* fd_event) override;
  bool HandleWrite(FdEvent* fd_event) override;
  bool HandleError(FdEvent* fd_event) override;
  bool HandleClose(FdEvent* fd_event) override;

private:
  std::atomic<int> status_;
  std::atomic_flag running_;

  std::mutex start_stop_lock_;
  std::condition_variable cv_;

  std::string loop_name_;
  std::unique_ptr<std::thread> thread_ptr_;

  RefFdEvent task_event_;
  std::atomic_flag notify_flag_;

  ConcurrentTaskQueue scheduled_tasks_;
  std::vector<TaskBasePtr> in_loop_tasks_;

  std::list<PersistRunner*> persist_runner_;

  // pipe just use for loop control
  int wakeup_pipe_in_ = -1;
  RefFdEvent wakeup_event_;

  EventPump event_pump_;
  DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

}  // namespace base
#endif
