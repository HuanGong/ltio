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

#ifndef BASE_TimeoutEvent_EVENT_H_
#define BASE_TimeoutEvent_EVENT_H_

#include <cinttypes>

#include <base/closure/closure_task.h>
#include <base/time/timestamp.h>

extern "C" {
#include <thirdparty/timeout/timeout.h>
}

typedef struct timeout Timeout;
namespace base {

class TimeoutEvent : public Timeout {
public:
  static TimeoutEvent* CreateOneShot(uint64_t ms, bool delelte_after_invoke);

  TimeoutEvent(uint64_t ms, bool repeat);
  ~TimeoutEvent();

  void Invoke();

  // use on coro task
  base::TaskBasePtr MoveInvoker();

  void UpdateInterval(int64_t ms);
  void InstallTimerHandler(TaskBasePtr&& h);
  bool IsRepeated() const { return flags & TIMEOUT_INT; }
  inline bool DelAfterInvoke() const { return del_after_invoke_; }
  inline bool IsAttached() const { return pending != NULL; }
  inline uint64_t Interval() const { return interval; }
  inline uint64_t IntervalMicroSecond() const { return interval * 1000; }

private:
  bool use_coro_ = false;
  bool del_after_invoke_ = false;
  TaskBasePtr timer_handler_;
};

}  // namespace base
#endif
