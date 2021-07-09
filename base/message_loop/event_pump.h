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

#ifndef BASE_EVENT_PUMP_H_
#define BASE_EVENT_PUMP_H_

#include <inttypes.h>

#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "fd_event.h"
#include "io_multiplexer.h"
#include "timeout_event.h"

namespace base {

class IOMux;

typedef struct timeouts TimeoutWheel;
typedef std::unique_ptr<TimeoutWheel> TimeoutWheelPtr;

typedef std::function<void()> QuitClosure;
typedef std::shared_ptr<FdEvent> RefFdEvent;
typedef std::vector<TimeoutEvent*> TimerEventList;

class PumpDelegate {
public:
  virtual void RunNestedTask(){};
  virtual uint64_t PumpTimeout() { return 5; };  // ms
  virtual void RunTimerClosure(const TimerEventList&){};
};

/* pump fd event and timeout event and pass them to handler by delegate
 * interface*/
class EventPump {
public:
  EventPump();
  EventPump(PumpDelegate* d);
  ~EventPump();

  void Run();

  void RunOnce();

  void PrepareRun();

  bool Running() { return running_; }

  void Quit() { running_ = false; };

  bool InstallFdEvent(FdEvent* fd_event);

  bool RemoveFdEvent(FdEvent* fd_event);

  void AddTimeoutEvent(TimeoutEvent* timeout_ev);

  void RemoveTimeoutEvent(TimeoutEvent* timeout_ev);

  bool IsInLoop() const;

  uint64_t LoopID() const { return loop_id_;}

  void SetLoopId(uint64_t id);

  static uint64_t CurrentThreadLoopID();
protected:
  /* update the time wheel mononic time and get all expired
   * timeoutevent will be invoke and re shedule if was repeated*/
  void ProcessTimerEvent();

  void InvokeFiredEvent(FiredEvent* evs, int count);
  /* return the minimal timeout for next poll
   * return 0 when has event expired,
   * return default_timeout when no other timeout provided*/
  timeout_t NextTimeout();

  /* finalize TimeoutWheel and delete Life-Ownered timeoutEvent*/
  void InitializeTimeWheel();
  void FinalizeTimeWheel();

  /*calculate abs timeout time and add to timeout wheel*/
  void add_timer_internal(uint64_t now_us, TimeoutEvent* event);

private:
  PumpDelegate* delegate_;
  bool running_;

  std::unique_ptr<IOMux> io_mux_;

#if 0
  // replace by new timeout_wheel for more effective perfomance [ O(1) ]
  //TimerTaskQueue timer_queue_;
#endif

  int32_t timestamp_;

  uint64_t loop_id_ = 0;

  std::vector<FiredEvent> fired_list_;

  TimeoutWheel* timeout_wheel_ = nullptr;
};

}  // namespace base
#endif
