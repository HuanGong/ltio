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

#ifndef BASE_CLOSURE_TASK_H_H
#define BASE_CLOSURE_TASK_H_H

#include <assert.h>
#include <functional>
#include <memory>

#include "glog/logging.h"

#include "base/base_micro.h"
#include "location.h"

namespace base {

using LtClosure = std::function<void()>;
using ClosureCallback = LtClosure;

class TaskBase {
public:
  TaskBase() {}
  explicit TaskBase(const Location& location) : location_(location) {}
  virtual ~TaskBase() {}
  virtual void Run() = 0;
  const Location& TaskLocation() const { return location_; }
  std::string ClosureInfo() const { return location_.ToString(); }

private:
  Location location_;
};
using TaskBasePtr = std::unique_ptr<TaskBase>;

template <typename Functor>
class ClosureTask : public TaskBase {
public:
  explicit ClosureTask(const Location& location, const Functor& closure)
    : TaskBase(location), closure_task(closure) {}
  void Run() override {
    try {
      closure_task();
    } catch (...) {
      LOG(ERROR) << "Crash From:" << ClosureInfo();
      abort();
    }
  }

private:
  Functor closure_task;
};

template <class Closure, class Cleanup>
class TaskWithCleanup : public TaskBase {
public:
  TaskWithCleanup(const Location& location,
                  const Closure& closure,
                  const Cleanup& cleanup)
    : TaskBase(location), closure_task(closure), cleanup_task(cleanup) {}
  void Run() override {
    try {
      closure_task();
    } catch (...) {
      LOG(ERROR) << "Task Crash, From:" << ClosureInfo();
      abort();
    }
    try {
      cleanup_task();
    } catch (...) {
      LOG(ERROR) << "Cleanup Crash, From:" << ClosureInfo();
      abort();
    }
  }

private:
  Closure closure_task;
  Cleanup cleanup_task;
};

template <class Closure>
static TaskBasePtr CreateClosure(const Location& location,
                                 const Closure& closure) {
  return TaskBasePtr(new ClosureTask<Closure>(location, closure));
}

template <class Closure, class Cleanup>
static TaskBasePtr CreateTaskWithCallback(const Location& location,
                                          const Closure& closure,
                                          const Cleanup& cleanup) {
  return TaskBasePtr(
      new TaskWithCleanup<Closure, Cleanup>(location, closure, cleanup));
}

}  // namespace base

#define NewClosure(Functor) ::base::CreateClosure(FROM_HERE, Functor)
#define NewClosureWithCleanup(Functor, Cleanup) \
  ::base::CreateClosureWithCallback(Location, Functor, Cleanup)

#endif
