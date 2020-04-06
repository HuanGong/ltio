#ifndef _LT_NET_CLIENT_CHANNEL_H
#define _LT_NET_CLIENT_CHANNEL_H

#include "net_io/url_utils.h"
#include "net_io/net_callback.h"
#include "net_io/protocol/proto_message.h"
#include "net_io/protocol/proto_service.h"

#include "client_base.h"

namespace lt {
namespace net {

class ClientChannel;

REF_TYPEDEFINE(ClientChannel);

class ClientChannel : public ProtoServiceDelegate {
public:
  class Delegate {
  public:
    virtual const ClientConfig& GetClientConfig() const = 0;
    virtual const url::RemoteInfo& GetRemoteInfo() const = 0;

    virtual void OnClientChannelInited(const ClientChannel* channel) = 0;
    virtual void OnClientChannelClosed(const RefClientChannel& channel) = 0;
    virtual void OnRequestGetResponse(const RefProtocolMessage&, const RefProtocolMessage&) = 0;
  };

  enum State {
    kInitialing   = 0,
    kReady        = 1,
    kClosing      = 2,
    kDisconnected = 3
  };

  ClientChannel(Delegate* d, const RefProtoService& service);
  virtual ~ClientChannel();

  void ResetDelegate();
  virtual void StartClientChannel();
  virtual void CloseClientChannel();
  // a change for close all inprogress request
  virtual void BeforeCloseChannel() = 0;
  virtual void SendRequest(RefProtocolMessage request) = 0;

  bool Ready() const {return state_ == kReady;}
  bool Closing() const {return state_ == kClosing;}
  bool Initializing() const {return state_ == kInitialing;}

  void SetRequestTimeout(uint32_t ms) {request_timeout_ = ms;};
  base::MessageLoop* IOLoop() {return protocol_service_->IOLoop();};

  //override from ProtoServiceDelegate
  const url::RemoteInfo* GetRemoteInfo() const override;
  void OnProtocolServiceGone(const RefProtoService& service) override;
  void OnProtocolServiceReady(const RefProtoService& service) override;
protected:
  void OnHearbeatTimerInvoke();
  std::string ConnectionInfo() const;

  // return true when message be handled, otherwise return false
  bool HandleResponse(const RefProtocolMessage& req,
                      const RefProtocolMessage& res);
protected:
  Delegate* delegate_;
  State state_ = kInitialing;
  RefProtoService protocol_service_;
  uint32_t request_timeout_ = 5000; //5s
  base::TimeoutEvent* heartbeat_timer_ = NULL;
  RefProtocolMessage heartbeat_message_;
};

RefClientChannel CreateClientChannel(ClientChannel::Delegate*, const RefProtoService&);

}}//end namespace
#endif
