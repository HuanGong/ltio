#ifndef _LT_FW_RAPID_MESSAGE_H_
#define _LT_FW_RAPID_MESSAGE_H_

#include <sstream>
#include <string>

#include <net_io/channel.h>
#include <net_io/codec/codec_message.h>

#include <glog/logging.h>

using namespace lt;

typedef struct RapidHeader {
  uint32_t size = sizeof(RapidHeader);  // total package length in bytes
  uint32_t seqid;
  uint8_t type;  //
  uint8_t version;
  uint16_t cmdid;
  uint32_t extra;
} RapidHeader;

class FwRapidMessage : public lt::net::CodecMessage {
public:
  typedef FwRapidMessage ResponseType;
  typedef std::shared_ptr<FwRapidMessage> RefFwRapidMessage;

  static bool KeepQueue() { return false; }
  static bool WithHeartbeat() { return true; }

  static RefFwRapidMessage Create(bool request) {
    auto t = request ? net::MessageType::kRequest : net::MessageType::kResponse;
    auto msg = std::make_shared<FwRapidMessage>(t);
    msg->header_.size = sizeof(RapidHeader);
    return msg;
  }

  static RefFwRapidMessage CreateResponse(const FwRapidMessage* request) {
    auto res = Create(false);
    res->header_ = request->header_;
    res->header_.size = sizeof(RapidHeader);
    res->header_.extra = 0;
    return res;
  }

  bool EncodeTo(net::SocketChannel* ch) {
    VLOG(GLOG_VTRACE) << __FUNCTION__ << " frame size:" << header_.size;
    if (ch->Send((const char*)(&header_), sizeof(RapidHeader)) < 0) {
      return false;
    }
    return ch->Send(content_.data(), content_.size()) >= 0;
  }

  static RefFwRapidMessage Decode(net::IOBuffer* buffer, bool server_side) {
    if (buffer->CanReadSize() < sizeof(RapidHeader)) {
      return NULL;
    }
    uint32_t frame_size = *(uint32_t*)(buffer->GetRead());
    if (buffer->CanReadSize() < frame_size) {
      return NULL;
    }
    auto message = Create(server_side);
    ::memcpy(&message->header_, buffer->GetRead(), sizeof(RapidHeader));
    const uint64_t body_size = message->header_.size - sizeof(RapidHeader);
    buffer->Consume(sizeof(RapidHeader));

    if (body_size > 0) {
      message->content_ =
          std::string((const char*)buffer->GetRead(), body_size);
      buffer->Consume(body_size);
    }
    return message;
  }

  FwRapidMessage(net::MessageType t) : CodecMessage(t) {}
  ~FwRapidMessage(){};

  uint8_t Type() const { return header_.type; }
  void SetType(uint8_t t) { header_.type = t; }

  uint16_t CmdId() const { return header_.cmdid; }
  void SetCmdId(uint16_t id) { header_.cmdid = id; }

  uint8_t Version() const { return header_.version; }
  void SetVersion(uint8_t ver) { header_.version = ver; }

  const std::string& Content() const { return content_; }
  void SetContent(const std::string& c) {
    header_.size = sizeof(RapidHeader) + c.size();
    content_ = c;
  }

  // overide from protocolmessage
  void SetAsyncId(uint64_t id) override { header_.seqid = id; }
  const uint64_t AsyncId() const override { return header_.seqid; };

  bool AsHeartbeat() override {
    SetType(0xFF);
    return true;
  };
  bool IsHeartbeat() const override { return Type() == 0xFF; };
  const std::string Dump() const override {
    std::ostringstream oss;
    oss << "header=[seqid:" << header_.seqid << ", type:" << int(header_.type)
        << ", cmd:" << int(header_.cmdid) << ", ver:" << int(header_.version)
        << ", extr:" << int(header_.extra) << "], body=[" << content_ << "];";
    return oss.str();
  };

private:
  RapidHeader header_;
  std::string content_;
};

#endif
