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

#ifndef BASE_IO_MULTIPLEXER_H
#define BASE_IO_MULTIPLEXER_H

#include <map>
#include <vector>

#include "event.h"
#include "fd_event.h"

namespace base {

typedef struct FiredEvent {
  int fd_id;
  LtEvent event_mask;
  void reset() {
    fd_id = -1;
    event_mask = LT_EVENT_NONE;
  }
} FiredEvent;
using FiredEvList = std::vector<FiredEvent>;

class IOMux : public FdEvent::Watcher {
public:
  IOMux();
  virtual ~IOMux();

  // return active event count, and fired event store to out
  virtual int WaitingIO(FiredEvList& out, int32_t ms) = 0;

  virtual void AddFdEvent(FdEvent* fd_ev) = 0;
  virtual void DelFdEvent(FdEvent* fd_ev) = 0;

  // override for watch event update
  virtual void UpdateFdEvent(FdEvent* fd_ev) override = 0;

  /* may return NULL if removed by
   * previous InvokedEvent handler*/
  virtual FdEvent* FindFdEvent(int fd) = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(IOMux);
};

}  // namespace base
#endif
