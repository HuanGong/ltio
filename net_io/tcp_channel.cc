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

#include "tcp_channel.h"
#include <cmath>
#include "base/base_constants.h"
#include "base/closure/closure_task.h"
#include "base/message_loop/event_pump.h"
#include "base/utils/sys_error.h"
#include "glog/logging.h"
#include "net_callback.h"
#include "socket_utils.h"

namespace {
const int32_t kBlockSize = 2 * 1024;
}

namespace lt {
namespace net {

// static
TCPChannelPtr TcpChannel::Create(int socket_fd,
                                 const IPEndPoint& local,
                                 const IPEndPoint& peer) {
  return TCPChannelPtr(new TcpChannel(socket_fd, local, peer));
}

TcpChannel::TcpChannel(int socket_fd,
                       const IPEndPoint& loc,
                       const IPEndPoint& peer)
  : SocketChannel(socket_fd, loc, peer) {
  fd_event_->SetEdgeTrigger(true);
  socketutils::TCPNoDelay(socket_fd);
  socketutils::KeepAlive(socket_fd, true);
}

TcpChannel::~TcpChannel() {
  VLOG(GLOG_VTRACE) << __FUNCTION__ << ChannelInfo();
}

bool TcpChannel::HandleRead(base::FdEvent* event) {
  VLOG(GLOG_VTRACE) << ChannelInfo() << " handle read";
  int err = 0;
  ssize_t bytes_read;
  bool need_close = false;
  do {
    in_buffer_.EnsureWritableSize(kBlockSize);

    bytes_read = ::read(fd_event_->GetFd(), in_buffer_.GetWrite(),
                        in_buffer_.CanWriteSize());
    if (bytes_read > 0) {
      VLOG(GLOG_VTRACE) << ChannelInfo() << " read [" << bytes_read << "] bytes";
      in_buffer_.Produce(bytes_read);
      continue;
    }

    VLOG(GLOG_VTRACE) << ChannelInfo() << " read err:" << base::StrError();
    if (errno == EAGAIN) {
      break;
    }
    if (bytes_read == 0) { //peer close
      need_close = true;
      break;
    }
    if (EINTR != errno) {
      need_close = true;
      LOG(ERROR) << ChannelInfo() << " read error:" << base::StrError();
      break;
    }
  } while (true);

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
  VLOG(GLOG_VTRACE) << ChannelInfo() << " handle write";

  int fatal_err = 0;
  int socket_fd = fd_event_->GetFd();

  ssize_t writen_bytes = 0;
  while (out_buffer_.CanReadSize()) {
    writen_bytes = socketutils::Write(socket_fd,
                                      out_buffer_.GetRead(),
                                      out_buffer_.CanReadSize());
    if (writen_bytes > 0) {
      out_buffer_.Consume(writen_bytes);
      continue;
    }
    if (errno == EINTR) {
      VLOG(GLOG_VTRACE) << ChannelInfo() << " EINTR continue";
      continue;
    }
    if (errno != EAGAIN) {
      fatal_err = errno;
    }
    break;
  };

  if (fatal_err != 0) {
    LOG(ERROR) << ChannelInfo()
               << " write err:" << base::StrError(fatal_err);
    return HandleClose(event);
  }

  if (out_buffer_.CanReadSize() == 0) {
    reciever_->OnDataFinishSend(this);
    if (schedule_shutdown_) {
      return HandleClose(event);
    }
  }
  return true;
}

int32_t TcpChannel::Send(const char* data, const int32_t len) {
  DCHECK(pump_->IsInLoop());

  if (!IsConnected()) {
    return -1;
  }

  if (out_buffer_.CanReadSize() > 0) {
    out_buffer_.WriteRawData(data, len);
    HandleWrite(fd_event_.get());
    return len;
  }

  int32_t fatal_err = 0;
  size_t n_write = 0;
  size_t n_remain = len;
  do {
    ssize_t part_write =
        socketutils::Write(fd_event_->GetFd(), data + n_write, n_remain);
    if (part_write > 0) {
      n_write += part_write;
      n_remain = n_remain - part_write;

      DCHECK((n_write + n_remain) == size_t(len));
      continue;
    }
    if (errno == EINTR) {
      continue;
    }

    if (errno == EAGAIN) {
      out_buffer_.WriteRawData(data + n_write, n_remain);
    } else {
      // to avoid A -> B -> callback delete A problem,
      // use a write event to triggle handle close action
      fatal_err = errno;
      schedule_shutdown_ = true;
      SetChannelStatus(Status::CLOSING);
      LOG(ERROR) << ChannelInfo() << " send err:" << base::StrError()
        << " schedule close";
    }
    break;
  } while (n_remain != 0);

  if (out_buffer_.CanReadSize()) {
    fd_event_->EnableWriting();
  }
  return fatal_err != 0 ? -1 : n_write;
}

}  // namespace net
}  // namespace lt
