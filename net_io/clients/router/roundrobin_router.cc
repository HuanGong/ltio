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

#include "roundrobin_router.h"

namespace lt {
namespace net {

void RoundRobinRouter::AddClient(RefClient&& client) {
  clients_.push_back(std::move(client));
}

RefClient RoundRobinRouter::GetNextClient(const std::string& key,
                                          CodecMessage* request) {
  uint32_t idx = round_index_.fetch_add(1) % clients_.size();
  return clients_[idx];
}

}  // namespace net
}  // namespace lt
