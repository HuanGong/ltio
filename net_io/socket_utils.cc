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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>    // snprintf
#include <strings.h>  // bzero
#include <sys/socket.h>
#include <unistd.h>
#include <string>

#include "base/ip_endpoint.h"
#include "base/sockaddr_storage.h"
#include "glog/logging.h"

#include <arpa/inet.h>
#include <base/base_constants.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "base/utils/sys_error.h"
#include "socket_utils.h"

namespace lt {
namespace net {

namespace socketutils {

SocketFd CreateNoneBlockTCP(sa_family_t family, int type) {
  int sockfd =
      ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, type);
  LOG_IF(ERROR, sockfd == -1)
      << " create tcp socket err:[" << base::StrError() << "]";
  return sockfd;
}

SocketFd CreateNoneBlockUDP(sa_family_t family, int type) {
  int sockfd =
      ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, type);
  LOG_IF(ERROR, sockfd == -1)
      << " create udp socket err:[" << base::StrError() << "]";
  return sockfd;
}

int BindSocketFd(SocketFd sockfd, const struct sockaddr* addr) {
  int ret =
      ::bind(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
  LOG_IF(ERROR, ret < 0) << __FUNCTION__ << " bind err:[" << base::StrError()
                         << "]";
  return ret;
}

int SocketConnect(SocketFd fd, const struct sockaddr* addr, int* err) {
  int ret =
      ::connect(fd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
  LOG_IF(ERROR, ret < 0 && errno != EINPROGRESS)
      << __FUNCTION__ << " connect failed, err:[" << base::StrError() << "]";
  if (ret < 0 && err != nullptr) {
    *err = errno;
  }
  return ret;
}

int ListenSocket(SocketFd socket_fd) {
  int ret = ::listen(socket_fd, SOMAXCONN);
  LOG_IF(ERROR, ret < 0) << __FUNCTION__ << " listen fd:[" << socket_fd
                         << "] err:[" << base::StrError() << "]";
  return ret;
}

SocketFd AcceptSocket(SocketFd sockfd, struct sockaddr* addr, int* err) {
  socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
  int connfd = ::accept4(sockfd, addr, &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
  int savedErrno = 0;
  if (connfd < 0) {
    savedErrno = errno;
    VLOG(GLOG_VERROR) << "SocketUtils::AcceptSocket Error: fd:" << sockfd;
    switch (savedErrno) {
      case EAGAIN:
      case ECONNABORTED:
      case EINTR:
      case EPROTO:  // ???
      case EPERM:
      case EMFILE:  // per-process lmit of open file desctiptor ???
        // expected errors
        errno = savedErrno;
        break;
      case EBADF:
      case EFAULT:
      case EINVAL:
      case ENFILE:
      case ENOBUFS:
      case ENOMEM:
      case ENOTSOCK:
      case EOPNOTSUPP:
        // unexpected errors
        VLOG(GLOG_VERROR) << "unexpected error of ::accept " << savedErrno;
        break;
      default:
        VLOG(GLOG_VERROR) << "unknown error of ::accept " << savedErrno;
        break;
    }
  }
  if (err) {
    *err = savedErrno;
  }
  return connfd;
}

ssize_t Read(SocketFd sockfd, void* buf, size_t count) {
  return ::read(sockfd, buf, count);
}

ssize_t Write(SocketFd sockfd, const void* buf, size_t count) {
  return ::write(sockfd, buf, count);
}

ssize_t ReadV(SocketFd sockfd, const struct iovec* iov, int iovcnt) {
  return ::readv(sockfd, iov, iovcnt);
}

void CloseSocket(SocketFd sockfd) {
  LOG_IF(ERROR, (::close(sockfd) < 0))
      << __func__ << " close socket error:" << base::StrError(errno);
}

void ShutdownWrite(SocketFd sockfd) {
  LOG_IF(ERROR, ::shutdown(sockfd, SHUT_WR) < 0)
      << __FUNCTION__ << " shutdown write error:" << base::StrError(errno);
}

int GetSocketError(SocketFd sockfd) {
  int optval;
  socklen_t optlen = static_cast<socklen_t>(sizeof optval);

  if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
    return errno;
  }
  return optval;
}

bool GetPeerEndpoint(int sock, IPEndPoint* ep) {
  struct sockaddr peeraddr;
  bzero(&peeraddr, sizeof peeraddr);
  socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
  if (::getpeername(sock, &peeraddr, &addrlen) < 0) {
    LOG(ERROR) << __func__ << " Failed";
    return false;
  }
  return ep->FromSockAddr(&peeraddr, addrlen);
}

bool GetLocalEndpoint(int sock, IPEndPoint* ep) {
  struct sockaddr localaddr;
  bzero(&localaddr, sizeof localaddr);

  socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
  if (::getsockname(sock, &localaddr, &addrlen) < 0) {
    LOG(ERROR) << __func__ << " Failed";
    return false;
  }
  return ep->FromSockAddr(&localaddr, addrlen);
}

struct sockaddr GetLocalAddrIn(int sockfd) {
  struct sockaddr localaddr;
  bzero(&localaddr, sizeof localaddr);

  socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
  if (::getsockname(sockfd, &localaddr, &addrlen) < 0) {
    LOG(ERROR) << "socketutils::GetLocalAddrIn Call getsockname ERROR";
  }
  return localaddr;
}

struct sockaddr GetPeerAddrIn(int sockfd) {
  struct sockaddr peeraddr;
  bzero(&peeraddr, sizeof peeraddr);
  socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
  if (::getpeername(sockfd, &peeraddr, &addrlen) < 0) {
    LOG(ERROR) << "socketutils::GetPeerAddr Failed";
  }
  return peeraddr;
}

uint32_t SockAddrFamily(struct sockaddr* addr) {
  const struct sockaddr_in* laddr4 =
      reinterpret_cast<struct sockaddr_in*>(addr);
  if (laddr4->sin_family == AF_INET) {
    return AF_INET;
  }
  return AF_INET6;
}

bool IsSelfConnect(int sockfd) {
  IPEndPoint peer_ep;
  IPEndPoint local_ep;
  if (!GetLocalEndpoint(sockfd, &local_ep) ||
      !GetPeerEndpoint(sockfd, &peer_ep)) {
    return false;
  }

  SockaddrStorage peer_storage;
  if (!peer_ep.ToSockAddr(peer_storage.AsSockAddr(), peer_storage.addr_len)) {
    return false;
  }

  SockaddrStorage local_storage;
  if (!local_ep.ToSockAddr(local_storage.AsSockAddr(),
                           local_storage.addr_len)) {
    return false;
  }

  switch (local_ep.GetSockAddrFamily()) {
    case AF_INET: {
      const struct sockaddr_in* raddr4 = peer_storage.AsSockAddrIn();
      const struct sockaddr_in* laddr4 = local_storage.AsSockAddrIn();

      return laddr4->sin_port == raddr4->sin_port &&
             laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;

    } break;
    case AF_INET6: {
      const struct sockaddr_in6* raddr6 = peer_storage.AsSockAddrIn6();
      const struct sockaddr_in6* laddr6 = local_storage.AsSockAddrIn6();

      return laddr6->sin6_port == raddr6->sin6_port &&
             (memcmp(&laddr6->sin6_addr, &raddr6->sin6_addr,
                     sizeof(raddr6->sin6_addr)) == 0);
    }
    default:
      break;
  };
  return false;
}

bool ReUseSocketAddress(SocketFd socket_fd, bool reuse) {
  int option = reuse ? 1 : 0;
  int r = ::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option,
                       static_cast<socklen_t>(sizeof option));
  LOG_IF(ERROR, r == -1) << " REUSEADDR Failed, fd:" << socket_fd
                         << " Reuse:" << reuse;
  return (r == 0);
}

bool ReUseSocketPort(SocketFd socket_fd, bool reuse) {
#ifdef SO_REUSEPORT
  int optval = reuse ? 1 : 0;
  int ret = ::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &optval,
                         static_cast<socklen_t>(sizeof optval));
  LOG_IF(ERROR, ret == -1) << "Set SO_REUSEPORT failed.";
  return ret == 0;
#else
  LOG(ERROR) << "SO_REUSEPORT is not supported."
#endif
}

void KeepAlive(SocketFd fd, bool alive) {
  int optval = alive ? 1 : 0;
  ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval,
               static_cast<socklen_t>(sizeof optval));
}

void TCPNoDelay(SocketFd fd) {
  int on = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on));
}

}  // namespace socketutils
}  // namespace net
}  // namespace lt
