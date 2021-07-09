#include <atomic>
#include <functional>
#include <memory>

#include "base/coroutine/co_runner.h"
#include "base/coroutine/wait_group.h"
#include "net_io/clients/client.h"
#include "net_io/codec/http/http_request.h"
#include "net_io/codec/http/http_response.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

using namespace lt;
using base::MessageLoop;

typedef std::unique_ptr<net::Client> ClientPtr;
ClientPtr http_client;

DEFINE_string(cert, "", "cert ca file");
DEFINE_bool(insercure, false, "https without ca verify");
DEFINE_string(remote, "", "scheme://host:port remote address");

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::SetUsageMessage(
      "usage: ./lt_client_cli --remote=scheme://host:port[?[supported "
      "queries]]");

  if (FLAGS_remote.empty()) {
    gflags::ShowUsageWithFlags(argv[0]);
    return -1;
  }

  net::ClientConfig config;
  net::url::RemoteInfo server_info;
  if (!net::url::ParseRemote(FLAGS_remote, server_info)) {
    LOG(ERROR) << "address" << FLAGS_remote << " not correct";
    return -2;
  }
  config.connections = 1;
  config.recon_interval = 1000;
  config.message_timeout = 5000;

  base::MessageLoop background;
  background.Start();
  http_client.reset(new net::Client(&background, server_info));
  if (server_info.protocol == "https") {
#ifdef LTIO_HAVE_SSL
    auto client_ssl_ctx = new base::SSLCtx(base::SSLRole::Client);
    if (FLAGS_insercure) {
      LOG(INFO) << "use https without verify server certfication";
      client_ssl_ctx->SetVerifyMode(SSL_VERIFY_NONE);
    } else if (FLAGS_cert.size()) {
      LOG(INFO) << "use ca file:" << FLAGS_cert << " verify certfication";
      client_ssl_ctx->UseVerifyCA(FLAGS_cert, "");
    }
    http_client->SetSSLCtx(client_ssl_ctx);
#else
    LOG(INFO) << "https need compile with openssl, see config for more detail";
    exit(-1);
#endif
  }
  http_client->Initialize(config);

  bool done = false;
  std::string content;
  while (true) {
    std::cout << "\nnext: issue a request, quit: existing>:";
    std::flush(std::cout);
    std::getline(std::cin, content);
    if (content == "quit") {
      http_client->Finalize();
      background.QuitLoop();
      break;
    }
    if (content != "next") {
      continue;
    }
    done = false;
    CO_GO[&]() {
      auto http_request = std::make_shared<lt::net::HttpRequest>();
      http_request->SetMethod("GET");
      http_request->SetRequestURL("/ping");
      http_request->SetKeepAlive(true);

      auto res = http_client->SendRecieve(http_request);
      LOG_IF(INFO, !res) << "nil response";
      LOG_IF(INFO, res) << "response:" << res->Dump();
      done = true;
    };
    while (!done)
      sleep(0);
  };
  background.WaitLoopEnd();
}
