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

#include "io_mux_epoll.h"
#include "fdev_mgr.h"

#include <base/utils/sys_error.h>

#include "glog/logging.h"

namespace base {

namespace {
  constexpr size_t kMaxFiredEvOnePoll = 65535;
}

IOMuxEpoll::IOMuxEpoll()
  : IOMux(),
    fd_mgr_(FdEventMgr::Get()),
    ep_evs_(kMaxFiredEvOnePoll) {
  epoll_ = ::epoll_create(kMaxFiredEvOnePoll);
}

IOMuxEpoll::~IOMuxEpoll() {
  ::close(epoll_);
}

int IOMuxEpoll::WaitingIO(FiredEvList& out, int32_t ms) {
  const int ret_count =
      ::epoll_wait(epoll_, &ep_evs_[0], kMaxFiredEvOnePoll, ms);
  if (ret_count < 0) {  // error
    int32_t err = errno;
    LOG(ERROR) << "fd:" << epoll_ << " epoll_wait error:" << StrError(err);
    return 0;
  }

  if (out.size() < ret_count) {
    out.resize(ret_count, {-1, LtEv::LT_EVENT_NONE});
  }
  for (int idx = 0; idx < ret_count; idx++) {
    struct epoll_event& ev = ep_evs_[idx];
    DCHECK(fd_mgr_.GetFdEvent(ev.data.fd) != NULL);

    out[idx] = {ev.data.fd, ToLtEvent(ev.events)};
    // fd_id = ev.data.fd;
    // out[idx].event_mask = ToLtEvent(ev.events);
  }
  return ret_count;
}

LtEvent IOMuxEpoll::ToLtEvent(const uint32_t epoll_ev) {
  LtEvent event = LtEv::LT_EVENT_NONE;

  if (epoll_ev & EPOLLERR) {
    event |= LtEv::LT_EVENT_ERROR;
  }
  // case readable:
  // case hang out: but can read till EOF
  if (epoll_ev & EPOLLHUP || epoll_ev & EPOLLIN) {
    event |= LtEv::LT_EVENT_READ;
  }
  // writable
  if (epoll_ev & EPOLLOUT) {
    event |= LtEv::LT_EVENT_WRITE;
  }
  // peer close
  if (epoll_ev & EPOLLRDHUP) {
    event |= LtEv::LT_EVENT_CLOSE;
  }
  return event;
}

uint32_t IOMuxEpoll::ToEpollEvent(const LtEvent& lt_ev, bool add_extr) {
  uint32_t epoll_ev = 0;
  if (lt_ev & LtEv::LT_EVENT_READ) {
    epoll_ev |= EPOLLIN;
  }
  if (lt_ev & LtEv::LT_EVENT_WRITE) {
    epoll_ev |= EPOLLOUT;
  }
  if ((lt_ev & LtEv::LT_EVENT_CLOSE) || add_extr) {
    epoll_ev |= EPOLLRDHUP;
  }
  return epoll_ev;
}

FdEvent* IOMuxEpoll::FindFdEvent(int fd) {
  return fd_mgr_.GetFdEvent(fd);
}

void IOMuxEpoll::AddFdEvent(FdEvent* fd_ev) {
  CHECK(fd_mgr_.Add(fd_ev) == FdEventMgr::Success);
  EpollCtl(fd_ev, EPOLL_CTL_ADD);
}

void IOMuxEpoll::DelFdEvent(FdEvent* fd_ev) {
  EpollCtl(fd_ev, EPOLL_CTL_DEL);
  fd_mgr_.Remove(fd_ev);
}

void IOMuxEpoll::UpdateFdEvent(FdEvent* fd_ev) {
  if (0 != EpollCtl(fd_ev, EPOLL_CTL_MOD)) {
    LOG(ERROR) << "update fd event failed:" << fd_ev->EventInfo();
    return;
  }
  int32_t fd = fd_ev->GetFd();
  if (fd_mgr_.GetFdEvent(fd_ev->GetFd()) == NULL) {
    fd_mgr_.Add(fd_ev);
  }
}

int IOMuxEpoll::EpollCtl(FdEvent* fdev, int opt) {
  struct epoll_event ev;

  int fd = fdev->GetFd();

  ev.data.fd = fd;
  ev.events = ToEpollEvent(fdev->MonitorEvents());
  if (fdev->EdgeTriggerMode()) {
    ev.events |= EPOLLET;
  }

  int ret = ::epoll_ctl(epoll_, opt, fd, &ev);
  LOG_IF(ERROR, ret != 0) << "apply epoll_ctl opt " << EpollOptToString(opt)
                          << " on fd " << fd
                          << " failed, errno:" << StrError(errno)
                          << " events:" << fdev->MonitorEventAsString();

  return ret;
}

std::string IOMuxEpoll::EpollOptToString(int opt) {
  switch (opt) {
    case EPOLL_CTL_ADD:
      return "EPOLL_CTL_ADD";
    case EPOLL_CTL_DEL:
      return "EPOLL_CTL_DEL";
    case EPOLL_CTL_MOD:
      return "EPOLL_CTL_MOD";
    default:
      break;
  }
  return "BAD CTL OPTION";
}

}  // namespace base
