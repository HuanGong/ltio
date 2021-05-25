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

#ifndef NET_HTTP_SERVER_H_H
#define NET_HTTP_SERVER_H_H

#include <list>
#include <vector>
#include <chrono>             // std::chrono::seconds
#include <functional>

#include "http_context.h"
#include "base/base_micro.h"
#include "base/message_loop/message_loop.h"
#include "net_io/io_service.h"
#include "net_io/codec/codec_message.h"
#include "net_io/codec/http/http_request.h"
#include "net_io/codec/http/http_response.h"

namespace lt {
namespace net {

typedef std::function<void(HttpContext* context)> HttpMessageHandler;

//?? use traits replace IOserviceDelegate override?
class HttpServer : public IOServiceDelegate {
public:
  HttpServer();
  ~HttpServer();

  void SetDispatcher(Dispatcher* dispatcher);
  void SetIOLoops(std::vector<base::MessageLoop*>& loops);

  void ServeAddress(const std::string&, HttpMessageHandler);
  void SetCloseCallback(const base::ClosureCallback& callback);

  void StopServer();
protected:
  //override from ioservice delegate
  bool CanCreateNewChannel() override;
  void IncreaseChannelCount() override;
  void DecreaseChannelCount() override;
  base::MessageLoop* GetNextIOWorkLoop() override;
  void IOServiceStarted(const IOService* ioservice) override;
  void IOServiceStoped(const IOService* ioservice) override;
  void OnRequestMessage(const RefCodecMessage& request) override;

  // handle http request in target loop
  void HandleHttpRequest(HttpContext* context);

  base::MessageLoop* NextWorkerLoop();
private:
  Dispatcher* dispatcher_ = NULL;
  HttpMessageHandler message_handler_;

  std::vector<base::MessageLoop*> io_loops_;
  std::list<RefIOService> ioservices_;
  base::LtClosure closed_callback_;

  std::mutex mtx_;
  std::atomic_bool serving_flag_;

  std::atomic<uint32_t> connection_count_;

  std::atomic<uint32_t> worker_index_;
  DISALLOW_COPY_AND_ASSIGN(HttpServer);
};

}}
#endif
