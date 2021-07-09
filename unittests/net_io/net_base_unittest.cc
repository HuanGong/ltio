//
// Created by gh on 18-12-23.
//

#include <base/message_loop/message_loop.h>
#include <base/time/time_utils.h>
#include <stdlib.h>
#include <unistd.h>
#include <atomic>
#include <iostream>
#include "base/closure/closure_task.h"
#include "glog/logging.h"
#include "net_io/clients/client.h"
#include "net_io/clients/client_connector.h"
#include "net_io/codec/codec_factory.h"
#include "net_io/codec/codec_service.h"
#include "net_io/codec/http/http_request.h"
#include "net_io/codec/http/http_response.h"
#include "net_io/codec/line/line_message.h"
#include "net_io/codec/raw/raw_codec_service.h"
#include "net_io/codec/raw/raw_message.h"
#include "net_io/dispatcher/coro_dispatcher.h"
#include "net_io/socket_acceptor.h"
#include "net_io/socket_utils.h"
#include "net_io/tcp_channel.h"
#include "net_io/url_utils.h"

#include <thirdparty/catch/catch.hpp>
#include "net_io/base/ip_address.h"
#include "net_io/base/ip_endpoint.h"
#include "net_io/base/sockaddr_storage.h"
#include "net_io/udp_io/udp_service.h"

using namespace lt;

TEST_CASE("net.base", "[system basic check]") {
  struct sockaddr sock_addr;
  struct sockaddr_in sock_addr_v4;
  struct sockaddr_in6 sock_addr_v6;
  net::SockaddrStorage storage;
  LOG(INFO) << "sizeof(sockaddr):" << sizeof(sock_addr)
            << " sizeof(sockaddr_in):" << sizeof(sockaddr_in)
            << " sizeof(sock_addr_v6):" << sizeof(sock_addr_v6)
            << " sorage.addr_len:" << storage.addr_len;
}

TEST_CASE("url.host_resolve", "[host resolve test]") {
  std::string host_ip;
  lt::net::url::HostResolve("g.test.amnetapi.com", host_ip);
  std::cout << "result:" << host_ip << std::endl;
}

TEST_CASE("uri.parse", "[http uri parse]") {
  {
    lt::net::url::SchemeIpPort result;
    REQUIRE(lt::net::url::ParseURI("http://www.baidu.com:80", result));
    REQUIRE(result.port == 80);
    REQUIRE(result.protocol == "http");
    REQUIRE(result.host == "www.baidu.com");
  }

  {
    net::url::SchemeIpPort result;
    REQUIRE(net::url::ParseURI("://127.0.0.1:", result));
    REQUIRE(result.protocol == "http");
    REQUIRE(result.host == "127.0.0.1");
  }

  {
    net::url::SchemeIpPort result;
    REQUIRE(net::url::ParseURI("://61.135.169.121:5006", result));
    REQUIRE(result.protocol == "http");
    REQUIRE(result.host == "61.135.169.121");
    LOG(INFO) << " result host_ip:" << result.host_ip;
  }
}

TEST_CASE("uri.parse.remote", "[remote uri parse]") {
  std::vector<std::string> remote_uris = {
      "http://gh:passwd@localhost:8020?abc=&name=1234",
      "://gh:passwd@localhost:8020?abc=&name=1234",
      "gh:passwd@localhost:8020?abc=&name=1234",
      "gh:@localhost:8020?abc=&name=1234",
      "gh@localhost:8020?abc=&name=1234",
      "gh@localhost?abc=&name=1234",
      "gh@localhost?",
      "gh@localhost",
      "localhost:80?abc=",
  };

  for (auto remote_uri : remote_uris) {
    net::url::RemoteInfo remote;
    remote.reset();

    net::url::ParseRemote(remote_uri, remote, true);
    LOG(INFO) << ">>>>>>:" << remote_uri;
    LOG(INFO) << "protocol:" << remote.protocol << " user:" << remote.user
              << " psd:" << remote.passwd << " host:" << remote.host
              << " ip:" << remote.host_ip << " port:" << remote.port
              << " query:" << remote.querys.size();
  }
}

TEST_CASE("ip.address", "[ip address test]") {
  LOG(INFO) << "ipv4 localhost:" << net::IPAddress::IPv4Localhost().ToString();
  LOG(INFO) << "ipv6 localhost:" << net::IPAddress::IPv6Localhost().ToString();

  auto bytes = net::IPAddressBytes();
  REQUIRE(bytes.size() == 0);
  REQUIRE(bytes.empty());

  auto address = net::IPAddress();
  LOG(INFO) << "net::IPAddress().IsIPv4():" << address.IsIPv4();
  LOG(INFO) << "net::IPAddress().IsIPv6():" << address.IsIPv6();
  LOG(INFO) << "net::IPAddress().IsZero():" << address.IsZero();

  address.AssignFromIPLiteral("127.0.0.1");
  REQUIRE((address.IsIPv4() && address.IsValid() && address.IsLoopback()));

  REQUIRE((address.ToString() == "127.0.0.1"));

  auto ipv6 = net::ConvertIPv4ToIPv4MappedIPv6(address);
  LOG(INFO) << "127.0.0.1 => IPV6:" << ipv6.ToString();
}

TEST_CASE("ip.endpoint", "[ip endpoint test]") {
  auto ep = net::IPEndPoint();
  LOG(INFO) << " port:" << ep.port();

  auto local_ep = net::IPEndPoint(net::IPAddress::IPv6Localhost(), 8080);
  REQUIRE((local_ep.GetFamily() == net::ADDRESS_FAMILY_IPV6));

  sockaddr_in6 addr;
  uint32_t len = sizeof(sockaddr_in6);
  REQUIRE(local_ep.ToSockAddr((struct sockaddr*)&addr, len));
  REQUIRE(addr.sin6_port == htobe16(8080));

  LOG(INFO) << "ipv6 localhost with port 8080:" << local_ep.ToString();
}

TEST_CASE("udp.pollbuffer", "[udp pollbuffer]") {
  net::UDPPollBuffer buffer(5);
}

TEST_CASE("udp.udpservice", "[udp serivce]") {
  base::MessageLoop main;
  main.Start();

  net::IPEndPoint ep("127.0.0.1", 8888);
  auto service = net::UDPService::Create(&main, ep);
  service->StartService();

  main.WaitLoopEnd();
}
