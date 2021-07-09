#include <vector>
#include <csignal>
#include "base/coroutine/co_runner.h"
#include "base/message_loop/message_loop.h"
#include "net_io/clients/client.h"
#include "net_io/clients/client_connector.h"
#include "net_io/server/raw_server/raw_server.h"
#include "net_io/codec/raw/raw_codec_service.h"

#include "fw_rapid_message.h"

using namespace lt::net;
using namespace lt;
using namespace base;

#define USE_CORO_DISPATCH 1

MessageLoop main_loop;

class SimpleRapidApp: public ClientDelegate {
public:
  SimpleRapidApp() {

    int loop_count = std::min(4, int(std::thread::hardware_concurrency()));
    for (int i = 0; i < loop_count; i++) {
      auto loop = new(MessageLoop);
      loop->SetLoopName("io_" + std::to_string(i));
      loop->Start();
      loops.push_back(loop);
    }

    CodecFactory::RegisterCreator(
      "rapid", [](MessageLoop* loop) -> RefCodecService {
        auto service = std::make_shared<RawCodecService<FwRapidMessage>>(loop);
        return std::static_pointer_cast<CodecService>(service);
      });

    raw_server
      .WithIOLoops(loops)
      .WithAddress("rapid://0.0.0.0:5004")
      .ServeAddress([](const RefRawRequestContext& context) {

        const FwRapidMessage* req = context->GetRequest<FwRapidMessage>();
        LOG_EVERY_N(INFO, 10000) << " got request:" << req->Dump();

        auto res = FwRapidMessage::CreateResponse(req);

        res->SetContent(req->Content());

        return context->Response(res);
      });
  }

  ~SimpleRapidApp() {
    for (auto loop : loops) {
      delete loop;
    }
    loops.clear();
  }

  // delegate interface
  MessageLoop* NextIOLoopForClient() override {
    if (loops.empty()) {
      return NULL;
    }
    io_round_count++;
    return loops[io_round_count % loops.size()];
  }

  void StopAllService() {
    CHECK(CO_CANYIELD);

    raw_server.StopServer(CO_RESUMER);

    CO_YIELD;

    main_loop.QuitLoop();
  }

  RawServer raw_server;

  std::vector<MessageLoop*> loops;
  static std::atomic_int io_round_count;
};

std::atomic_int SimpleRapidApp::io_round_count = {0};

SimpleRapidApp app;

void signalHandler( int signum ){
  LOG(INFO) << "sighandler sig:" << signum;
  CO_GO &main_loop << std::bind(&SimpleRapidApp::StopAllService, &app);
}


int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  main_loop.SetLoopName("main");
  main_loop.Start();

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  main_loop.WaitLoopEnd();
}

