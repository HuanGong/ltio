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

#ifndef BASE_CLOSURE_TASK_QUEUE_H_H
#define BASE_CLOSURE_TASK_QUEUE_H_H

#include <thirdparty/cameron_queue/concurrentqueue.h>
#include "base/closure/closure_task.h"

namespace base {

using moodycamel::ConcurrentQueue;
struct TaskQueueTraits : moodycamel::ConcurrentQueueDefaultTraits {
  static const size_t BLOCK_SIZE = 1024;
  static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 32;
};
typedef moodycamel::ConcurrentQueue<TaskBasePtr, TaskQueueTraits>
    ConcurrentTaskQueue;

using TaskQueue = ConcurrentQueue<TaskBasePtr, TaskQueueTraits>;

}  // namespace base
#endif
