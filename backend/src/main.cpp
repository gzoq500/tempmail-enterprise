#include "server.h"
#include "database.h"
#include "crypto.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <pthread.h>
#include <sys/stat.h>
#include <thread>

int main(int argc, char* argv[]) {
    // Database/WAL/key files are private service state by default.
    umask(0077);
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

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
        // Load or create the Kyber-protected master key before opening the DB:
        // email fields and API-key hashing all derive from it. The key material
        // lives next to the database (configurable) and never inside the DB.
        const char* env_keys = std::getenv("TEMPMAIL_KEY_DIR");
        std::string key_dir = env_keys ? env_keys : (db_path.substr(0, db_path.find_last_of('/')) + "/keys");
        std::cout << "Initializing crypto (ML-KEM-768 envelope)..." << std::endl;
        std::string master_key;
        if (!tempmail_crypto::load_or_create_master_key(key_dir + "/master", key_dir, master_key)) {
            std::cerr << "Fatal: cannot load or create the Kyber master key envelope in " << key_dir << std::endl;
            return 1;
        }

        std::cout << "Initializing database: " << db_path << std::endl;
        Database db(db_path, master_key);

        std::cout << "Starting server..." << std::endl;
        TempMailServer server(db, domain, port);
        std::thread signal_thread([&]() {
            int received_signal = 0;
            sigwait(&signals, &received_signal);
            server.stop();
        });
        server.start();
        signal_thread.join();

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
