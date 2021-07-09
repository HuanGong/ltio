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

#ifndef LIGHTING_NET_SOCKET_UTILS_H
#define LIGHTING_NET_SOCKET_UTILS_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include "base/ip_endpoint.h"

/* about this code, a beeter refrence is muduo code, most of this from
 * chenshuo's impliment*/
namespace lt {
namespace net {
namespace socketutils {

#define SocketFd int

SocketFd CreateNoneBlockTCP(sa_family_t family, int type = IPPROTO_TCP);
SocketFd CreateNoneBlockUDP(sa_family_t family, int type = IPPROTO_UDP);

int ListenSocket(SocketFd fd);

int BindSocketFd(SocketFd fd, const struct sockaddr* addr);

int SocketConnect(SocketFd fd, const struct sockaddr* addr, int* err);

SocketFd AcceptSocket(SocketFd fd, struct sockaddr* addr, int* err);

ssize_t Read(SocketFd fd, void* buf, size_t count);

ssize_t Write(SocketFd fd, const void* buf, size_t count);

ssize_t ReadV(SocketFd fd, const struct iovec* iov, int iovcnt);

void CloseSocket(SocketFd fd);

void ShutdownWrite(SocketFd fd);

int GetSocketError(SocketFd fd);

bool GetPeerEndpoint(int sock, IPEndPoint* ep);
bool GetLocalEndpoint(int sock, IPEndPoint* ep);

struct sockaddr GetPeerAddrIn(int sockfd);
struct sockaddr GetLocalAddrIn(int sockfd);

bool IsSelfConnect(int sockfd);

bool ReUseSocketPort(SocketFd, bool reuse);
bool ReUseSocketAddress(SocketFd socket_fd, bool reuse);

void KeepAlive(SocketFd, bool alive);
void TCPNoDelay(SocketFd fd);
}  // namespace socketutils
}  // namespace net
}  // namespace lt
#endif
