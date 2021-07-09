#ifndef _LT_BASE_TIMER_TASK_HELPER_H_
#define _LT_BASE_TIMER_TASK_HELPER_H_

#include "base/closure/closure_task.h"
#include "base/message_loop/event_pump.h"

namespace base {
class TimerTaskHelper : public TaskBase {
public:
  TimerTaskHelper(TaskBasePtr task, EventPump* pump, uint64_t ms);

  void Run() override;

private:
  TaskBasePtr timeout_fn_;
  EventPump* event_pump_;
  const uint64_t delay_ms_;
  const uint64_t schedule_time_;
};

TaskBasePtr NewTimerTaskHelper(TaskBasePtr task, EventPump* pump, uint64_t ms);

}  // namespace base

#endif
