#include <csignal>
#include <vector>

#include "base/coroutine/co_runner.h"
#include "base/message_loop/message_loop.h"
#include "base/utils/string/str_utils.h"
#include "gflags/gflags.h"
#include "net_io/clients/client.h"
#include "net_io/clients/client_connector.h"
#include "net_io/codec/redis/redis_request.h"
#include "net_io/dispatcher/coro_dispatcher.h"
#include "net_io/dispatcher/workload_dispatcher.h"
#include "net_io/server/generic_server.h"
#include "net_io/server/http_server/http_server.h"
#include "net_io/server/raw_server/raw_server.h"

#ifdef LTIO_HAVE_SSL
#include "base/crypto/lt_ssl.h"

DEFINE_bool(ssl, false, "use ssl");
base::SSLCtx* server_ssl_ctx = nullptr;
base::SSLCtx* client_ssl_ctx = nullptr;
#endif

using namespace lt::net;
using namespace lt;
using namespace base;

DEFINE_string(key, "cert/server.key", "use ssl server key file");
DEFINE_string(cert, "cert/server.crt", "use ssl server cert file");

DEFINE_string(raw, "0.0.0.0:5005", "host:port used for raw service listen on");
DEFINE_string(http,
              "0.0.0.0:5006",
              "host:port used for http service listen on");
DEFINE_string(redis,
              "0.0.0.0:6379",
              "host:port used for redis client connected");

DEFINE_bool(redis_client, false, "wheather enable redis client");
DEFINE_bool(raw_client, false, "wheather enable self connected http client");
DEFINE_bool(http_client, false, "wheather enable self connected raw client");
DEFINE_int32(loops, 4, "how many loops use for handle message and io");

net::ClientConfig DeafaultClientConfig(int count = 2) {
  net::ClientConfig config;
  config.connections = count;
  config.recon_interval = 500;
  config.message_timeout = 1000;
  config.heartbeat_ms = 500;
  return config;
}

void DumpRedisResponse(RedisResponse* redis_response) {
  for (size_t i = 0; i < redis_response->Count(); i++) {
    auto& value = redis_response->ResultAtIndex(i);
    switch (value.type()) {
      case resp::ty_string: {
        LOG(INFO) << "string:" << value.string().data();
      } break;
      case resp::ty_error: {
        LOG(INFO) << "error:" << value.error().data();
      } break;
      case resp::ty_integer: {
        LOG(INFO) << "interger:" << value.integer();
      } break;
      case resp::ty_null: {
        LOG(INFO) << "null:";
      } break;
      case resp::ty_array: {
        resp::unique_array<resp::unique_value> arr = value.array();
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); i++) {
          oss << "'"
              << std::string(arr[i].bulkstr().data(), arr[i].bulkstr().size())
              << "'";
          if (i < arr.size() - 1) {
            oss << ", ";
          }
        }
        LOG(INFO) << "array: [" << oss.str() << "]";
      } break;
      case resp::ty_bulkstr: {
        LOG(INFO) << "bulkstr:"
                  << std::string(value.bulkstr().data(),
                                 value.bulkstr().size());
      } break;
      default: {
        LOG(INFO) << " default handler for redis message response";
      } break;
    }
  }
}

base::MessageLoop main_loop;

class SimpleApp : public net::ClientDelegate {
public:
  SimpleApp() {}

  ~SimpleApp() {
    LOG(INFO) << __func__ << " close app";
    if (FLAGS_raw_client) {
      delete raw_client;
    }
    if (FLAGS_redis_client) {
      delete redis_client;
    }
    for (auto loop : loops) {
      delete loop;
    }
    loops.clear();
  }

  void Initialize() {
    int loop_count =
        std::min(FLAGS_loops, int(std::thread::hardware_concurrency()));
    for (int i = 0; i < loop_count; i++) {
      auto loop = new base::MessageLoop("io_" + std::to_string(i));
      loop->Start();
      CoroRunner::RegisteRunner(loop);
      loops.push_back(loop);
    }

    raw_server.WithIOLoops(loops)
        .WithAddress(base::StrUtil::Concat("raw://", FLAGS_raw))
        .ServeAddress(std::bind(&SimpleApp::HandleRawRequest,
                                this,
                                std::placeholders::_1));

    std::string http_address = base::StrUtil::Concat("http://", FLAGS_http);
#ifdef LTIO_HAVE_SSL
    if (FLAGS_ssl) {
      LOG(INFO) << "init ssl use cert:" << FLAGS_cert << ", key:" << FLAGS_key;
      base::SSLCtx::Param param(base::SSLRole::Server);
      param.crt_file = FLAGS_cert;
      param.key_file = FLAGS_key;
      server_ssl_ctx = new base::SSLCtx(param);
      http_address = base::StrUtil::Concat("https://", FLAGS_http);

      client_ssl_ctx = new base::SSLCtx(base::SSLRole::Client);
      client_ssl_ctx->UseVerifyCA(FLAGS_cert, "");
      client_ssl_ctx->UseCertification(FLAGS_cert, "");
    }
#endif

    http_server.WithIOLoops(loops).WithAddress(http_address);
#ifdef LTIO_HAVE_SSL
    if (FLAGS_ssl) {
      http_server.WithSSLContext(server_ssl_ctx);
    }
#endif
    http_server.ServeAddress(
        std::bind(&SimpleApp::HandleHttpRequest, this, std::placeholders::_1));
  }

  void HandleRawRequest(RefRawRequestContext context) {
    const LtRawMessage* req = context->GetRequest<LtRawMessage>();
    LOG_EVERY_N(INFO, 10000) << "got request:" << req->Dump();

    auto res = LtRawMessage::CreateResponse(req);
    res->SetContent(req->Content());
    return context->Response(res);
  }

  void HandleHttpRequest(RefHttpRequestCtx context) {
    const HttpRequest* req = context->Request();
    LOG_EVERY_N(INFO, 10000) << "got 1w Http request, body:" << req->Dump();

    RefHttpResponse response = HttpResponse::CreateWithCode(200);
    response->SetKeepAlive(req->IsKeepAlive());

#ifdef ENBALE_RDS_CLIENT
    if (req->RequestUrl() == "/rds") {
      app.SendRedisMessage();
      response->MutableBody() = "proxy a redis request";
      return context->Send(std::move(response));
    }
#endif

    if (FLAGS_raw_client && req->RequestUrl() == "/raw") {
      SendRawRequest();
      response->MutableBody() = "proxy a raw request";
    } else if (req->RequestUrl() == "/http") {
      SendHttpRequest();
      response->MutableBody() = "proxy a http request";
    } else if (req->RequestUrl() == "/ping") {
      response->MutableBody() = "PONG";
    } else {
      static const std::string kresponse(3650, 'c');
      response->MutableBody() = kresponse;
    }

    return context->Response(response);
  }

  base::MessageLoop* NextIOLoopForClient() {
    if (loops.empty()) {
      return NULL;
    }
    io_round_count++;
    return loops[io_round_count % loops.size()];
  }

  void StartHttpClients(std::string url) {
    net::url::RemoteInfo server_info;
    LOG_IF(ERROR, !net::url::ParseRemote(url, server_info))
        << " server can't be resolve";

    http_client = new net::Client(NextIOLoopForClient(), server_info);
    net::ClientConfig config = DeafaultClientConfig(1);
    http_client->SetDelegate(this);
    http_client->Initialize(config);
#ifdef LTIO_HAVE_SSL
    http_client->SetSSLCtx(client_ssl_ctx);
#endif
  }

  void SendHttpRequest() {
    if (http_client == nullptr)
      return;

    auto http_request = std::make_shared<HttpRequest>();
    http_request->SetMethod("GET");
    http_request->SetRequestURL("/ping");
    http_request->SetKeepAlive(true);

    HttpResponse* http_response = http_client->SendRecieve(http_request);
    if (http_response) {
      LOG(INFO) << "http client got response:" << http_response->Body();
    } else {
      LOG(ERROR) << "http client request failed:";
    }
  }

  void StartRedisClient() {
    net::url::RemoteInfo server_info;
    LOG_IF(ERROR, !net::url::ParseRemote("redis://127.0.0.1:6379", server_info))
        << " server can't be resolve";
    redis_client = new net::Client(NextIOLoopForClient(), server_info);
    net::ClientConfig config = DeafaultClientConfig();
    redis_client->SetDelegate(this);
    redis_client->Initialize(config);
  }

  void SendRedisMessage() {
    auto redis_request = std::make_shared<RedisRequest>();

    redis_request->SetWithExpire("name", "huan.gong", 2000);
    redis_request->Exists("name");
    redis_request->Delete("name");

    redis_request->Incr("counter");
    redis_request->IncrBy("counter", 10);

    redis_request->Decr("counter");
    redis_request->DecrBy("counter", 10);

    redis_request->TTL("counter");
    redis_request->Expire("counter", 200);

    redis_request->Persist("counter");
    redis_request->TTL("counter");

    auto redis_response = redis_client->SendRecieve(redis_request);
    if (redis_response) {
      DumpRedisResponse(redis_response);
    } else {
      LOG(ERROR) << "redis client request failed:" << redis_request->FailCode();
    }
  }

  void StartRawClient(std::string server_addr) {
    net::url::RemoteInfo server_info;
    LOG_IF(ERROR, !net::url::ParseRemote(server_addr, server_info))
        << " server can't be resolve";
    raw_client = new net::Client(NextIOLoopForClient(), server_info);
    net::ClientConfig config = DeafaultClientConfig();
    raw_client->SetDelegate(this);
    raw_client->Initialize(config);
  }

  void SendRawRequest() {
    auto raw_request = LtRawMessage::Create(true);
    raw_request->SetMethod(12);
    raw_request->SetContent("ABC");
    LtRawMessage* raw_response = raw_client->SendRecieve(raw_request);
    if (!raw_response) {
      LOG(ERROR) << "raw client request failed:" << raw_request->FailCode();
    }
  }

  void StopAllService() {
    LOG(INFO) << __FUNCTION__ << " stop enter";
    for (auto loop : loops) {
      LOG(INFO) << __FUNCTION__ << " loop:" << loop;
    }

    if (http_client) {
      LOG(INFO) << __FUNCTION__ << " start stop httpclient";
      http_client->Finalize();
      LOG(INFO) << __FUNCTION__ << " http client has stoped";
    }

    if (FLAGS_raw_client) {
      LOG(INFO) << __FUNCTION__ << " start stop rawclient";
      raw_client->Finalize();
      LOG(INFO) << __FUNCTION__ << " raw client has stoped";
    }

    if (FLAGS_redis_client) {
      LOG(INFO) << __FUNCTION__ << " start stop rdsclient";
      redis_client->Finalize();
      LOG(INFO) << __FUNCTION__ << " redis client has stoped";
    }

    CHECK(CO_CANYIELD);

    raw_server.StopServer(CO_RESUMER);

    CO_YIELD;

    http_server.StopServer(CO_RESUMER);

    CO_YIELD;

    LOG(INFO) << __FUNCTION__ << " stop leave";
    main_loop.QuitLoop();
  }

  net::Client* raw_client = NULL;  //(base::MessageLoop*, const IPEndpoint&);
  net::Client* redis_client = NULL;

  net::Client* http_client = NULL;  //(base::MessageLoop*, const IPEndpoint&);
  static std::atomic_int io_round_count;
  std::vector<base::MessageLoop*> loops;

  RawCoroServer raw_server;
  HttpCoroServer http_server;
};
std::atomic_int SimpleApp::io_round_count = {0};

SimpleApp app;

void signalHandler(int signum) {
  LOG(INFO) << "sighandler sig:" << signum;
  CO_GO std::bind(&SimpleApp::StopAllService, &app);
}

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::SetUsageMessage(
      "usage: exec --http=ip:port --raw=ip:port --noredis_client ...");

#ifdef LTIO_WITH_OPENSSL
  // google::InitGoogleLogging(argv[0]);
  // google::SetVLOGLevel(NULL, 26);
  LOG(INFO) << "simple server compiled with openssl support";
#endif

  main_loop.SetLoopName("main");
  main_loop.Start();

  app.Initialize();

  std::string http_url = base::StrUtil::Concat("http://", FLAGS_http);
#ifdef LTIO_WITH_OPENSSL
  if (FLAGS_ssl) {
    http_url = base::StrUtil::Concat("https://", FLAGS_http);
  }
#endif
  if (FLAGS_http_client) {
    app.StartHttpClients(http_url);
  }

  if (FLAGS_raw_client) {
    app.StartRawClient(base::StrUtil::Concat("raw://", FLAGS_raw));
  }

  if (FLAGS_redis_client) {
    app.StartRedisClient();
  }

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  main_loop.WaitLoopEnd();
}
