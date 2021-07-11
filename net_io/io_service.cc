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

#include "io_service.h"
#include "base/base_constants.h"
#include "base/closure/closure_task.h"
#include "base/ip_endpoint.h"
#include "codec/codec_factory.h"
#include "codec/codec_message.h"
#include "codec/codec_service.h"
#include "glog/logging.h"
#include "tcp_channel.h"

#ifdef LTIO_HAVE_SSL
#include "tcp_channel_ssl.h"
#endif

namespace lt {
namespace net {

IOService::IOService(const IPEndPoint& addr,
                     const std::string protocol,
                     base::MessageLoop* ioloop,
                     IOServiceDelegate* delegate)
  : protocol_(protocol),
    acpt_io_(ioloop),
    delegate_(delegate),
    is_stopping_(false) {
  CHECK(delegate_);

  auto pump = acpt_io_->Pump();

  service_name_ = addr.ToString();
  acceptor_.reset(new SocketAcceptor(this, pump, addr));
}

IOService::~IOService() {
  VLOG(GLOG_VTRACE) << " IOService@" << this << " Gone";
}

void IOService::Start() {
  auto guard_this = shared_from_this();
  if (!acpt_io_->IsInLoopThread()) {
    acpt_io_->PostTask(FROM_HERE, &IOService::Start, guard_this);
    return;
  }

  DCHECK(acpt_io_->IsInLoopThread());
  CHECK(acceptor_->StartListen());
  delegate_->IOServiceStarted(this);
}

/* step1: close the acceptor */
void IOService::Stop() {
  auto refthis = shared_from_this();

  if (!acpt_io_->IsInLoopThread()) {
    auto functor = std::bind(&IOService::Stop, refthis);
    acpt_io_->PostTask(NewClosure(std::move(functor)));
    return;
  }
  VLOG(GLOG_VINFO) << __FUNCTION__ << " enter, in acpt loop";

  is_stopping_ = true;
  CHECK(acpt_io_->IsInLoopThread());

  // sync top acceptor
  acceptor_->StopListen();

  for (auto& codec : codecs_) {
    base::MessageLoop* loop = codec->IOLoop();
    loop->PostTask(FROM_HERE, [codec, refthis]() {
      // stop close notify
      codec->CloseService(true);

      refthis->OnProtocolServiceGone(codec);
    });
  }
  if (codecs_.empty()) {
    delegate_->IOServiceStoped(this);
  }
}

RefCodecService IOService::CreateCodeService(int fd, const IPEndPoint& peer) {
  base::MessageLoop* io_loop = delegate_->GetNextIOWorkLoop();
  if (!io_loop) {
    socketutils::CloseSocket(fd);
    LOG(INFO) << " no loop can handle this connection";
    return nullptr;
  }

  auto codec = CodecFactory::NewServerService(protocol_, io_loop);

  if (!codec) {
    socketutils::CloseSocket(fd);
    LOG(ERROR) << " protocol:" << protocol_ << " NOT-FOUND";
    return nullptr;
  }

  IPEndPoint local_addr;
  CHECK(socketutils::GetLocalEndpoint(fd, &local_addr));

  SocketChannelPtr channel;
  // ??make a channel creator delegate to export CreateAction by server implment
  if (codec->UseSSLChannel()) {
#ifdef LTIO_HAVE_SSL
    auto ssl_channel = TCPSSLChannel::Create(fd, local_addr, peer);
    ssl_channel->InitSSL(delegate_->GetSSLContext()->NewSSLSession(fd));
    channel = std::move(ssl_channel);
#else
    CHECK(false) << "ssl need compile with openssl lib support";
#endif
  } else {
    channel = TcpChannel::Create(fd, local_addr, peer);
  }
  channel->SetIOEventPump(io_loop->Pump());

  codec->SetDelegate(this);
  codec->BindSocket(std::move(channel));

  io_loop->PostTask(FROM_HERE, [codec]() {
    codec->StartProtocolService();
  });
  return codec;
}

void IOService::OnNewConnection(int fd, const IPEndPoint& peer_addr) {
  CHECK(acpt_io_->IsInLoopThread());
  VLOG(GLOG_VTRACE) << "setup new connection:" << peer_addr.ToString();

  // check connection limit and others
  if (!delegate_->CanCreateNewChannel()) {
    LOG(INFO) << "stop accept new connection"
              << ", current has:[" << codecs_.size() << "]";
    return socketutils::CloseSocket(fd);
  }
  auto codec = CreateCodeService(fd, peer_addr);
  if (codec.get() == nullptr) {
    LOG(INFO) << "failed create new codec for connection"
              << ", current has:[" << codecs_.size() << "]";
    return;
  }
  StoreProtocolService(codec);
  VLOG(GLOG_VTRACE) << "connection done from:" << peer_addr.ToString();
}

void IOService::OnCodecMessage(const RefCodecMessage& message) {
  delegate_->OnRequestMessage(std::move(message));
}

void IOService::OnProtocolServiceGone(const net::RefCodecService& service) {
  // use another task remove a service is a more safe way delete channel&
  // protocol things avoid somewhere->B(do close a channel) ->  ~A  -> use A
  // again in somewhere
  auto guard_this = shared_from_this();
  acpt_io_->PostTask(FROM_HERE, [guard_this, service]() {
    guard_this->RemoveProtocolService(service);
  });
}

void IOService::StoreProtocolService(const RefCodecService service) {
  CHECK(acpt_io_->IsInLoopThread());

  codecs_.insert(service);
  delegate_->IncreaseChannelCount();
}

void IOService::RemoveProtocolService(const RefCodecService service) {
  CHECK(acpt_io_->IsInLoopThread());
  VLOG(GLOG_VINFO) << "codec:" << service.get() << " stoped";

  if (codecs_.erase(service)) {
    delegate_->DecreaseChannelCount();
  } else {
    LOG(ERROR) << "seems has been erase preivously";
  }

  if (is_stopping_ && codecs_.empty()) {
    delegate_->IOServiceStoped(this);
  }
}

}  // namespace net
}  // namespace lt
