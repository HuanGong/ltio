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

#include "socket_acceptor.h"
#include "base/base_constants.h"
#include "base/ip_endpoint.h"
#include "base/sockaddr_storage.h"
#include "base/utils/sys_error.h"
#include "glog/logging.h"
#include "socket_utils.h"

namespace lt {
namespace net {

SocketAcceptor::SocketAcceptor(Actor* actor,
                               base::EventPump* pump,
                               const IPEndPoint& address)
  : listening_(false),
    address_(address),
    handler_(actor),
    event_pump_(pump) {
  CHECK(InitListener());
  CHECK(actor && pump);
}

SocketAcceptor::~SocketAcceptor() {
  CHECK(listening_ == false);
  socket_event_.reset();
}

bool SocketAcceptor::InitListener() {
  int socket_fd = socketutils::CreateNoneBlockTCP(address_.GetSockAddrFamily());
  if (socket_fd < 0) {
    return false;
  }

  // reuse socket addr and port if possible
  socketutils::ReUseSocketPort(socket_fd, true);
  socketutils::ReUseSocketAddress(socket_fd, true);

  SockaddrStorage storage;
  address_.ToSockAddr(storage.AsSockAddr(), storage.Size());

  int ret = socketutils::BindSocketFd(socket_fd, storage.AsSockAddr());
  if (ret < 0) {
    LOG(ERROR) << "acceptor failed bind, address:" << address_.ToString()
               << " fd:" << socket_fd;
    socketutils::CloseSocket(socket_fd);
    return false;
  }
  socket_event_ =
      base::FdEvent::Create(this, socket_fd, base::LtEv::LT_EVENT_NONE);

  VLOG(GLOG_VTRACE) << "acceptor init success, fd:["
                    << socket_fd << "] bind to local:[" << address_.ToString()
                    << "]";
  return true;
}

bool SocketAcceptor::StartListen() {
  CHECK(event_pump_);
  CHECK(event_pump_->IsInLoop());

  if (listening_) {
    LOG(ERROR) << "already listen on:" << address_.ToString();
    return true;
  }

  event_pump_->InstallFdEvent(socket_event_.get());
  socket_event_->EnableReading();

  if (socketutils::ListenSocket(socket_event_->GetFd()) < 0) {
    socket_event_->DisableAll();
    event_pump_->RemoveFdEvent(socket_event_.get());
    LOG(ERROR) << "acceptor failed listen on" << address_.ToString();
    return false;
  }
  VLOG(GLOG_VINFO) << "acceptor start listen on:" << address_.ToString();
  listening_ = true;
  return true;
}

void SocketAcceptor::StopListen() {
  CHECK(event_pump_->IsInLoop());
  if (!listening_) {
    return;
  }
  event_pump_->RemoveFdEvent(socket_event_.get());
  socket_event_->DisableAll();

  listening_ = false;
  VLOG(GLOG_VINFO) << "acceptor stop listen on:" << address_.ToString();
}

bool SocketAcceptor::HandleRead(base::FdEvent* fd_event) {
  struct sockaddr client_socket_in;

  int err = 0;
  int peer_fd = socketutils::AcceptSocket(socket_event_->GetFd(),
                                          &client_socket_in,
                                          &err);
  if (peer_fd < 0) {
    LOG(ERROR) << "accept failed, err:" << base::StrError(err);
    return true;
  }

  IPEndPoint client_addr;
  client_addr.FromSockAddr(&client_socket_in, sizeof(client_socket_in));

  VLOG(GLOG_VTRACE) << "accept a connection:" << client_addr.ToString();

  handler_->OnNewConnection(peer_fd, client_addr);
  return true;
}

bool SocketAcceptor::HandleWrite(base::FdEvent* ev) {
  LOG(ERROR) << "fd [" << ev->GetFd() << "] write event should not reached";
  return true;
}

bool SocketAcceptor::HandleError(base::FdEvent* fd_event) {
  LOG(ERROR) << "fd [" << fd_event->GetFd() << "] error:[" << base::StrError()
             << "]";

  listening_ = false;

  // Relaunch This server
  if (InitListener()) {
    bool re_listen_ok = StartListen();
    LOG_IF(ERROR, !re_listen_ok)
        << "acceptor:[" << address_.ToString() << "] re-listen failed";
  }
  return false;
}

bool SocketAcceptor::HandleClose(base::FdEvent* fd_event) {
  LOG(ERROR) << "acceptor listen fd [" << fd_event->GetFd() << "] closed";
  return HandleError(fd_event);
}

}  // namespace net
}  // namespace lt
