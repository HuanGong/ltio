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

#include "http_response.h"
#include <sstream>
#include "http_constants.h"

#include <base/base_constants.h>
#include <base/utils/string/str_utils.h>
#include <thirdparty/http_parser/http_parser.h>
#include "glog/logging.h"

namespace lt {
namespace net {

// static
RefHttpResponse HttpResponse::CreateWithCode(uint16_t code) {
  auto r = std::make_shared<HttpResponse>();
  r->SetResponseCode(code);
  return r;
}

HttpResponse::HttpResponse()
  : CodecMessage(MessageType::kResponse),
    keepalive_(false),
    http_major_(1),
    http_minor_(1) {}

HttpResponse::~HttpResponse() {}

std::string& HttpResponse::MutableBody() {
  return body_;
}
const std::string& HttpResponse::Body() const {
  return body_;
}

KeyValMap& HttpResponse::MutableHeaders() {
  return headers_;
}

const KeyValMap& HttpResponse::Headers() const {
  return headers_;
}

bool HttpResponse::HasHeaderField(const std::string& field) const {
  return headers_.find(field) != headers_.end();
}

void HttpResponse::InsertHeader(const char* k, const char* v) {
  headers_.insert(std::make_pair(k, v));
}

void HttpResponse::InsertHeader(const std::string& k, const std::string& v) {
  headers_.insert(std::make_pair(k, v));
}

const std::string& HttpResponse::GetHeader(const std::string& field) const {
  auto iter = headers_.find(field);
  if (iter != headers_.end()) {
    return iter->second;
  }
  return base::kNullString;
}

const std::string HttpResponse::Dump() const {
  std::ostringstream oss;
  oss << "{\"type\": \"" << TypeAsStr() << "\""
      << ", \"http_major\": " << (int)http_major_
      << ", \"http_minor\": " << (int)http_minor_
      << ", \"status_code\": " << (int)status_code_;

  for (const auto& pair : headers_) {
    oss << ", \"header." << pair.first << "\": \"" << pair.second << "\"";
  }
  oss << ", \"keep_alive\": " << keepalive_ << ", \"body\": \"" << body_ << "\""
      << "}";
  return oss.str();
}

bool HttpResponse::IsKeepAlive() const {
  return keepalive_;
}

void HttpResponse::SetKeepAlive(bool alive) {
  keepalive_ = alive;
}

uint16_t HttpResponse::ResponseCode() const {
  return status_code_;
}

void HttpResponse::SetResponseCode(uint16_t code) {
  status_code_ = code;
}

std::string HttpResponse::StatusCodeInfo() const {
  const char* desc = http_status_str(http_status(status_code_));
  return desc;
}

}  // namespace net
};  // namespace lt
