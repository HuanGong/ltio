
#include "co_loop.h"
#include "base/closure/closure_task.h"
#include "base/message_loop/io_mux_epoll.h"
#include "base/message_loop/linux_signal.h"
#include "fcontext/fcontext.h"

namespace base {

CoLoop::CoLoop() {
  InitializeTimeWheel();

  io_mux_.reset(new base::IOMuxEpoll());
}

CoLoop::~CoLoop() {
  FinalizeTimeWheel();
}

bool CoLoop::PostTask(TaskBasePtr&& task) {
  runner_->ScheduleTask(std::move(task));
  return true;
}

bool CoLoop::PostDelayTask(TaskBasePtr&&, int64_t ms) {
  return false;
}

bool CoLoop::InLoop() const {
  return false;
}

timeout_t CoLoop::NextTimeout() {
  static const uint64_t default_timeout_ms = 50;
  if (runner_->task_count()) {
    return 0;
  }
  ::timeouts_update(time_wheel_, time_ms());
  if (::timeouts_expired(time_wheel_)) {
    return 0;
  }
  timeout_t ms = default_timeout_ms;
  if (::timeouts_pending(time_wheel_)) {
    ms = std::min(ms, ::timeouts_timeout(time_wheel_));
  }
  return ms;
}

//static
void CoLoop::ThreadMain() {
  IgnoreSigPipeSignalOnCurrentThread();

  int ms = 0;
  std::vector<FiredEvent> active_list(65535);
  //init setup
  while(true) {

    ms = NextTimeout();
    if (runner_->task_count()) {
      ms = 0;
    }
    int count = io_mux_->WaitingIO(active_list, ms);

    ProcessTimerEvent();

    InvokeFiredEvent(&active_list[0], count);

    runner_->Run(); //run schedule task
  }
  active_list.clear();
  //clean up
}

void CoLoop::InvokeFiredEvent(FiredEvent* evs, int count) {
  for (int i = 0; i < count; i++) {
    FiredEvent ev = evs[i];

    auto func = [this, ev]() {
      FdEvent* fd_event = io_mux_->FindFdEvent(ev.fd_id);
      if (fd_event) {
        fd_event->SetActivedEvent(ev.event_mask);
        fd_event->HandleEvent(ev.event_mask);
        return;
      }
      LOG(ERROR) << "event removed previously, fd:" << ev.fd_id;
    };
    runner_->AppendTask(NewClosure(std::move(func)));
    ev.reset();
  }
}

void CoLoop::ProcessTimerEvent() {
  ::timeouts_update(time_wheel_, time_ms());

  Timeout* expired = NULL;
  while (NULL != (expired = timeouts_get(time_wheel_))) {

    TimeoutEvent* timeout_ev = static_cast<TimeoutEvent*>(expired);

    if (timeout_ev->IsRepeated()) {
      runner_->AppendTask(NewClosure([timeout_ev](){
        timeout_ev->Invoke();
      }));
      continue;
    }

    // one shot timeout_ev
    runner_->AppendTask(std::move(timeout_ev->MoveInvoker()));
    if (timeout_ev->DelAfterInvoke()) {
      delete timeout_ev;
    }
  }
}

void CoLoop::InitializeTimeWheel() {
  CHECK(time_wheel_ == NULL);
  int err = 0;
  time_wheel_ = ::timeouts_open(TIMEOUT_mHZ, &err);
  CHECK(err == 0);
  ::timeouts_update(time_wheel_, time_ms());
}

void CoLoop::FinalizeTimeWheel() {
  CHECK(time_wheel_ != NULL);

  std::vector<TimeoutEvent*> to_be_delete;
  Timeout* to = NULL;
  TIMEOUTS_FOREACH(to, time_wheel_, TIMEOUTS_ALL) {
    TimeoutEvent* toe = static_cast<TimeoutEvent*>(to);
    ::timeouts_del(time_wheel_, to);
    if (toe->DelAfterInvoke()) {
      to_be_delete.push_back(toe);
    }
  }

  for (TimeoutEvent* toe : to_be_delete) {
    delete toe;
  }
  to_be_delete.clear();
  ::timeouts_close(time_wheel_);
  time_wheel_ = NULL;
}

}
