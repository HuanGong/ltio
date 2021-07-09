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

#include "wait_group.h"
#include "co_runner.h"

#include <base/message_loop/message_loop.h>

namespace base {

std::shared_ptr<WaitGroup> WaitGroup::New() {
  return std::shared_ptr<WaitGroup>(new WaitGroup());
}

WaitGroup::WaitGroup() : result_status_(kSuccess), wait_count_(0) {
  flag_.clear();

  loop_ = base::MessageLoop::Current();
  DCHECK(loop_);
}

WaitGroup::~WaitGroup() {}

void WaitGroup::Done() {
  if (wait_count_.fetch_sub(1) != 1) {
    return;
  }
  auto guard = shared_from_this();
  loop_->PostTask(FROM_HERE, &WaitGroup::wakeup_internal, std::move(guard));
}

void WaitGroup::Add(int64_t count) {
  DCHECK(count > 0);
  wait_count_.fetch_add(count);
}

void WaitGroup::OnTimeOut() {
  result_status_ = kTimeout;
  wakeup_internal();
}

void WaitGroup::wakeup_internal() {
  if (resumer_)
    resumer_();
}

WaitGroup::Result WaitGroup::Wait(int64_t timeout_ms) {
  if (flag_.test_and_set()) {
    CHECK(false);
    return kSuccess;
  }

  if (0 == wait_count_.load()) {
    return kSuccess;
  }

  resumer_ = CO_RESUMER;

  if (timeout_ms > 0) {
    timeout_.reset(TimeoutEvent::CreateOneShot(timeout_ms, false));
    // Don't Worry about invalid `this` happend, bz only follow two cases
    // case 1: timeout invoke, nothing happend
    // case 2: all task done, timeout event be removed, OnTimeOut never invoked
    auto functor = std::bind(&WaitGroup::OnTimeOut, this);
    timeout_->InstallTimerHandler(NewClosure(std::move(functor)));
    loop_->Pump()->AddTimeoutEvent(timeout_.get());
  }

  CO_YIELD;

  if (timeout_) {
    loop_->Pump()->RemoveTimeoutEvent(timeout_.get());
  }
  return result_status_;
}

}  // end namespace base
