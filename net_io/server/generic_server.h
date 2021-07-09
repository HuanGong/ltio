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

#ifndef LT_NET_GENERIC_SERVER_H_H
#define LT_NET_GENERIC_SERVER_H_H

#include <chrono>  // std::chrono::seconds
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>  // std::mutex, std::unique_lock
#include <vector>

#include "base/base_micro.h"
#include "base/coroutine/co_runner.h"
#include "net_io/codec/codec_factory.h"
#include "net_io/io_service.h"

namespace lt {
namespace net {

using base::MessageLoop;

typedef struct DefaultConfigurator {
  static const bool coro_process = true;
  static const uint64_t kClientConnLimit = 65536;
  static const uint64_t kRequestQpsLimit = 100000;
} DefaultConfigurator;

/*
 * Context need implement follow interface
 *
 * Context::New(RefCodecMessage request);
 * */
template <typename Context, typename Configurator = DefaultConfigurator>
class BaseServer : public IOServiceDelegate {
public:
  typedef BaseServer<Context, Configurator> Server;
  typedef std::shared_ptr<Context> RefContext;
  typedef std::list<RefIOService> RefIOServiceList;
  typedef std::vector<MessageLoop*> MessageLoopList;

  typedef std::function<void(const std::shared_ptr<Context>&)> Handler;

  BaseServer() : serving_flag_(false), client_count_(0) {}

  Server& WithIOLoops(MessageLoopList& loops) {
    io_loops_ = loops;
    return *this;
  }

  Server& WithAddress(const std::string& addr) {
    address_ = addr;
    return *this;
  }

  std::string ServerInfo() const {
    return uri_.protocol + ":" + endpoint_.ToString();
  }

  const IPEndPoint& Endpoint() const { return endpoint_; };
  const url::SchemeIpPort& ServeURI() const { return uri_; }

  void ServeAddress(const Handler& handler) { ServeAddress(address_, handler); }

  void ServeAddress(const std::string& address, const Handler& handler) {
    CHECK(handler);
    CHECK(io_loops_.size());
    CHECK(!serving_flag_.exchange(true));

    address_ = address;
    handler_ = handler;

    if (!url::ParseURI(address, uri_)) {
      LOG(ERROR) << "error address,required format:[scheme://host:port]";
      CHECK(false);
    }
    if (!CodecFactory::HasCreator(uri_.protocol)) {
      LOG(ERROR) << "scheme:" << uri_.protocol << " codec not found";
      CHECK(false);
    }

    endpoint_ = net::IPEndPoint(uri_.host_ip, uri_.port);
#if defined SO_REUSEPORT && defined NET_ENABLE_REUSER_PORT
    for (base::MessageLoop* loop : io_loops_) {
      RefIOService service(new IOService(endpoint_, uri_.protocol, loop, this));
      loop->PostTask(FROM_HERE, &IOService::Start, service);
      ioservices_.push_back(std::move(service));
    }
#else
    base::MessageLoop* loop = io_loops_.front();
    RefIOService service(new IOService(endpoint_, uri_.protocol, loop, this));
    loop->PostTask(FROM_HERE, &IOService::Start, service);
    ioservices_.push_back(std::move(service));
#endif
  }

  void StopServer(const base::ClosureCallback& callback = nullptr) {
    CHECK(serving_flag_);
    closed_callback_ = callback;

    std::list<RefIOService> services = ioservices_;
    for (RefIOService& service : services) {
      base::MessageLoop* io = service->AcceptorLoop();
      io->PostTask(FROM_HERE, &IOService::Stop, service);
    }

    if (services.empty()) {
      closed_callback_();
    }
  }

protected:
  // override from ioservice delegate
  bool CanCreateNewChannel() override {
    bool can = client_count_ < Configurator::kClientConnLimit;
    LOG_IF(INFO, !can) << " max client limit reach";
    return can;
  };

  void IncreaseChannelCount() override { client_count_++; }

  void DecreaseChannelCount() override { client_count_--; }

  MessageLoop* GetNextIOWorkLoop() override {
#if defined SO_REUSEPORT && defined NET_ENABLE_REUSER_PORT
    return MessageLoop::Current();
#else
    static std::atomic<uint32_t> index = 0;
    return io_loops_[index.fetch_add(1) % io_loops_.size()];
#endif
  };

  void IOServiceStarted(const IOService* service) override {
    std::unique_lock<std::mutex> lck(mtx_);
    int active_iosrv = 0;
    for (const RefIOService& service : ioservices_) {
      active_iosrv += service->IsRunning();
    }
    if (active_iosrv == ioservices_.size()) {
      LOG(INFO) << "Server " << ServerInfo() << " Started";
    }
  }

  void IOServiceStoped(const IOService* service) override {
    std::unique_lock<std::mutex> lck(mtx_);
    ioservices_.remove_if(
        [&](const RefIOService& s) -> bool { return s.get() == service; });

    LOG_IF(INFO, ioservices_.empty()) << "Server:" << ServerInfo() << " Stoped";
    if (ioservices_.empty() && closed_callback_) {
      closed_callback_();
    }
  }

  void OnRequestMessage(const RefCodecMessage& request) override {
    if (!Configurator::coro_process) {
      return handler_(Context::New(request));
    }
    CO_GO std::bind(handler_, Context::New(request));
  }
#ifdef LTIO_HAVE_SSL
public:
  Server& WithSSLContext(base::SSLCtx* ctx) {
    ssl_ctx_ = ctx;
    return *this;
  }
  base::SSLCtx* GetSSLContext() override {
    if (ssl_ctx_) return ssl_ctx_;
    return &base::SSLCtx::DefaultServerCtx();
  }
private:
  base::SSLCtx* ssl_ctx_ = nullptr;
#endif

private:
  Handler handler_;

  bool coro_handler_ = true;

  std::mutex mtx_;

  std::string address_;

  IPEndPoint endpoint_;

  url::SchemeIpPort uri_;

  MessageLoopList io_loops_;

  RefIOServiceList ioservices_;

  std::atomic<bool> serving_flag_;

  std::atomic<uint32_t> active_iosrv_;
  std::atomic<uint32_t> client_count_;

  base::ClosureCallback closed_callback_;

  DISALLOW_COPY_AND_ASSIGN(BaseServer);
};

}  // namespace net
}  // namespace lt
#endif
