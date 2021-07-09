#ifndef BASE_CORO_BSTCTX_IMPL_H_H
#define BASE_CORO_BSTCTX_IMPL_H_H

#include <string.h>
#include <memory>
#include "base/base_constants.h"
#include "base/base_micro.h"
#include "base/time/time_utils.h"
#include "fcontext/fcontext.h"
#include "glog/logging.h"

#include <math.h>
#include <string.h>
#include <sys/resource.h>

namespace base {

class Coroutine;
using RefCoroutine = std::shared_ptr<Coroutine>;
using WeakCoroutine = std::weak_ptr<Coroutine>;

struct coro_context {
 fcontext_stack_t stack_;
 fcontext_transfer_t co_ctx_;
};

class Coroutine : public coro_context, public EnableShared(Coroutine) {
public:
  using EntryFunc = pfn_fcontext;

  static RefCoroutine New() {
    return RefCoroutine(new Coroutine());
  }

  static RefCoroutine New(EntryFunc fn) {
    return RefCoroutine(new Coroutine(fn));
  }

  ~Coroutine() {
    VLOG(GLOG_VTRACE) << "corotuine gone";
    destroy_fcontext_stack(&stack_);
  }

private:
  friend class CoroRunner;
  friend class CoroRunnerImpl;

  Coroutine() {
    memset(&stack_, 0, sizeof(stack_));
    Reset(nullptr);
  }

  Coroutine(EntryFunc fn) {
    CHECK(fn);

    VLOG(GLOG_VTRACE) << "corotuine born";
    memset(&stack_, 0, sizeof(stack_));
    stack_ = create_fcontext_stack(64 * 1024);

    struct rlimit limit;
    getrlimit(RLIMIT_STACK, &limit);

    Reset(fn);
  }

  void Reset(EntryFunc fn) {
    co_ctx_.data = this;
    if (stack_.sptr && stack_.ssize && fn) {
      co_ctx_.ctx = make_fcontext(stack_.sptr, stack_.ssize, fn);
    }
  }

  void Resume() {
    resume_id_++;
    ticks_ = base::time_us();
    co_ctx_ = jump_fcontext(co_ctx_.ctx, this);
  }

  uint64_t ResumeID() const { return resume_id_; }

  uint64_t ElapsedTime() const { return time_us() - ticks_; }

  Coroutine* SelfHolder() {
    self_ = shared_from_this();
    return this;
  }

  void ReleaseSelfHolder() { self_.reset(); };

  WeakCoroutine AsWeakPtr() { return shared_from_this(); }

  std::string CoInfo() const {
    std::ostringstream oss;
    oss << "[co@" << this << "#" << resume_id_ << "]";
    return oss.str();
  }

  uint64_t resume_id_ = 0;

  /* ticks record for every coro task 'continuous'
   * time consume, it will reset every yield action*/
  int64_t ticks_ = 0;

  std::shared_ptr<Coroutine> self_;

  DISALLOW_COPY_AND_ASSIGN(Coroutine);
};

}  // namespace base

#endif
