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

#ifndef BASE_LT_EVENT_H
#define BASE_LT_EVENT_H

#include <sys/epoll.h>
#include <sys/poll.h>

#include <functional>
#include <memory>

#include "base/base_micro.h"
#include "base/queue/double_linked_list.h"

namespace base {

enum LtEv {
  LT_EVENT_NONE = 0x0000,
  LT_EVENT_READ = 0x0001,
  LT_EVENT_WRITE = 0x0002,
  LT_EVENT_CLOSE = 0x0004,
  LT_EVENT_ERROR = 0x0008,
};

typedef uint32_t LtEvent;

std::string events2string(const LtEvent& event);

}  // namespace base
#endif
