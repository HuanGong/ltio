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

#include "timeout_event.h"

namespace base {

// static
TimeoutEvent* TimeoutEvent::CreateOneShot(uint64_t ms,
                                          bool delelte_after_invoke) {
  TimeoutEvent* toe = new TimeoutEvent(ms, false);
  toe->del_after_invoke_ = delelte_after_invoke;
  return toe;
}

TimeoutEvent::TimeoutEvent(uint64_t ms, bool repeat) {
  ::timeout_init((Timeout*)this, (repeat ? TIMEOUT_INT : TIMEOUT_ABS));
  this->interval = ms;
}

TimeoutEvent::~TimeoutEvent() {
  CHECK(!IsAttached());
}

void TimeoutEvent::UpdateInterval(int64_t ms) {
  interval = ms;
}

base::TaskBasePtr TimeoutEvent::MoveInvoker() {
  return std::move(timer_handler_);
}

void TimeoutEvent::Invoke() {
  if (timer_handler_) {
    timer_handler_->Run();
  }
}

void TimeoutEvent::InstallTimerHandler(TaskBasePtr&& h) {
  timer_handler_ = std::move(h);
}

}  // namespace base
