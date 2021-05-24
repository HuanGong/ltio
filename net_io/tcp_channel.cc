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



#include "glog/logging.h"

#include "base/base_constants.h"
#include "base/message_loop/event_pump.h"
#include "base/closure/closure_task.h"
#include <base/utils/sys_error.h>

#include "net_callback.h"
#include "socket_utils.h"
#include "tcp_channel.h"

namespace {
  const int32_t kBlockSize = 4 * 1024;
}

namespace lt {
namespace net {

//static
TcpChannelPtr TcpChannel::Create(int socket_fd,
                                 const IPEndPoint& local,
                                 const IPEndPoint& peer,
                                 base::EventPump* pump) {

  return TcpChannelPtr(new TcpChannel(socket_fd, local, peer, pump));
}

TcpChannel::TcpChannel(int socket_fd,
                       const IPEndPoint& loc,
                       const IPEndPoint& peer,
                       base::EventPump* pump)
  : SocketChannel(socket_fd, loc, peer, pump) {

  fd_event_->SetEdgeTrigger(true);
  socketutils::KeepAlive(socket_fd, true);
}

TcpChannel::~TcpChannel() {
  VLOG(GLOG_VTRACE) << __FUNCTION__ << ChannelInfo();
}

bool TcpChannel::HandleRead(base::FdEvent* event) {

  int err = 0;
  ssize_t bytes_read;
  bool need_close = false;
  do {
    in_buffer_.EnsureWritableSize(kBlockSize);

    bytes_read = ::read(fd_event_->GetFd(),
                        in_buffer_.GetWrite(),
                        in_buffer_.CanWriteSize());
    int err = errno;
    VLOG(GLOG_VTRACE) << __FUNCTION__ << ChannelInfo() << " read [" << bytes_read << "] bytes";

    if (bytes_read > 0) {
      in_buffer_.Produce(bytes_read);
      continue;
    }
    if (0 == bytes_read) {
      need_close = true;
      break;
    }

    if (err != EAGAIN) {
      need_close = true;
      LOG(ERROR) << __FUNCTION__ << ChannelInfo() << " read error:" << base::StrError();
    }
    break;

  } while(1);

  if (in_buffer_.CanReadSize()) {
    reciever_->OnDataReceived(this, &in_buffer_);
  }

  bool ev_continue = true;
  if (need_close) {
      ev_continue = HandleClose(event);
  }

  return ev_continue;
}

bool TcpChannel::HandleWrite(base::FdEvent* event) {
  int fatal_err = 0;
  int socket_fd = fd_event_->fd();

  while(out_buffer_.CanReadSize()) {

    ssize_t writen_bytes = socketutils::Write(socket_fd, out_buffer_.GetRead(), out_buffer_.CanReadSize());
    if (writen_bytes < 0) {
      if (errno != EAGAIN) {
        fatal_err = errno;
      }
      break;
    }
    out_buffer_.Consume(writen_bytes);
  };

  if (fatal_err != 0) {
    LOG(ERROR) << __FUNCTION__ << ChannelInfo() << " write err:" << base::StrError(fatal_err);
    return HandleClose(event);
  }

  if (out_buffer_.CanReadSize() == 0) {

    fd_event_->DisableWriting();
    reciever_->OnDataFinishSend(this);

    if (schedule_shutdown_) {
      return HandleClose(event);
    }
  }
  return true;
}

bool TcpChannel::HandleError(base::FdEvent* event) {
  int err = socketutils::GetSocketError(fd_event_->fd());
  VLOG(GLOG_VERROR) << __FUNCTION__ << ChannelInfo() << " error: [" << base::StrError(err) << "]";
  return HandleClose(event);
}

bool TcpChannel::HandleClose(base::FdEvent* event) {
  DCHECK(pump_->IsInLoopThread());
  VLOG(GLOG_VTRACE) << __FUNCTION__ << ChannelInfo();

  if (IsConnected()) {
    close_channel();
  }
  reciever_->OnChannelClosed(this);
  return false; // don't stop next event
}

void TcpChannel::ShutdownChannel(bool half_close) {
  DCHECK(pump_->IsInLoopThread());

  VLOG(GLOG_VTRACE) << __FUNCTION__ << ChannelInfo();

  schedule_shutdown_ = true;
  if (half_close) {
    if (!fd_event_->IsWriteEnable()) {
      fd_event_->EnableWriting();
    }
  } else {
    HandleClose(fd_event_.get());
  }
}

void TcpChannel::ShutdownWithoutNotify() {
  if (IsConnected()) {
    close_channel();
  }
}

int32_t TcpChannel::Send(const char* data, const int32_t len) {
  DCHECK(pump_->IsInLoopThread());

  if (!IsConnected()) {
    return -1;
  }

  if (out_buffer_.CanReadSize() > 0) {
    out_buffer_.WriteRawData(data, len);

    if (!fd_event_->IsWriteEnable()) {
      fd_event_->EnableWriting();
    }
    return 0; //all write to buffer, zero write to fd
  }

  int32_t fatal_err = 0;
  size_t n_write = 0;
  size_t n_remain = len;

  do {
    ssize_t part_write = socketutils::Write(fd_event_->fd(), data + n_write, n_remain);

    if (part_write < 0) {

      if (errno == EAGAIN) {

        out_buffer_.WriteRawData(data + n_write, n_remain);

      } else {
        // to avoid A -> B -> callback delete A problem,
        // use a write event to triggle handle close action
      	fatal_err = errno;
        schedule_shutdown_ = true;
        SetChannelStatus(Status::CLOSING);
        if (!fd_event_->IsWriteEnable()) {
          fd_event_->EnableWriting();
        }
        LOG(ERROR) << ChannelInfo() << " send err:" << base::StrError() << " schedule close";
      }
      break;
    }
    n_write += part_write;
    n_remain = n_remain - part_write;

    DCHECK((n_write + n_remain) == size_t(len));
  } while(n_remain != 0);

  if (out_buffer_.CanReadSize() && (!fd_event_->IsWriteEnable())) {
    fd_event_->EnableWriting();
  }
  return fatal_err != 0 ? -1 : n_write;
}


}} //end lt::net
