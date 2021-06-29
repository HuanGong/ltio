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

#include <base/base_constants.h>
#include <atomic>
#include "base/closure/closure_task.h"
#include "base/ip_endpoint.h"
#include "glog/logging.h"

#include "codec/codec_factory.h"
#include "codec/codec_message.h"
#include "codec/codec_service.h"
#include "io_service.h"
#include "tcp_channel.h"
#include "tcp_channel_ssl.h"

namespace lt {
namespace net {

IOService::IOService(const IPEndPoint& addr,
                     const std::string protocol,
                     base::MessageLoop* ioloop,
                     IOServiceDelegate* delegate)
  : protocol_(protocol),
    accept_loop_(ioloop),
    delegate_(delegate),
    is_stopping_(false) {
  CHECK(delegate_);

  service_name_ = addr.ToString();
  acceptor_.reset(new SocketAcceptor(accept_loop_->Pump(), addr));
  acceptor_->SetNewConnectionCallback(std::bind(&IOService::OnNewConnection,
                                                this, std::placeholders::_1,
                                                std::placeholders::_2));
}

IOService::~IOService() {
  VLOG(GLOG_VTRACE) << __FUNCTION__ << " IOService@" << this << " Gone";
}

void IOService::StartIOService() {
  if (accept_loop_->IsInLoopThread()) {
    return StartInternal();
  }

  accept_loop_->PostTask(
      FROM_HERE, std::bind(&IOService::StartInternal, shared_from_this()));
}

void IOService::StartInternal() {
  DCHECK(accept_loop_->IsInLoopThread());

  CHECK(acceptor_->StartListen());

  if (delegate_) {
    delegate_->IOServiceStarted(this);
  }
}

/* step1: close the acceptor */
void IOService::StopIOService() {
  if (!accept_loop_->IsInLoopThread()) {
    auto functor = std::bind(&IOService::StopIOService, this);
    accept_loop_->PostTask(NewClosure(std::move(functor)));
    return;
  }

  CHECK(accept_loop_->IsInLoopThread());

  // sync
  acceptor_->StopListen();
  is_stopping_ = true;

  // async
  for (auto& codec_service : codecs_) {
    base::MessageLoop* loop = codec_service->IOLoop();
    // TODO: add a flag stop callback, force stop/close?
    loop->PostTask(FROM_HERE, &CodecService::CloseService, codec_service,
                   false);
  }

  if (codecs_.empty() && delegate_) {
    delegate_->IOServiceStoped(this);
  }
}

void IOService::OnNewConnection(int fd, const IPEndPoint& peer_addr) {
  CHECK(accept_loop_->IsInLoopThread());

  VLOG(GLOG_VTRACE) << __FUNCTION__
                    << " connect apply from:" << peer_addr.ToString();

  // check connection limit and others
  if (!delegate_ || !delegate_->CanCreateNewChannel()) {
    LOG(INFO) << __FUNCTION__ << " Stop accept new connection"
              << ", current has:[" << codecs_.size() << "]";
    return socketutils::CloseSocket(fd);
  }

  base::MessageLoop* io_loop = delegate_->GetNextIOWorkLoop();
  if (!io_loop) {
    LOG(INFO) << __FUNCTION__ << " No IOLoop handle this connect request";
    return socketutils::CloseSocket(fd);
  }

  RefCodecService codec_service =
      CodecFactory::NewServerService(protocol_, io_loop);

  if (!codec_service) {
    LOG(ERROR) << __FUNCTION__ << " scheme:" << protocol_ << " NOT-FOUND";
    return socketutils::CloseSocket(fd);
  }

  IPEndPoint local_addr;
  CHECK(socketutils::GetLocalEndpoint(fd, &local_addr));
  SocketChannelPtr channel;
  if (codec_service->UseSSLChannel()) {
    channel = TCPSSLChannel::Create(fd, local_addr, peer_addr, io_loop->Pump());
  } else {
    channel = TcpChannel::Create(fd, local_addr, peer_addr, io_loop->Pump());
  }
  codec_service->SetDelegate(this);
  codec_service->BindSocket(std::move(channel));

  if (io_loop->IsInLoopThread()) {
    codec_service->StartProtocolService();
  } else {
    io_loop->PostTask(NewClosure(
        std::bind(&CodecService::StartProtocolService, codec_service)));
  }
  StoreProtocolService(codec_service);

  VLOG(GLOG_VTRACE) << __FUNCTION__
                    << " Connection from:" << peer_addr.ToString()
                    << " establisted";
}

void IOService::OnCodecMessage(const RefCodecMessage& message) {
  if (delegate_) {
    return delegate_->OnRequestMessage(message);
  }
  LOG(INFO) << __func__ << " nobody handle this request";
}

void IOService::OnProtocolServiceGone(const net::RefCodecService& service) {
  // use another task remove a service is a more safe way delete channel&
  // protocol things avoid somewhere->B(do close a channel) ->  ~A  -> use A
  // again in somewhere
  accept_loop_->PostTask(FROM_HERE, &IOService::RemoveProtocolService, this,
                         service);
}

void IOService::StoreProtocolService(const RefCodecService service) {
  CHECK(accept_loop_->IsInLoopThread());

  codecs_.insert(service);
  delegate_->IncreaseChannelCount();
}

void IOService::RemoveProtocolService(const RefCodecService service) {
  CHECK(accept_loop_->IsInLoopThread());

  size_t count = codecs_.erase(service);
  LOG_IF(INFO, count == 0) << __func__ << " seems been erase early";
  if (!delegate_) {
    return;
  }

  if (count == 1) {
    delegate_->DecreaseChannelCount();
  }

  if (is_stopping_ && codecs_.empty()) {
    delegate_->IOServiceStoped(this);
  }
}

}  // namespace net
}  // namespace lt
