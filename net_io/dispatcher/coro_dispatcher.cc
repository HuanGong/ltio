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

#include "coro_dispatcher.h"

#include <base/base_constants.h>
#include <base/coroutine/co_runner.h>
#include <net_io/codec/codec_service.h>
#include <net_io/tcp_channel.h>

#include "glog/logging.h"

namespace lt {
namespace net {

CoroDispatcher::CoroDispatcher(bool handle_in_io) : Dispatcher(handle_in_io) {}

CoroDispatcher::~CoroDispatcher() {}

bool CoroDispatcher::SetWorkContext(CodecMessage* message) {
  message->SetWorkerCtx(base::MessageLoop::Current(), CO_RESUMER);
  return base::MessageLoop::Current();
}

bool CoroDispatcher::Dispatch(const base::LtClosure& closure) {
  if (handle_in_io_) {
    CO_GO base::MessageLoop::Current() << closure;
    return true;
  }

  CO_GO NextWorker() << closure;
  return true;
}

}  // namespace net
}  // namespace lt
