#include "timer_task_helper.h"
#include "base/base_constants.h"

namespace base {

TaskBasePtr NewTimerTaskHelper(TaskBasePtr task, EventPump* pump, uint64_t ms) {
  return std::make_unique<TimerTaskHelper>(std::move(task), pump, ms);
}

TimerTaskHelper::TimerTaskHelper(TaskBasePtr task, EventPump* pump, uint64_t ms)
  : TaskBase(task->TaskLocation()),
    timeout_fn_(std::move(task)),
    event_pump_(pump),
    delay_ms_(ms),
    schedule_time_(time_ms()) {}

void TimerTaskHelper::Run() {
  uint64_t has_passed_time = time_ms() - schedule_time_;
  int64_t new_delay_ms = delay_ms_ - has_passed_time;
  if (new_delay_ms <= 0) {
    return timeout_fn_->Run();
  }
  VLOG(GLOG_VTRACE) << "Re-Schedule timer " << new_delay_ms << " ms";
  TimeoutEvent* timeout_ev = TimeoutEvent::CreateOneShot(new_delay_ms, true);
  timeout_ev->InstallTimerHandler(std::move(timeout_fn_));
  event_pump_->AddTimeoutEvent(timeout_ev);
}

}  // namespace base
