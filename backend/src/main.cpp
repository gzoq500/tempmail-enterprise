#include "server.h"
#include "database.h"
#include <iostream>
#include <csignal>
#include <cstdlib>

static volatile bool running = true;

void signal_handler(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Configuration from env or defaults
    const char* env_port = std::getenv("TEMPMAIL_PORT");
    const char* env_domain = std::getenv("TEMPMAIL_DOMAIN");
    const char* env_db = std::getenv("TEMPMAIL_DB");

    int port = env_port ? std::atoi(env_port) : 3001;
    std::string domain = env_domain ? env_domain : "routerssh.web.id";
    std::string db_path = env_db ? env_db : "/opt/tempmail/backend/data/tempmail.db";

    // Override from command line
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--domain" && i + 1 < argc) domain = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_path = argv[++i];
        else if (arg == "--help") {
            std::cout << "Usage: tempmail-server [options]\n"
                      << "  --port PORT      Server port (default: 3001)\n"
                      << "  --domain DOMAIN  Email domain (default: routerssh.web.id)\n"
                      << "  --db PATH        Database path (default: /opt/tempmail/backend/data/tempmail.db)\n";
            return 0;
        }
    }

    try {
        std::cout << "Initializing database: " << db_path << std::endl;
        Database db(db_path);

        std::cout << "Starting server..." << std::endl;
        TempMailServer server(db, domain, port);
        server.start();

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
