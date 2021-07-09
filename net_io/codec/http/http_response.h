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

#ifndef _NET_HTTP_RESPONSE_MESSAGE_H
#define _NET_HTTP_RESPONSE_MESSAGE_H

#include <map>
#include <sstream>

#include <net_io/codec/codec_message.h>

namespace lt {
namespace net {

class HttpResponse;

typedef std::map<std::string, std::string> KeyValMap;
typedef std::shared_ptr<HttpResponse> RefHttpResponse;

class HttpResponse : public CodecMessage {
public:
  HttpResponse();
  ~HttpResponse();

  static RefHttpResponse CreateWithCode(uint16_t code);

  std::string& MutableBody();
  const std::string& Body() const;

  KeyValMap& MutableHeaders();

  const KeyValMap& Headers() const;
  bool HasHeaderField(const std::string&) const;
  void InsertHeader(const std::string&, const std::string&);
  void InsertHeader(const char*, const char*);
  const std::string& GetHeader(const std::string&) const;

  bool IsKeepAlive() const;
  void SetKeepAlive(bool alive);

  const std::string Dump() const override;

  uint16_t ResponseCode() const;
  void SetResponseCode(uint16_t code);
  std::string StatusCodeInfo() const;
  int VersionMajor() const { return http_major_; }
  int VersionMinor() const { return http_minor_; }

private:
  friend class HttpCodecService;
  friend class ResParseContext;

  bool keepalive_;
  uint8_t http_major_;
  uint8_t http_minor_;
  uint16_t status_code_;

  std::string body_;
  KeyValMap headers_;
};

}  // namespace net
}  // namespace lt
#endif
