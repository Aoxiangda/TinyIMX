// Identical byte-echo contract for TinyIMX and Muduo; no protocol/DB/auth claims.
#ifdef USE_MUDUO
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/base/Logging.h>
#else
#include "common/net/EventLoop.h"
#include "common/net/TcpServer.h"
#include "common/logging/Logger.h"
#endif
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const int port = std::stoi(argv[1]), threads = std::stoi(argv[2]);
    if (port < 20000 || port > 60000 || threads < 1 || threads > 8) return 2;
#ifdef USE_MUDUO
    muduo::Logger::setLogLevel(muduo::Logger::ERROR);
    muduo::net::EventLoop loop;
    muduo::net::TcpServer server(&loop, muduo::net::InetAddress(argv[3], port), "peer-echo");
    server.setThreadNum(threads);
    server.setConnectionCallback([](const muduo::net::TcpConnectionPtr& c) {
        if (c->connected()) c->setTcpNoDelay(true);
    });
    server.setMessageCallback([](const muduo::net::TcpConnectionPtr& c,
                                 muduo::net::Buffer* b, muduo::Timestamp) {
        const std::string data = b->retrieveAllAsString();
        c->send(data);
    });
    server.start();
    std::cout << "READY muduo " << port << std::endl;
    loop.loop();
#else
    if (!tinyimx::Logger::Instance().Init("error", "", true)) return 3;
    tinyimx::EventLoop loop;
    tinyimx::TcpServer server(&loop, tinyimx::InetAddress(argv[3], port),
                             "peer-echo", threads, 4096);
    server.SetMessageCallback([](const tinyimx::TcpConnectionPtr& c, tinyimx::Buffer* b) {
        const std::string data = b->RetrieveAllAsString();
        c->Send(data);
    });
    if (!server.Start()) return 3;
    std::cout << "READY tinyimx " << port << std::endl;
    loop.Loop();
#endif
}
