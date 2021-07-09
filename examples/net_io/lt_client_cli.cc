#include <atomic>
#include <functional>
#include <memory>

#include "base/coroutine/co_runner.h"
#include "fw_rapid_message.h"
#include "net_io/clients/client.h"
#include "net_io/codec/raw/raw_codec_service.h"
#include "net_io/codec/raw/raw_message.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <thirdparty/cameron_queue/blockingconcurrentqueue.h>

using namespace lt;
typedef moodycamel::BlockingConcurrentQueue<std::string> BlockMessageQueue;
using base::MessageLoop;

typedef std::unique_ptr<net::Client> ClientPtr;
ClientPtr raw_router;

BlockMessageQueue message_queue;

void DoLtRawRequest(const std::string& content) {
  auto raw_request = net::LtRawMessage::Create(true);
  raw_request->SetMethod(12);
  raw_request->SetContent(content);
  net::LtRawMessage* response = raw_router->SendRecieve(raw_request);
  if (response) {
    std::cout << "res:" << response->Dump() << std::endl;
    message_queue.enqueue(response->Content());
  } else {
    std::cout << "err:" << raw_request->FailCode() << std::endl;
  }
}

void DoFwRapidRequest(std::string content) {
  auto raw_request = FwRapidMessage::Create(true);
  raw_request->SetCmdId(100);
  raw_request->SetContent(content);
  FwRapidMessage* response = raw_router->SendRecieve(raw_request);
  if (response) {
    std::cout << "res:" << response->Content() << std::endl;
    message_queue.enqueue(response->Content());
  } else {
    std::cout << "err:" << raw_request->FailCode() << std::endl;
  }
}

DEFINE_string(remote, "", "scheme://host:port remote address");

void CodecInitialize() {
  net::CodecFactory::RegisterCreator(
      "rapid",
      [](base::MessageLoop* loop) -> net::RefCodecService {
        auto service =
            std::make_shared<net::RawCodecService<FwRapidMessage>>(loop);
        return std::static_pointer_cast<net::CodecService>(service);
      });
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::SetUsageMessage(
      "usage: ./lt_client_cli --remote=scheme://host:port[?[supported "
      "queries]]");

  if (FLAGS_remote.empty()) {
    gflags::ShowUsageWithFlags(argv[0]);
    return -1;
  }

  CodecInitialize();

  base::MessageLoop mainloop;

  net::ClientConfig config;
  net::url::RemoteInfo server_info;

  if (!net::url::ParseRemote(FLAGS_remote, server_info)) {
    LOG(ERROR) << " address" << FLAGS_remote << " not correct";
    return -2;
  }

  mainloop.SetLoopName("main");
  mainloop.Start();

  raw_router.reset(new net::Client(&mainloop, server_info));

  config.connections = 200;
  config.recon_interval = 1000;
  config.message_timeout = 5000;
  raw_router->Initialize(config);

  while (true) {
    std::string content;
    std::cout << "send:";
    std::flush(std::cout);
    std::getline(std::cin, content);
    if (content == "quit") {
      raw_router->Finalize();
      mainloop.QuitLoop();
      break;
    }

    if (server_info.protocol == "raw") {
      CO_GO& mainloop << std::bind(&DoLtRawRequest, content);
    } else {
      CO_GO& mainloop << std::bind(&DoFwRapidRequest, content);
    }

    std::string response;
    if (!message_queue.wait_dequeue_timed(response, 1000 * 1000)) {
      LOG(INFO) << "no response got, timeout/failed";
    } else {
      LOG(INFO) << "<<" << response;
    }
  };
  mainloop.WaitLoopEnd();
}
