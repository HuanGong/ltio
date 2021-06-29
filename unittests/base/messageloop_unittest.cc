#include <thirdparty/catch/catch.hpp>
#include "base/closure/closure_task.h"
#include "glog/logging.h"

#include <base/coroutine/coroutine_runner.h>
#include <base/message_loop/event_pump.h>
#include <base/message_loop/message_loop.h>
#include <iostream>

class Stub {
public:
  void func(int a, int b) { LOG(INFO) << __FUNCTION__ << " invoked "; };
};

Stub stub;

void FailureDump(const char* s, int sz) {
  std::string failure_info(s, sz);
  LOG(INFO) << " ERROR: " << failure_info;
}

void cfunc() {
  LOG(INFO) << __func__ << " c function run";
}

class TaskMock {
public:
  void member_fun() { LOG(INFO) << __func__ << " run"; }
  void member_fun_args(int v) { LOG(INFO) << __func__ << " run, arg:" << v; }
  static void static_member_fun() { LOG(INFO) << __func__ << " run, arg:"; }
};

TEST_CASE("base.task", "[test event pump timer]") {
  auto f = NewClosure(&cfunc);
  f->Run();
  auto lambda_task = NewClosure([]() { ; });
  lambda_task->Run();

  TaskMock mock;
  auto static_member_fun_task = NewClosure(&TaskMock::static_member_fun);
  static_member_fun_task->Run();

  auto member_fun_task = NewClosure(std::bind(&TaskMock::member_fun, &mock));
  member_fun_task->Run();
}

TEST_CASE("event_pump.timer", "[test event pump timer]") {
  base::EventPump pump;
  pump.SetLoopThreadId(std::this_thread::get_id());

  size_t repeated_times = 0;
  bool oneshot_invoked = false;

  base::TimeoutEvent* repeated_toe = new base::TimeoutEvent(5, true);
  repeated_toe->InstallTimerHandler(NewClosure([&]() { repeated_times++; }));

  base::TimeoutEvent* quit_toe = base::TimeoutEvent::CreateOneShot(1000, false);
  quit_toe->InstallTimerHandler(NewClosure([&]() {
    oneshot_invoked = 10;
    pump.RemoveTimeoutEvent(repeated_toe);
    pump.RemoveTimeoutEvent(quit_toe);
    pump.Quit();
  }));

  pump.AddTimeoutEvent(quit_toe);
  pump.AddTimeoutEvent(repeated_toe);

  std::cout << "start at ms:" << base::time_ms() << std::endl;
  pump.Run();
  REQUIRE(oneshot_invoked);
  REQUIRE(repeated_times > 150);

  delete quit_toe;
  delete repeated_toe;
}

TEST_CASE("messageloop.delaytask", "[run delay task]") {
  base::MessageLoop loop;
  loop.SetLoopName("DelayTaskTestLoop");
  loop.Start();

  uint64_t start = base::time_ms();
  loop.PostDelayTask(NewClosure([&]() {
                       uint64_t end = base::time_ms();
                       LOG(INFO) << "delay task run:" << end - start
                                 << "(ms), expect 500";
                       REQUIRE(((end - start >= 500) && (end - start <= 505)));
                     }),
                     500);

  loop.PostDelayTask(NewClosure([&]() { loop.QuitLoop(); }), 1000);

  loop.WaitLoopEnd();
}

TEST_CASE("messageloop.replytask", "[task with reply function]") {
  base::MessageLoop loop;
  loop.Start();

  base::MessageLoop replyloop;
  replyloop.Start();

  int64_t replytask_invoked_times = 0;

  loop.PostTaskAndReply(FROM_HERE,
                        [&]() { LOG(INFO) << " task bind reply in loop run"; },
                        [&]() {
                          replytask_invoked_times++;
                          REQUIRE(base::MessageLoop::Current() == &loop);
                        });

  loop.PostTaskAndReply(
      FROM_HERE,
      [&]() { LOG(INFO) << " task bind reply use another loop run"; },
      [&]() {
        replytask_invoked_times++;
        REQUIRE(base::MessageLoop::Current() == &replyloop);
      },
      &replyloop);

  loop.PostDelayTask(NewClosure([&]() {
                       loop.QuitLoop();
                       replyloop.QuitLoop();
                     }),
                     100);
  loop.WaitLoopEnd();
  REQUIRE(replytask_invoked_times == 2);
}

int64_t start_time;
static std::int64_t counter = 0;
static const std::int64_t kTaskCount = 10000000;

void invoke(base::MessageLoop* loop, bool coro) {
  if (counter++ == kTaskCount) {
    int64_t diff = base::time_us() - start_time;
    int64_t task_per_second = kTaskCount * counter / diff;
    LOG(INFO) << "coro task one by one bench:" << (coro ? "true" : "false")
              << ", total:" << diff << ", " << task_per_second << "/sec";
    loop->QuitLoop();
    return;
  };

  if (coro) {
    CO_GO loop << std::bind(invoke, loop, coro);
    return;
  }
  loop->PostTask(FROM_HERE, invoke, loop, false);
}

TEST_CASE("co_task_obo_bench", "[new coro task bench per second one by one]") {
  base::MessageLoop loop;
  loop.Start();
  LOG(INFO) << __FUNCTION__ << ", co_task_obo_bench run";
  counter = 0;
  CO_GO& loop << std::bind(invoke, &loop, true);
  start_time = base::time_us();

  loop.WaitLoopEnd();
  LOG(INFO) << __FUNCTION__ << ", co_task_obo_bench end";
}

TEST_CASE("task_obo_bench", "[new task bench per second one by one]") {
  LOG(INFO) << __FUNCTION__ << ", task_obo_bench run";

  base::MessageLoop loop;
  loop.Start();

  counter = 0;
  loop.PostTask(FROM_HERE, invoke, &loop, false);
  start_time = base::time_us();

  loop.WaitLoopEnd();
  LOG(INFO) << __FUNCTION__ << ", task_obo_bench end";
}
