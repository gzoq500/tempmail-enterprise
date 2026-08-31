#pragma once
#include "database.h"
#include <string>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace httplib { class Server; }

class TempMailServer {
public:
    TempMailServer(Database& db, const std::string& domain, int port);
    void start();
    void stop();

private:
    Database& db_;
    std::string domain_;
    int port_;
    std::atomic<bool> stopping_{false};
    std::atomic<httplib::Server*> server_{nullptr};
    std::mutex event_mutex_;
    std::condition_variable email_event_;
    std::uint64_t email_event_version_{0};
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_event_;
    std::thread cleanup_thread_;

    std::string generate_alias();
    void setup_routes(void* svr);
    static std::string generate_uuid();
};
