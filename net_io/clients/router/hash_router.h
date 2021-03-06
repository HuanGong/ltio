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

#ifndef LT_NET_HASH_CLIENT_ROUTER_H_H
#define LT_NET_HASH_CLIENT_ROUTER_H_H

#include <net_io/clients/client.h>
#include <net_io/codec/codec_message.h>
#include <memory>
#include <vector>
#include "client_router.h"

namespace lt {
namespace net {

template <typename Hasher>
class HashRouter : public ClientRouter {
public:
  HashRouter(){};
  virtual ~HashRouter(){};

  void AddClient(RefClient&& client) override {
    clients_.push_back(std::move(client));
  };

  RefClient GetNextClient(const std::string& hash_key,
                          CodecMessage* hint_message = NULL) override {
    uint64_t value = hasher_(hash_key);
    RefClient client = clients_[value % clients_.size()];
    return client;
  };

private:
  Hasher hasher_;
  std::vector<RefClient> clients_;
};

}  // namespace net
}  // namespace lt

#endif
