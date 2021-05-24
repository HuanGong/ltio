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


#include <sstream>
#include "http_request.h"
#include "http_constants.h"

#include <base/base_constants.h>
#include <base/utils/string/str_utils.h>

namespace lt {
namespace net {

HttpRequest::HttpRequest()
  : CodecMessage(MessageType::kRequest),
    keepalive_(false),
    method_("GET"),
    url_("/"),
    http_major_(1),
    http_minor_(1),
    url_param_parsed_(false) {
}

HttpRequest::~HttpRequest() {
}

void HttpRequest::SetRequestURL(const char* url) {
  url_ = url;
}

void HttpRequest::SetRequestURL(const std::string& url) {
  url_ = url;
}

const std::string& HttpRequest::RequestUrl() const {
  return url_;
}

std::string& HttpRequest::MutableBody() {
  return body_;
}

const std::string& HttpRequest::Body() const {
  return body_;
}

KeyValMap& HttpRequest::MutableHeaders() {
  return headers_;
}

const KeyValMap& HttpRequest::Headers() const {
  return headers_;
}

bool HttpRequest::HasHeaderField(const char* f) const {
  return headers_.find(f) != headers_.end();
}

bool HttpRequest::HasHeaderField(const std::string& field) const {
  return headers_.find(field) != headers_.end();
}

void HttpRequest::InsertHeader(const char* k, const char* v) {
  headers_.insert(std::make_pair(k, v));
}

void HttpRequest::InsertHeader(const std::string& k, const std::string& v) {
  headers_.insert(std::make_pair(k, v));
}

const std::string& HttpRequest::GetHeader(const std::string& field) const {
  auto iter = headers_.find(field);
  if (iter != headers_.end()) {
    return iter->second;
  }
  return base::kNullString;
}

KeyValMap& HttpRequest::MutableParams() {
  return params_;
}

void HttpRequest::ParseUrlToParams() {
  url_param_parsed_ = true;
}

const std::string& HttpRequest::GetUrlParam(const char* key) {
  const std::string str_key(key);
  return GetUrlParam(str_key);
}
const std::string& HttpRequest::GetUrlParam(const std::string& key) {
  if (params_.empty() && !url_param_parsed_) {
    ParseUrlToParams();
  }
  const auto& val = params_.find(key);
  return val != params_.end() ? val->second : base::kNullString;
}

const std::string& HttpRequest::Method() const {
  return method_;
}

const std::string HttpRequest::Dump() const {
  std::ostringstream oss;
  oss << "{\"type\": \"" << TypeAsStr() << "\""
      << ", \"http_major\": " << (int)http_major_
      << ", \"http_minor\": " << (int)http_minor_
      << ", \"method\": \"" << Method() << "\""
      << ", \"request_url\": \"" << url_ << "\"";

  for (const auto& pair : headers_) {
    oss << ", \"header." << pair.first << "\": \"" << pair.second << "\"";
  }
  oss << ", \"keep_alive\": " << keepalive_
      << ", \"body\": \"" << body_ << "\""
      << "}";
  return std::move(oss.str());
}

void HttpRequest::SetMethod(const std::string method) {
  method_ = method;
  base::StrUtil::ToUpper(method_);
}

bool HttpRequest::IsKeepAlive() const {
  return keepalive_;
}

void HttpRequest::SetKeepAlive(bool alive) {
  keepalive_ = alive;
}

}};//end namespace net
