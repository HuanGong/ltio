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
#ifndef BASE_CORO_LIBACO_IMPL_H_H
#define BASE_CORO_LIBACO_IMPL_H_H

#include <atomic>
#include <cinttypes>
#include <functional>
#include <memory>
#include "base/time/time_utils.h"

#include <base/base_constants.h>
#include <base/base_micro.h>
#include <base/closure/closure_task.h>
#include <thirdparty/libaco/aco.h>

#ifndef COROUTINE_STACK_SIZE
#define COROUTINE_STACK_SIZE 262144  // 32768 * sizeof(void*)
#endif

namespace base {

class Coroutine;
using RefCoroutine = std::shared_ptr<Coroutine>;
using WeakCoroutine = std::weak_ptr<Coroutine>;

struct coro_context {
  aco_t* coro_ = nullptr;
  aco_share_stack_t* sstk_ = nullptr;
};

class Coroutine : public EnableShared(Coroutine) {
public:
  using EntryFunc = aco_cofuncp_t;

  static RefCoroutine New(EntryFunc entry, Coroutine* main_co) {
    return RefCoroutine(new Coroutine(entry, main_co));
  }

  ~Coroutine() {
    aco_destroy(coro_);
    coro_ = nullptr;

    if (sstk_) {
      aco_share_stack_destroy(sstk_);
      sstk_ = nullptr;
    }
    VLOG(GLOG_VTRACE) << "coroutine gone:" << CoInfo();
  }
private:
  friend class CoroRunner;
  Coroutine(EntryFunc entry, Coroutine* main_co) {
    if (entry == nullptr && main_co == nullptr) {
      aco_thread_init(NULL);
      coro_ = aco_create(NULL, NULL, 0, NULL, NULL);
    } else {
      sstk_ = aco_share_stack_new(COROUTINE_STACK_SIZE);
      coro_ = aco_create(main_co->coro_, sstk_, 0, entry, this);
      VLOG(GLOG_VTRACE) << "sstack size:" << sstk_->real_sz;
    }
    VLOG(GLOG_VTRACE) << "new coroutine:" << CoInfo();
  }

  /*NOTE: this will switch to main coro, be care for call this
   * must ensure call it when coro_fn finish, can't use return*/
  void Exit() {
    aco_exit();
  }

  void Reset(EntryFunc entry) {
    auto main_co = coro_->main_co;
    aco_destroy(coro_);
    coro_ = nullptr;
    coro_ = aco_create(main_co, sstk_, 0, entry, this);
  }

  void Resume() {
    resume_id_++;
    ticks_ = base::time_us();
    if (aco_is_main_co(coro_)) {
      //aco_yield(); <===> aco_yield1(aco_gtls_co);
      aco_yield1(aco_gtls_co);
    } else {
      aco_resume(coro_);
    }
  }

  std::string CoInfo() const {
    std::ostringstream oss;
    oss << "[co@" << this << "#" << resume_id_ << "]";
    return oss.str();
  }

  Coroutine* SelfHolder() {
    self_ = shared_from_this();
    return this;
  }

  void ReleaseSelfHolder() { self_.reset(); };

  WeakCoroutine AsWeakPtr() { return shared_from_this(); }
private:
  aco_t* coro_ = nullptr;

  aco_share_stack_t* sstk_ = nullptr;

  uint64_t resume_id_ = 0;

  /* ticks record for every coro task 'continuous'
   * time consume, it will reset every yield action*/
  int64_t ticks_ = 0;

  RefCoroutine self_;
  DISALLOW_COPY_AND_ASSIGN(Coroutine);
};

}  // namespace base

#endif
