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

#ifndef _NET_IO_SERVICE_H_H
#define _NET_IO_SERVICE_H_H

#include <atomic>
#include <unordered_set>

#include "base/ip_endpoint.h"
#include "codec/codec_message.h"
#include "codec/codec_service.h"
#include "net_callback.h"
#include "socket_acceptor.h"

#ifdef LTIO_HAVE_SSL
#include "base/crypto/lt_ssl.h"
#endif

namespace lt {
namespace net {

class IOService;

/* IOserviceDelegate Provide the some notify to its owner and got the IOWork
 * loop for IO*/
class IOServiceDelegate {
public:
  virtual ~IOServiceDelegate(){};
  virtual base::MessageLoop* GetNextIOWorkLoop() = 0;

  /* use for couting connection numbers and limit
   * max connections; return false when reach limits,
   * otherwise return true */
  virtual bool CanCreateNewChannel() { return true; }

  virtual void IncreaseChannelCount() = 0;
  virtual void DecreaseChannelCount() = 0;
  /* do other config things for this ioservice; return false will kill this
   * service*/
  virtual void IOServiceStarted(const IOService* ioservice){};
  virtual void IOServiceStoped(const IOService* ioservice){};

  virtual void OnRequestMessage(const RefCodecMessage& request) = 0;

#ifdef LTIO_HAVE_SSL //server should provider this
  virtual base::SSLCtx* GetSSLContext() {return nullptr;}
#endif
};

/* Every IOService own a acceptor and listing on a adress,
 * handle incomming connection from acceptor and manager
 * them on working-messageloop */
class IOService : public EnableShared(IOService),
                  public SocketAcceptor::Actor,
                  public CodecService::Delegate {
public:
  /* Must Construct in ownerloop, why? bz we want all io level is clear and tiny
   * it only handle io relative things, it's easy! just post a task IOMain at
   * everything begin,
   *
   * A IOService Accept listen a local address and accept incoming connection;
   * every connections will bind to protocol service*/
  IOService(const IPEndPoint& local,
            const std::string protocol,
            base::MessageLoop* workloop,
            IOServiceDelegate* delegate);

  ~IOService();

  void Start();
  void Stop();

  base::MessageLoop* AcceptorLoop() { return acpt_io_; }
  const std::string& IOServiceName() const { return service_name_; }
  bool IsRunning() { return acceptor_ && acceptor_->IsListening(); }

private:
  // void HandleProtoMessage(RefCodecMessage message);
  /* create a new connection channel */
  void OnNewConnection(int, const IPEndPoint&) override;

  // override from CodecService::Delegate to manager[remove] from managed list
  void OnCodecMessage(const RefCodecMessage& message) override;
  void OnProtocolServiceGone(const RefCodecService& service) override;

  void StoreProtocolService(const RefCodecService);
  void RemoveProtocolService(const RefCodecService);

  RefCodecService CreateCodeService(int fd, const IPEndPoint& ep);

  // bool as_dispatcher_;
  std::string protocol_;

  SocketAcceptorPtr acceptor_;
  base::MessageLoop* acpt_io_;

  /* interface to owner and handler */
  IOServiceDelegate* delegate_;
  bool is_stopping_ = false;

  uint64_t channel_count_;
  std::string service_name_;
  std::unordered_set<RefCodecService> codecs_;
};

}  // namespace net
}  // namespace lt
#endif
