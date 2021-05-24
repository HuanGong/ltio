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

#ifndef _LT_NET_CLIENT_INTERCEPTOR_H_H
#define _LT_NET_CLIENT_INTERCEPTOR_H_H

#include <initializer_list>

namespace lt {
namespace net {

class Client;
class CodecMessage;

class Interceptor {
public:
  Interceptor() {};
  virtual ~Interceptor() {};
  virtual CodecMessage*
    Intercept(const Client* c, const CodecMessage* req) = 0;
};
typedef std::initializer_list<Interceptor*> InterceptArgList;


}} //end lt::net
#endif
