
#include <stdlib.h>
#include <unistd.h>
#include <atomic>
#include <iostream>

#include "glog/logging.h"

#include <base/message_loop/message_loop.h>
#include <base/time/time_utils.h>
#include <net_io/clients/router/maglev_router.h>
#include <thirdparty/murmurhash/MurmurHash3.h>
#include <thirdparty/catch/catch.hpp>

using namespace lt;

using Endpoint = net::lb::Endpoint;

TEST_CASE("maglev.distribution", "[meglev]") {
  LOG(INFO) << " start test client.maglev";

  std::vector<Endpoint> eps;
  for (uint32_t i = 0; i < 10; i++) {
    Endpoint s = {i, (i + 1) * 1000, 10000 * (i + 1) + (100 * i)};
    LOG(INFO) << s.num << ", " << s.weight << ", " << s.hash;
    eps.push_back(s);
  }

  LOG(INFO) << " GenerateMaglevHash";
  auto lookup_table = net::lb::MaglevV2::GenerateHashRing(eps);
  LOG(INFO) << " GenerateMaglevHash success";

  std::map<uint32_t, uint32_t> freq;
  for (auto v : lookup_table) {
    freq[v]++;
  }

  for (auto kv : freq) {
    double p = double(kv.second) / net::lb::MaglevV2::RingSize();
    LOG(INFO) << "node chring dist: ep:" << kv.first << " p:" << 100 * p << "%";
  }

  freq.clear();
  for (uint32_t i = 0; i < 10000000; i++) {
    uint32_t out = 0;
    MurmurHash3_x86_32(&i, sizeof(i), 0x80000000, &out);
    auto node_id = lookup_table[out % lookup_table.size()];
    freq[node_id]++;
  }

  for (auto kv : freq) {
    double p = double(kv.second) / 10000000;
    LOG(INFO) << "random key dist: ep:" << kv.first << " p:" << 100 * p << "%";
  }

  LOG(INFO) << " end test maglev end";
}

TEST_CASE("maglev.base", "[meglev fixed weight distribution]") {
  LOG(INFO) << " start test Maglev.FixedWeight";

  std::vector<Endpoint> eps;
  for (uint32_t i = 0; i <= 10; i++) {
    if (i == 5)
      continue;

    uint32_t hash_value = 0;
    MurmurHash3_x86_32(&i, sizeof(i), 0x55, &hash_value);

    Endpoint s = {i, 100, hash_value};
    LOG(INFO) << s.num << ", " << s.weight << ", " << s.hash;
    eps.push_back(s);
  }

  LOG(INFO) << " GenerateMaglevHash, count:" << eps.size();
  auto lookup_table = net::lb::MaglevV2::GenerateHashRing(eps);

  std::map<int, uint32_t> freq;
  for (auto v : lookup_table) {
    freq[v]++;
  }
  LOG(INFO) << " frequency size:" << freq.size();

  for (auto kv : freq) {
    double p = double(kv.second) / net::lb::MaglevV2::RingSize();
    LOG(INFO) << "node chring dist: ep:" << kv.first << " p:" << 100 * p << "%";
  }

  freq.clear();
  for (uint32_t i = 0; i < 10000000; i++) {
    uint32_t out = 0;
    MurmurHash3_x86_32(&i, sizeof(i), 0x80000000, &out);
    auto node_id = lookup_table[out % lookup_table.size()];
    freq[node_id]++;
  }
  LOG(INFO) << " random test frequency size:" << freq.size();

  for (auto kv : freq) {
    double p = double(kv.second) / 10000000;
    LOG(INFO) << "random key dist: ep:" << kv.first << " p:" << 100 * p << "%";
  }

  LOG(INFO) << " end test Maglev.FixedWeight";
}
