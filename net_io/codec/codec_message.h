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

#ifndef NET_PROTOCOL_MESSAGE_H
#define NET_PROTOCOL_MESSAGE_H

#include <string>
#include <net_io/net_callback.h>
#include <base/closure/closure_task.h>

namespace base {
  class MessageLoop;
}

namespace lt {
namespace net {

enum class MessageType {
  kRequest,
  kResponse,
};

typedef enum {
  kSuccess = 0,
  kTimeOut = 1,
  kConnBroken = 2,
  kBadMessage = 3,
  kNotConnected = 4,
} MessageCode;

typedef struct {
  base::MessageLoop* io_loop;
  WeakCodecService codec;
} IOContext;

typedef struct {
  base::MessageLoop* loop;
  base::LtClosure resumer_fn;
} WorkContext;

class CodecMessage;
typedef std::weak_ptr<CodecMessage> WeakCodecMessage;
typedef std::shared_ptr<CodecMessage> RefCodecMessage;
typedef std::function<void(const RefCodecMessage&)> ProtoMessageHandler;

#define RefCast(Type, RefObj) std::static_pointer_cast<Type>(RefObj)

class CodecMessage {
public:
  CodecMessage(MessageType t);
  virtual ~CodecMessage();

  const MessageType& GetMessageType() const {return type_;};
  bool IsRequest() const {return type_ == MessageType::kRequest;};

  void SetRemoteHost(const std::string& h) {remote_host_ = h;}
  const std::string& RemoteHost() const {return remote_host_;}

  IOContext& GetIOCtx() {return io_context_;}
  void SetIOCtx(const RefCodecService& service);

  WorkContext& GetWorkCtx() {return work_context_;}
  void SetWorkerCtx(base::MessageLoop* loop);
  void SetWorkerCtx(base::MessageLoop* loop, base::LtClosure resumer);

  MessageCode FailCode() const;
  void SetFailCode(MessageCode reason);

  void SetResponse(const RefCodecMessage& response);
  CodecMessage* RawResponse() {return response_.get();}
  const RefCodecMessage& Response() const {return response_;}

  const char* TypeAsStr() const;
  bool IsResponded() const {return responded_;}

  /* use for those async-able protocol message,
   * use for matching request and response*/
  virtual bool AsHeartbeat() {return false;};
  virtual bool IsHeartbeat() const {return false;};
  virtual void SetAsyncId(uint64_t id) {};
  virtual const uint64_t AsyncId() const {return 0;};

  //a message info for dubuging or testing
  virtual const std::string Dump() const {return "";};
  const static RefCodecMessage kNullMessage;
protected:
  IOContext io_context_;
  WorkContext work_context_;
private:
  MessageType type_;
  std::string remote_host_;
  MessageCode fail_code_;
  bool responded_ = false;
  RefCodecMessage response_;
};

}}
#endif
