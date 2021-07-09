#include <stdlib.h>
#include <unistd.h>
#include <atomic>
#include <iostream>

#include <base/coroutine/co_runner.h>
#include <base/message_loop/message_loop.h>
#include <base/time/time_utils.h>
#include <base/utils/string/str_utils.h>
#include "base/closure/closure_task.h"
#include "glog/logging.h"

#include <thirdparty/catch/catch.hpp>

#include <glog/logging.h>
#include "net_io/codec/codec_factory.h"
#include "net_io/codec/codec_message.h"
#include "net_io/codec/codec_service.h"
#include "net_io/codec/http/http_request.h"
#include "net_io/codec/http/http_response.h"
#include "net_io/codec/line/line_message.h"
#include "net_io/codec/raw/raw_codec_service.h"
#include "net_io/codec/raw/raw_message.h"
#include "net_io/codec/redis/redis_request.h"
#include "net_io/codec/redis/redis_response.h"
#include "net_io/codec/redis/resp_codec_service.h"
#include "net_io/dispatcher/coro_dispatcher.h"
#include "net_io/socket_acceptor.h"
#include "net_io/socket_utils.h"
#include "net_io/tcp_channel.h"

#include <net_io/clients/router/client_router.h>
#include <net_io/clients/router/ringhash_router.h>
#include <net_io/clients/router/roundrobin_router.h>
#include "net_io/clients/client.h"
#include "net_io/clients/client_connector.h"

static std::atomic_int io_round_count;

const int bench_count = 1000000;
const int bench_concurrent = 50;

using namespace lt;

std::vector<base::MessageLoop*> InitLoop() {
  std::vector<base::MessageLoop*> lops;
  for (uint32_t i = 0; i < std::thread::hardware_concurrency(); i++) {
    base::MessageLoop* l = new base::MessageLoop();
    l->SetLoopName("io_" + std::to_string(i));
    l->Start();
    lops.push_back(l);
  }
  return lops;
}

std::vector<base::MessageLoop*> loops;

class RouterManager : public net::ClientDelegate {
public:
  base::MessageLoop* NextIOLoopForClient() {
    if (loops.empty()) {
      return NULL;
    }
    io_round_count++;
    return loops[io_round_count % loops.size()];
  }
};
RouterManager router_delegate;

TEST_CASE("client.base", "[http client]") {
  LOG(INFO) << " start test client.base, http client connections";

  base::MessageLoop loop;
  loop.SetLoopName("client");

  loop.Start();
  static const int connections = 10;

  net::url::RemoteInfo server_info;
  LOG_IF(ERROR, !net::url::ParseRemote("http://127.0.0.1:80", server_info))
      << " server can't be resolve";

  net::Client http_client(&loop, server_info);

  net::ClientConfig config;
  config.recon_interval = 100;
  config.message_timeout = 5000;
  config.connections = connections;

  http_client.Initialize(config);

  loop.PostDelayTask(NewClosure([&]() {
                       REQUIRE(http_client.ConnectedCount() == connections);
                       http_client.Finalize();
                       loop.QuitLoop();
                     }),
                     500);

  loop.WaitLoopEnd();

  LOG(INFO) << " end test client.base, http client connections";
}

TEST_CASE("client.async", "[http client]") {
  LOG(INFO) << " start test client.base, http client connections";

  base::MessageLoop loop;
  loop.SetLoopName("async_client");

  loop.Start();
  static const int connections = 3;

  net::url::RemoteInfo server_info;
  LOG_IF(ERROR, !net::url::ParseRemote("http://127.0.0.1:80", server_info))
      << " server can't be resolve";

  net::Client http_client(&loop, server_info);

  net::ClientConfig config;
  config.recon_interval = 100;
  config.message_timeout = 5000;
  config.connections = connections;

  http_client.Initialize(config);

  net::AsyncCallBack callback = [&](net::CodecMessage* response) {
    LOG(INFO) << __FUNCTION__ << " request back";
    LOG_IF(INFO, response) << "response:" << response->Dump();

    LOG(INFO) << __FUNCTION__ << " call FinalizeSync";
    http_client.Finalize();

    LOG(INFO) << __FUNCTION__ << " call quit loop";
    loop.QuitLoop();
  };

  loop.PostDelayTask(
      NewClosure([&]() {
        REQUIRE(http_client.ConnectedCount() == connections);

        net::RefHttpRequest request = std::make_shared<net::HttpRequest>();
        request->SetKeepAlive(true);
        request->SetRequestURL("/abck.txt");
        request->InsertHeader("Accept", "*/*");
        request->InsertHeader("Host", "127.0.0.1");
        request->InsertHeader("User-Agent", "curl/7.58.0");
        auto message = std::static_pointer_cast<net::CodecMessage>(request);
        http_client.AsyncDoRequest(message, callback);
      }),
      500);

  loop.WaitLoopEnd();

  LOG(INFO) << " end test client.base, http client connections";
}

TEST_CASE("client.http.request", "[http client send request]") {
  LOG(INFO) << " start test client.http.request, http client send request";

  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  static const int connections = 10;

  net::url::RemoteInfo server_info;
  LOG_IF(ERROR, !net::url::ParseRemote("http://127.0.0.1:80", server_info))
      << " server can't be resolve";

  net::Client http_client(&loop, server_info);

  net::ClientConfig config;
  config.recon_interval = 100;
  config.message_timeout = 5000;
  config.connections = connections;

  http_client.Initialize(config);

  int total_task = 1;
  int failed_request = 0;
  int success_request = 0;

  auto http_request_task = [&]() {
    net::RefHttpRequest request = std::make_shared<net::HttpRequest>();
    request->SetKeepAlive(true);
    request->SetRequestURL("/");
    request->InsertHeader("Accept", "*/*");
    request->InsertHeader("Host", "127.0.0.1");
    request->InsertHeader("User-Agent", "curl/7.58.0");

    net::HttpResponse* response = http_client.SendRecieve(request);
    if (response && request->FailCode() == net::MessageCode::kSuccess) {
      success_request++;
    } else {
      failed_request++;
    }
  };

  sleep(1);

  for (int i = 0; i < total_task; i++) {
    CO_GO& loop << http_request_task;
  }

  loop.PostDelayTask(
      NewClosure([&]() {
        REQUIRE(http_client.ConnectedCount() == connections);
        REQUIRE((failed_request + success_request) == total_task);

        http_client.Finalize();
        loop.QuitLoop();
      }),
      5000);

  loop.WaitLoopEnd();

  LOG(INFO) << " end test client.http.request, http client send request";
}

TEST_CASE("client.raw.request", "[raw client send request]") {
  LOG(INFO) << ">>>>>>> start test client.raw.request, raw client send request";

  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  net::url::RemoteInfo server_info;
  LOG_IF(ERROR, !net::url::ParseRemote("raw://127.0.0.1:5005", server_info))
      << " server can't be resolve";

  net::Client raw_router(&loop, server_info);

  static const int connections = 10;

  net::ClientConfig config;
  config.recon_interval = 1000;
  config.message_timeout = 5000;
  config.connections = connections;

  raw_router.Initialize(config);

  int total_task = 1;
  int failed_request = 0;
  int success_request = 0;

  auto raw_request_task = [&]() {
    auto request = net::LtRawMessage::Create(true);
    request->SetMethod(12);
    request->SetContent("RawRequest");

    net::LtRawMessage* response = raw_router.SendRecieve(request);

    if (response && request->FailCode() == net::MessageCode::kSuccess) {
      success_request++;
    } else {
      LOG(INFO) << " failed reason:" << request->FailCode();
      failed_request++;
    }
  };

  sleep(1);

  for (int i = 0; i < total_task; i++) {
    CO_GO& loop << raw_request_task;
  }

  loop.PostDelayTask(
      NewClosure([&]() {
        REQUIRE(raw_router.ConnectedCount() == connections);
        REQUIRE((failed_request + success_request) == total_task);

        raw_router.Finalize();
        loop.QuitLoop();
      }),
      5000);

  loop.WaitLoopEnd();
  LOG(INFO) << "<<<<<< end test client.raw.request, raw client send request";
}

TEST_CASE("client.timer.request", "[fetch resource every interval]") {
  LOG(INFO) << " start test client.timer.request, raw client send request";

  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  net::url::RemoteInfo server_info;
  LOG_IF(ERROR, !net::url::ParseRemote("raw://127.0.0.1:5005", server_info))
      << " server can't be resolve";

  net::Client raw_router(&loop, server_info);

  static const int connections = 10;

  {
    net::ClientConfig config;
    config.heartbeat_ms = 5000;
    config.recon_interval = 1000;
    config.message_timeout = 5000;
    config.connections = connections;

    raw_router.Initialize(config);
  }

  int send_count = 10;

  CO_GO& loop << [&]() {
    do {
      auto request = net::LtRawMessage::Create(true);
      request->SetMethod(12);
      request->SetContent("RawRequest");

      net::LtRawMessage* response = raw_router.SendRecieve(request);
      if (response && request->FailCode() == net::MessageCode::kSuccess) {
        // success  do your things
      }
      CO_SLEEP(50);
    } while (send_count--);

    loop.PostTask(NewClosure([&]() {
      raw_router.Finalize();

      loop.PostTask(NewClosure([&]() { loop.QuitLoop(); }));
    }));
  };

  loop.PostDelayTask(NewClosure([&]() {
                       LOG(INFO) << " timer.request ut timeout";
                       loop.QuitLoop();
                     }),
                     10000);

  loop.WaitLoopEnd();
  //LOG(INFO) << "system co count:" << base::CoroBase::SystemCoroutineCount();

  LOG(INFO) << " end test client.timer.request, raw client send request";
}

TEST_CASE("client.raw.bench", "[raw client send request benchmark]") {
  LOG(INFO)
      << " start test client.raw.bench, raw client send request benchmark";
  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  if (loops.empty()) {
    loops = InitLoop();
  }

  static std::atomic_int io_round_count;
  class RouterManager : public net::ClientDelegate {
  public:
    base::MessageLoop* NextIOLoopForClient() {
      if (loops.empty()) {
        return NULL;
      }
      io_round_count++;
      return loops[io_round_count % loops.size()];
    }
  };
  RouterManager router_delegate;

  net::url::RemoteInfo server_info;
  LOG_IF(ERROR, !net::url::ParseRemote("raw://127.0.0.1:5005", server_info))
      << " server can't be resolve";

  net::Client raw_router(&loop, server_info);

  static const int connections = 10;

  net::ClientConfig config;
  config.recon_interval = 1000;
  config.message_timeout = 5000;
  config.connections = connections;

  raw_router.SetDelegate(&router_delegate);
  raw_router.Initialize(config);

  std::atomic<int64_t> total_task;
  total_task = bench_count;

  std::atomic<int64_t> failed_request;
  failed_request = 0;
  std::atomic<int64_t> success_request;
  success_request = 0;

  auto raw_request_task = [&]() {
    while (total_task-- > 0) {
      auto request = net::LtRawMessage::Create(true);
      request->SetMethod(12);
      request->SetContent("RawRequest");

      net::LtRawMessage* response = raw_router.SendRecieve(request);

      if (response && request->FailCode() == net::MessageCode::kSuccess) {
        success_request++;
      } else {
        failed_request++;
      }

      if (failed_request + success_request == bench_count) {
        LOG(INFO) << " start bench finished.............<<<<<<"
                  << "success:" << success_request
                  << " failed: " << failed_request
                  << " test_count:" << bench_count;
        raw_router.Finalize();
        loop.QuitLoop();
      }
    };
  };

  sleep(5);
  LOG(INFO) << " start bench started.............<<<<<<";
  for (int i = 0; i < bench_concurrent; i++) {
    auto l = loops[i % loops.size()];
    CO_GO l << raw_request_task;
  }

  loop.WaitLoopEnd();
}

TEST_CASE("client.http.bench", "[http client send request benchmark]") {
  LOG(INFO)
      << " start test client.http.bench, http client send request benchmark";

  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  if (loops.empty()) {
    loops = InitLoop();
  }

  net::url::RemoteInfo server_info;
  // LOG_IF(ERROR, !net::url::ParseURI("http://www.ltio.com:5006", server_info))
  // << " server can't be resolve";
  LOG_IF(ERROR, !net::url::ParseRemote("http://127.0.0.1:5006", server_info))
      << " server can't be resolve";

  LOG(INFO) << "parse result, host:" << server_info.host
            << " ip:" << server_info.host_ip << " port:" << server_info.port
            << " protocol:" << server_info.protocol;

  net::Client http_client(&loop, server_info);

  const int connections = 10;

  net::ClientConfig config;
  config.recon_interval = 10;
  config.message_timeout = 5000;
  config.connections = connections;

  http_client.SetDelegate(&router_delegate);
  http_client.Initialize(config);

  std::atomic<int64_t> total_task;
  total_task = bench_count;

  std::atomic<int64_t> failed_request;
  failed_request = 0;
  std::atomic<int64_t> success_request;
  success_request = 0;

  auto request_task = [&]() {
    while (total_task-- > 0) {
      net::RefHttpRequest request = std::make_shared<net::HttpRequest>();
      request->SetKeepAlive(true);
      request->SetRequestURL("/");
      request->InsertHeader("Accept", "*/*");
      request->InsertHeader("User-Agent", "curl/7.58.0");

      net::HttpResponse* response = http_client.SendRecieve(request);
      if (response && request->FailCode() == net::MessageCode::kSuccess) {
        success_request++;
      } else {
        failed_request++;
      }

      if (failed_request + success_request == bench_count) {
        LOG(INFO) << " start bench finished.............<<<<<<"
                  << "success:" << success_request
                  << " failed: " << failed_request
                  << " test_count:" << bench_count;
        http_client.Finalize();
        loop.QuitLoop();
      }
    };
  };

  sleep(5);
  LOG(INFO) << " start bench started.............<<<<<<";
  for (int i = 0; i < bench_concurrent; i++) {
    auto l = loops[i % loops.size()];
    l->PostTask(NewClosure([=]() { CO_GO request_task; }));
  }
  loop.WaitLoopEnd();
}

TEST_CASE("client.redis_client", "[redis client]") {
  static const int connections = 2;

  LOG(INFO) << "start test client.router with redis protocal";

  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  std::vector<std::string> remote_hosts = {//"redis://localhost:6380?db=2",
                                           "redis://127.0.0.1:6379?db=3"};

  net::ClientConfig config;

  config.recon_interval = 1000;
  config.message_timeout = 5000;
  config.connections = connections;

  net::RoundRobinRouter router;
  for (auto& remote : remote_hosts) {
    net::url::RemoteInfo server_info;
    bool success = net::url::ParseRemote(remote, server_info);
    LOG_IF(ERROR, !success) << " server:" << remote << " can't be resolve";
    if (!success) {
      LOG(INFO) << "host:" << server_info.host << " ip:" << server_info.host_ip
                << " port:" << server_info.port
                << " protocol:" << server_info.protocol;
      return;
    }

    net::RefClient client(new net::Client(&loop, server_info));
    client->SetDelegate(&router_delegate);
    client->Initialize(config);

    router.AddClient(std::move(client));
  }

  auto task = [&]() {
    for (uint32_t i = 0; i < 3; i++) {
      auto redis_request = std::make_shared<net::RedisRequest>();

      redis_request->SetWithExpire("name", "huan.gong", 2000);
      // redis_request->Delete("name");
      redis_request->MGet("name", "abc", "efg");
      /*
            redis_request->Incr("counter");
            redis_request->IncrBy("counter", 10);
            redis_request->Decr("counter");
            redis_request->DecrBy("counter", 10);

            redis_request->Select("1");
            redis_request->Auth("");

            redis_request->TTL("counter");
            redis_request->Expire("counter", 200);
            redis_request->Persist("counter");
            redis_request->TTL("counter");
      */
      net::RefClient redis_client =
          router.GetNextClient("", redis_request.get());
      LOG(INFO) << "use redis client:" << redis_client->ClientInfo();

      net::RedisResponse* redis_response =
          redis_client->SendRecieve(redis_request);
      if (redis_response) {
        LOG(INFO) << "reponse:\n" << redis_response->DebugDump();
      } else {
        LOG(ERROR) << "redis client request failed:"
                   << redis_request->FailCode();
      }
    }

    loop.QuitLoop();
  };

  sleep(5);

  CO_GO& loop << task;
  loop.WaitLoopEnd();
  return;
}

TEST_CASE("client.ringhash_router", "[redis ringhash router client]") {
  static const int connections = 2;

  LOG(INFO) << "start test client.ringhash_router with redis protocol";

  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  std::vector<std::string> remote_hosts = {
      "redis://127.0.0.1:6400", "redis://127.0.0.1:6401",
      "redis://127.0.0.1:6402", "redis://127.0.0.1:6403",
      "redis://127.0.0.1:6404",
  };

  net::ClientConfig config;

  config.recon_interval = 10;
  config.message_timeout = 5000;
  config.connections = connections;

  net::RingHashRouter router;

  for (auto& remote : remote_hosts) {
    net::url::RemoteInfo server_info;

    bool success = net::url::ParseRemote(remote, server_info);
    LOG_IF(ERROR, !success) << " server:" << remote << " can't be resolve";
    if (!success) {
      LOG(INFO) << "host:" << server_info.host << " ip:" << server_info.host_ip
                << " port:" << server_info.port
                << " protocol:" << server_info.protocol;
      return;
    }

    net::RefClient client(new net::Client(&loop, server_info));
    client->SetDelegate(&router_delegate);
    client->Initialize(config);

    router.AddClient(std::move(client));
  }

  auto task = [&]() {
    for (uint32_t i = 0; i < 1000000; i++) {
      auto redis_request = std::make_shared<net::RedisRequest>();

      std::string key = std::to_string(10000 + i);

      redis_request->Incr("counter");

      net::RefClient redis_client =
          router.GetNextClient(key, redis_request.get());

      CHECK(redis_client);

      net::RedisResponse* redis_response =
          redis_client->SendRecieve(redis_request);
      if (redis_response) {
        // LOG(INFO) << "reponse:\n" << redis_response->DebugDump();
      } else {
        // LOG(ERROR) << "redis client request failed:" <<
        // redis_request->FailCode();
      }
    }

    loop.QuitLoop();
  };

  sleep(2);

  CO_GO& loop << task;
  loop.WaitLoopEnd();
  return;
}

TEST_CASE("client.redis_heartbeat", "[redis client heartbeat]") {
  static const int connections = 1;
  LOG(INFO) << "start test client.redis_heartbeat with redis protocal";

  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  std::vector<std::string> remote_hosts = {
      "redis://127.0.0.1:6379?db=2"
      "redis://127.0.0.1:6379?db=3"};

  net::ClientConfig config;

  config.recon_interval = 1000;
  config.message_timeout = 5000;
  config.heartbeat_ms = 1000;
  config.connections = connections;

  net::RoundRobinRouter router;
  for (auto& remote : remote_hosts) {
    net::url::RemoteInfo server_info;
    bool success = net::url::ParseRemote(remote, server_info);
    LOG_IF(ERROR, !success) << " server:" << remote << " can't be resolve";
    if (!success) {
      LOG(INFO) << "host:" << server_info.host << " ip:" << server_info.host_ip
                << " port:" << server_info.port
                << " protocol:" << server_info.protocol;
      return;
    }

    net::RefClient client(new net::Client(&loop, server_info));
    client->SetDelegate(&router_delegate);
    client->Initialize(config);

    router.AddClient(std::move(client));
  }

  sleep(10);
  CO_GO& loop << [&]() { loop.QuitLoop(); };
  loop.WaitLoopEnd();
  return;
}

class ClientD : public net::ClientDelegate {
public:
  ClientD() {
    for (size_t i = 0; i < 4; i++) {
      auto loop = new base::MessageLoop();
      loop->SetLoopName(base::StrUtil::Concat("io_", i));
      loop->Start();
      loops_.push_back(loop);
    }
  }
  base::MessageLoop* NextIOLoopForClient() {
    return loops_.at((index_++) % loops_.size());
  };
  /* do some init things like select db for redis,
   * or enable keepalive action etc*/
  void OnClientChannelReady(const lt::net::ClientChannel* channel) {
    LOG(INFO) << " a client channel ready";
  }
  /* when a abnormal broken happend, here a chance to notify
   * application level for handle this case, eg:remote server
   * shutdown,crash etc*/
  void OnAllClientPassiveBroken(const lt::net::Client* client) {
    LOG(INFO) << " all client broken";
  };

private:
  std::atomic_int index_;
  std::vector<base::MessageLoop*> loops_;
};

TEST_CASE("client.http_pool", "[http client pool manager]") {
  LOG(INFO) << " start test client.http_pool, http client send request";

  ClientD delegate;
  base::MessageLoop loop;
  loop.SetLoopName("client");
  loop.Start();

  static const int connections = 10000;

  net::url::RemoteInfo server_info;
  LOG_IF(ERROR,
         !net::url::ParseRemote("http://g.test.amnetapi.com", server_info))
      << " server can't be resolve";

  net::Client http_client(&loop, server_info);

  net::ClientConfig config;
  config.recon_interval = 100;
  config.message_timeout = 5000;
  config.connections = connections;

  http_client.SetDelegate(&delegate);
  http_client.Initialize(config);

  sleep(0);

  http_client.Finalize();

  loop.WaitLoopEnd();

  LOG(INFO) << " end test client.http.request, http client send request";
}
