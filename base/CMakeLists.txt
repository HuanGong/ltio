SET(BASE_SOURCES
  # chromium numerics
  numerics/checked_math_impl.h
  numerics/clamped_math_impl.h
  numerics/safe_conversions_impl.h
  numerics/safe_conversions_arm_impl.h
  numerics/safe_math_arm_impl.h
  numerics/safe_math_shared_impl.h
  numerics/safe_math_clang_gcc_impl.h

  # simple time utils
  time/time.cc
  time/time_delta.cc
  time/time_ticks.cc
  time/timestamp.cc
  time/time_utils.cc
  time/nspr/prtime.cc

  # posix-unix utils
  utils/sys_error.cc
  utils/rand_util.cc
  utils/rand_util_posix.cc
  utils/ns_convertor.cc
  utils/string/str_utils.cc

  #coroutine
  coroutine/co_loop.cc
  coroutine/co_runner.cc
  coroutine/wait_group.cc

  # task eventloop
  message_loop/event.cc
  message_loop/fd_event.cc
  message_loop/fdev_mgr.cc
  message_loop/event_pump.cc
  message_loop/linux_signal.cpp
  message_loop/file_util_linux.cc
  message_loop/io_multiplexer.cc
  message_loop/io_mux_epoll.cc
  #message_loop/timer_task_queue.cc
  message_loop/message_loop.cc
  message_loop/timeout_event.cc
  message_loop/timer_task_helper.cc
  message_loop/repeating_timer.cc

  #memory
  memory/spin_lock.cc
  memory/lazy_instance.cc

  # gzip compression utils
  utils/gzip/gzip_utils.cc

  # closure
  closure/location.cc
  )

if (LTIO_WITH_OPENSSL)
  list(APPEND BASE_SOURCES crypto/lt_ssl.cc)
endif()

add_library(ltbase_objs OBJECT ${BASE_SOURCES})

ltio_default_properties(ltbase_objs)
add_dependencies(ltbase_objs
  ${CORO_LIBRARY}
)
target_include_directories(ltbase_objs
    PUBLIC
    $<TARGET_PROPERTY:fmt,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:fcontext,INTERFACE_INCLUDE_DIRECTORIES>
)
