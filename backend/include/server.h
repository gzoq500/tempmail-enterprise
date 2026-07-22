#pragma once
#include "database.h"
#include <string>

class TempMailServer {
public:
    TempMailServer(Database& db, const std::string& domain, int port);
    void start();

private:
    Database& db_;
    std::string domain_;
    int port_;

    std::string generate_alias();
    void setup_routes(void* svr);
    static std::string generate_uuid();
};
