#ifndef _LT_CORO_TASK_LOOP_H_
#define _LT_CORO_TASK_LOOP_H_

#include "base/coroutine/bstctx_impl.hpp"
#include "base/coroutine/co_runner.h"
#include "base/queue/task_queue.h"

extern "C" {
#include <thirdparty/timeout/timeout.h>
}

namespace base {

class CoLoop {
public:
  using TimeoutWheel = struct timeouts;

  CoLoop();

  ~CoLoop();

  bool PostTask(TaskBasePtr&& task);

  bool PostDelayTask(TaskBasePtr&&, int64_t ms);

  template <typename Fn, typename... Args>
  bool PostTask(const Location& loc, Fn&& fn, Args&&... args) {
    return PostTask(CreateClosure(loc, std::bind(fn, args...)));
  }

  void Run();

  void PumpIOEvent();

  void WakeUpIfNeeded();

  bool InLoop() const;
protected:
  void ThreadMain(void);

  /* return the minimal timeout for next poll
   * return 0 when has event expired,
   * return default_timeout when no other timeout provided*/
  timeout_t NextTimeout();

  /* finalize TimeoutWheel and delete Life-Ownered timeoutEvent*/
  void InitializeTimeWheel();

  void FinalizeTimeWheel();

  void ProcessTimerEvent();

  void InvokeFiredEvent(FiredEvent* evs, int count);

private:
  bool running_ = true;

  CoroRunner* runner_;

  // Coro0 the core loop
  Coroutine* co_zero_;

  // a epoll based io pump
  Coroutine* co_io_;
  std::unique_ptr<IOMux> io_mux_;

  TimeoutWheel* time_wheel_ = nullptr;

private:
  DISALLOW_COPY_AND_ASSIGN(CoLoop);
};

}  // namespace base
#endif
