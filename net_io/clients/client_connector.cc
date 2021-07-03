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

#include <algorithm>

#include <base/utils/sys_error.h>
#include "base/message_loop/event.h"
#include "client_connector.h"
#include "net_io/base/sockaddr_storage.h"

namespace lt {
namespace net {

Connector::Connector(EventPump* pump, Delegate* d) : pump_(pump), delegate_(d) {
  CHECK(delegate_);
  count_.store(0);
}

void Connector::Launch(const IPEndPoint& address) {
  CHECK(pump_->IsInLoop() && delegate_);

  int sock_fd = socketutils::CreateNoneBlockTCP(address.GetSockAddrFamily());
  if (sock_fd == -1) {
    LOG(ERROR) << __FUNCTION__ << " connect [" << address.ToString()
               << "] failed";
    return delegate_->OnConnectFailed(inprogress_list_.size());
  }

  VLOG(GLOG_VTRACE) << __FUNCTION__ << " connect [" << address.ToString()
                    << "] enter";

  SockaddrStorage storage;
  if (address.ToSockAddr(storage.AsSockAddr(), storage.Size()) == 0) {
    socketutils::CloseSocket(sock_fd);
    return delegate_->OnConnectFailed(inprogress_list_.size());
  }

  int error = 0;
  int ret = socketutils::SocketConnect(sock_fd, storage.AsSockAddr(), &error);
  VLOG(GLOG_VTRACE) << __FUNCTION__ << " ret:" << ret
                    << " error:" << base::StrError(error);
  if (ret == 0 && error == 0) {
    IPEndPoint remote_addr;
    IPEndPoint local_addr;

    if (!socketutils::GetPeerEndpoint(sock_fd, &remote_addr) ||
        !socketutils::GetLocalEndpoint(sock_fd, &local_addr)) {
      socketutils::CloseSocket(sock_fd);
      return delegate_->OnConnectFailed(inprogress_list_.size());
    }

    VLOG(GLOG_VTRACE) << __FUNCTION__ << " " << address.ToString()
                      << " connected at once";
    return delegate_->OnConnected(sock_fd, local_addr, remote_addr);
  }

  switch (error) {
    case EINTR:
    case EISCONN:
    case EINPROGRESS: {
      RefFdEvent fd_event =
          FdEvent::Create(this, sock_fd, base::LtEv::LT_EVENT_WRITE);

      pump_->InstallFdEvent(fd_event.get());

      inprogress_list_.push_back(fd_event);
      count_.store(inprogress_list_.size());
      VLOG(GLOG_VTRACE) << __FUNCTION__
                        << " add new EINPROGRESS fd:" << fd_event->GetFd();

    } break;
    default: {
      socketutils::CloseSocket(sock_fd);
      LOG(ERROR) << __FUNCTION__
                 << " launch connection failed:" << base::StrError(error);
      return delegate_->OnConnectFailed(inprogress_list_.size());

    } break;
  }
}

bool Connector::HandleRead(base::FdEvent* fd_event) {
  LOG(ERROR) << __func__ << " should not reached";
  return HandleClose(fd_event);
}

bool Connector::HandleWrite(base::FdEvent* fd_event) {
  int socket_fd = fd_event->GetFd();

  if (nullptr == delegate_) {
    LOG(ERROR) << __func__ << " delegate_ null, endup";
    Cleanup(fd_event);
    return false;
  }

  if (socketutils::IsSelfConnect(socket_fd)) {
    LOG(ERROR) << __func__ << " detect a self connection, endup";
    Cleanup(fd_event);
    delegate_->OnConnectFailed(inprogress_list_.size());
    return false;
  }

  fd_event->ReleaseOwnership();
  Cleanup(fd_event);

  IPEndPoint remote_addr, local_addr;
  if (!socketutils::GetPeerEndpoint(socket_fd, &remote_addr) ||
      !socketutils::GetLocalEndpoint(socket_fd, &local_addr)) {
    Cleanup(fd_event);
    delegate_->OnConnectFailed(inprogress_list_.size());
    LOG(ERROR) << __func__ << " bad socket fd, connect failed";
    return false;
  }

  delegate_->OnConnected(socket_fd, local_addr, remote_addr);
  return false;
}

bool Connector::HandleError(base::FdEvent* fd_event) {
  LOG(ERROR) << __func__ << " connect fd error";
  Cleanup(fd_event);

  if (delegate_) {
    delegate_->OnConnectFailed(inprogress_list_.size());
  }
  return false;
}

bool Connector::HandleClose(base::FdEvent* fd_event) {
  LOG(ERROR) << __func__ << " connect fd close";
  return HandleError(fd_event);
}

void Connector::Stop() {
  CHECK(pump_->IsInLoop());

  delegate_ = NULL;
  for (auto& event : inprogress_list_) {
    pump_->RemoveFdEvent(event.get());
  }
  inprogress_list_.clear();
  count_.store(inprogress_list_.size());
}

void Connector::Cleanup(base::FdEvent* event) {
  pump_->RemoveFdEvent(event);
  inprogress_list_.remove_if(
      [event](base::RefFdEvent& ev) -> bool { return ev.get() == event; });
  count_.store(inprogress_list_.size());
  VLOG(GLOG_VINFO) << " connecting list size:" << inprogress_list_.size();
}

}  // namespace net
}  // namespace lt
