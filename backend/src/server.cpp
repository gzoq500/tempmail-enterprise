#include "server.h"
#include "email_parser.h"
#include "database.h"
#include "httplib.h"
#include "json.hpp"
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

using json = nlohmann::json;

TempMailServer::TempMailServer(Database& db, const std::string& domain, int port)
    : db_(db), domain_(domain), port_(port) {}

std::string TempMailServer::generate_alias() {
    // Indonesian name parts
    static const std::vector<std::string> first_names = {
        "kemal", "rangga", "rendy", "jeremy", "marvel", "adityo", "edy", "dede", "andrilla", "arfan",
        "budi", "andi", "dewi", "siti", "ahmad", "muhammad", "putra", "putri", "rizki", "fajar",
        "agung", "bagas", "dimas", "eko", "fajar", "gilang", "hadi", "iman", "joko", "kurnia",
        "lukman", "maman", "nanda", "opik", "pratama", "rahmat", "sandi", "taufik", "udin", "vicky",
        "wahyu", "yusuf", "zainal", "bayu", "candra", "dian", "erwin", "fauzi", "gunawan", "hendra",
        "ivan", "juli", "kevin", "leo", "mika", "nico", "oscar", "panji", "reza", "sultan",
        "tio", "ucup", "vino", "wawan", "xavier", "yoga", "zacky", "arif", "beni", "cecep"
    };

    static const std::vector<std::string> last_names = {
        "pinkanatalini", "wahyudi", "eda", "julianto", "saraswati", "fariza", "mairessi", "mayasopha", "satrio", "listyani",
        "pratama", "wijaya", "susanto", "halim", "gunawan", "santoso", "widodo", "setiawan", "kusuma", "nugroho",
        "saputra", "ramadhani", "permadi", "hutapea", "siregar", "nasution", "lubis", "harahap", "sinaga", "tarigan",
        "putra", "putri", "lestari", "rahayu", "sari", "dewi", "purnama", "adiputra", "mahendra", "firmansyah",
        "iskandar", "abdullah", "ibrahim", "rahman", "hamzah", "mansur", "bakri", "saleh", "umar", "hasan",
        "prasetyo", "widjaja", "tanuwidjaja", "salim", "teguh", "budiman", "hartono", "riyadi", "purnomo", "sutanto"
    };

    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> first_dist(0, first_names.size() - 1);
    std::uniform_int_distribution<> last_dist(0, last_names.size() - 1);
    std::uniform_int_distribution<> num_dist(100, 999);

    return first_names[first_dist(gen)] + last_names[last_dist(gen)] + std::to_string(num_dist(gen));
}

void TempMailServer::start() {
    httplib::Server svr;

    // CORS
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // POST /api/alias - Generate new alias (random or custom)
    svr.Post("/api/alias", [this](const httplib::Request& req, httplib::Response& res) {
        // Check if custom email provided
        std::string custom_email;
        if (req.has_param("email")) {
            custom_email = req.get_param_value("email");
        } else if (!req.body.empty()) {
            try {
                auto j = json::parse(req.body);
                if (j.contains("email")) {
                    custom_email = j["email"].get<std::string>();
                }
            } catch (...) {}
        }

        // Custom alias
        if (!custom_email.empty()) {
            // Validate format
            if (custom_email.find("@") == std::string::npos || custom_email.find("@") == 0) {
                custom_email = custom_email + "@" + domain_;
            }
            // Check if exists
            if (db_.get_alias(custom_email).has_value()) {
                res.status = 409;
                res.set_content(R"({"error":"Alias already exists"})", "application/json");
                return;
            }
            auto now = std::chrono::system_clock::now() + std::chrono::hours(24);
            auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
            auto a = db_.create_alias(custom_email, oss.str());
            json j = {{"id", a.id}, {"email", a.email}, {"expires_at", a.expires_at}};
            res.set_content(j.dump(), "application/json");
            return;
        }

        // Random alias (original logic)
        for (int i = 0; i < 10; ++i) {
            std::string alias = generate_alias();
            std::string email = alias + "@" + domain_;

            // Check if exists
            if (db_.get_alias(email).has_value()) continue;

            // Calculate expiry (24 hours from now)
            auto now = std::chrono::system_clock::now() + std::chrono::hours(24);
            auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");

            auto a = db_.create_alias(email, oss.str());
            json j = {{"id", a.id}, {"email", a.email}, {"expires_at", a.expires_at}};
            res.set_content(j.dump(), "application/json");
            return;
        }
        res.status = 500;
        res.set_content(R"({"error":"Failed to generate alias"})", "application/json");
    });

    // GET /api/aliases - List active aliases
    svr.Get("/api/aliases", [this](const httplib::Request&, httplib::Response& res) {
        auto aliases = db_.get_active_aliases();
        json arr = json::array();
        for (const auto& a : aliases) {
            arr.push_back({{"id", a.id}, {"email", a.email},
                          {"created_at", a.created_at}, {"expires_at", a.expires_at},
                          {"email_count", a.email_count}});
        }
        json j = {{"aliases", arr}};
        res.set_content(j.dump(), "application/json");
    });

    // GET /api/emails/:email - Get emails for alias
    svr.Get(R"(/api/emails/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string email = req.matches[1];
        auto alias = db_.get_alias(email);
        if (!alias) {
            res.status = 404;
            res.set_content(R"({"error":"Alias not found"})", "application/json");
            return;
        }
        auto after = 0;
        if (req.has_param("after")) after = std::stoi(req.get_param_value("after"));

        auto emails = db_.get_emails(alias->id, after);
        db_.mark_alias_read(alias->id);

        json arr = json::array();
        for (const auto& e : emails) {
            arr.push_back({{"id", e.id}, {"from_address", e.from_address},
                          {"subject", e.subject}, {"body_text", e.body_text},
                          {"body_html", e.body_html}, {"received_at", e.received_at},
                          {"is_read", e.is_read}});
        }
        json j = {{"emails", arr}};
        res.set_content(j.dump(), "application/json");
    });

    // GET /api/email/:id - Get specific email
    svr.Get(R"(/api/email/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        auto email = db_.get_email(id);
        if (!email) {
            res.status = 404;
            res.set_content(R"({"error":"Email not found"})", "application/json");
            return;
        }
        db_.mark_read(id);
        json j = {{"id", email->id}, {"from_address", email->from_address},
                  {"to_address", email->to_address}, {"subject", email->subject},
                  {"body_text", email->body_text}, {"body_html", email->body_html},
                  {"received_at", email->received_at}, {"is_read", email->is_read}};
        res.set_content(j.dump(), "application/json");
    });

    // DELETE /api/alias/:email - Delete alias
    svr.Delete(R"(/api/alias/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string email = req.matches[1];
        if (db_.delete_alias(email)) {
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"Alias not found"})", "application/json");
        }
    });

    // GET /api/check/:email - Poll for new emails
    svr.Get(R"(/api/check/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string email = req.matches[1];
        auto alias = db_.get_alias(email);
        if (!alias) {
            res.status = 404;
            res.set_content(R"({"error":"Alias not found"})", "application/json");
            return;
        }
        int after = 0;
        if (req.has_param("after")) after = std::stoi(req.get_param_value("after"));

        auto emails = db_.get_emails(alias->id, after);
        json arr = json::array();
        for (const auto& e : emails) {
            arr.push_back({{"id", e.id}, {"from_address", e.from_address},
                          {"subject", e.subject}, {"received_at", e.received_at},
                          {"is_read", e.is_read}});
        }
        json j = {{"emails", arr}, {"count", static_cast<int>(emails.size())}};
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/incoming - Receive email from Postfix
    svr.Post("/api/incoming", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string to = j.value("to", "");
            std::string from = j.value("from", "unknown");
            std::string subject = j.value("subject", "(No subject)");
            std::string body = j.value("body", "");
            std::string html = j.value("html", "");

            if (to.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Missing 'to' field"})", "application/json");
                return;
            }

            // Write ALL incoming emails to admin mbox for Roundcube
            std::string mbox = "/var/mail/admin";
            FILE* f = fopen(mbox.c_str(), "a");
            if (f) {
                time_t now = time(nullptr);
                struct tm* t = gmtime(&now);
                char datebuf[64];
                strftime(datebuf, sizeof(datebuf), "%a %b %d %H:%M:%S %Y", t);
                fprintf(f, "From %s %s\n", from.c_str(), datebuf);
                fprintf(f, "From: %s\n", from.c_str());
                fprintf(f, "To: %s\n", to.c_str());
                fprintf(f, "Subject: %s\n", subject.c_str());
                fprintf(f, "Content-Type: text/plain; charset=UTF-8\n");
                fprintf(f, "\n%s\n\n", body.c_str());
                fclose(f);
            }

            auto alias = db_.get_alias(to);
            if (!alias) {
                res.set_content(R"({"success":true,"message":"No matching alias"})", "application/json");
                return;
            }

            // Clean MIME content before storing
            std::string clean_body = clean_mime_body(body);
            std::string clean_html = html.empty() ? clean_body : clean_mime_body(html);
            // Also try to extract HTML specifically if body has MIME structure
            if (clean_body.find("Content-Type:") != std::string::npos) {
                std::string extracted_html = extract_html_body(body);
                if (!extracted_html.empty()) clean_html = extracted_html;
                std::string extracted_text = extract_text_body(body);
                if (!extracted_text.empty()) clean_body = extracted_text;
            }
            // Always decode quoted-printable (=3D, soft breaks)
            clean_html = quoted_printable_decode(clean_html);
            clean_body = quoted_printable_decode(clean_body);
int id = db_.store_email(alias->id, from, to, subject, clean_body, clean_html);
            std::cout << "[INCOMING] " << from << " -> " << to << " (" << subject << ") id=" << id << std::endl;


            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });

    // POST /api/send - Send email via Postfix
    svr.Post("/api/send", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string from_email = j.value("from", "");
            std::string from_name = j.value("name", "");
            std::string to = j.value("to", "");
            std::string subject = j.value("subject", "");
            std::string body = j.value("body", "");

            if (from_email.empty() || to.empty() || body.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Missing required fields: from, to, body"})", "application/json");
                return;
            }

            // Build plain text email only (no HTML to avoid spam filters)
            std::string from_header = from_name.empty() ? from_email : from_name + " <" + from_email + ">";
            
            std::string email_msg = "From: " + from_header + "\r\n"
                                  + "To: " + to + "\r\n"
                                  + "Subject: " + subject + "\r\n"
                                  + "List-Unsubscribe: <mailto:unsubscribe@" + domain_ + ">\r\n"
                                  + "List-Unsubscribe-Post: List-Unsubscribe=One-Click\r\n"
                                  + "Content-Type: text/plain; charset=UTF-8\r\n"
                                  + "\r\n"
                                  + body;

            // Write to temp file and send via sendmail
            std::string tmpfile = "/tmp/tempmail_send_" + std::to_string(time(nullptr)) + ".eml";
            FILE* f = fopen(tmpfile.c_str(), "w");
            if (!f) {
                res.status = 500;
                res.set_content(R"({"error":"Failed to create temp file"})", "application/json");
                return;
            }
            fprintf(f, "%s", email_msg.c_str());
            fclose(f);

            // Send via sendmail
            std::string cmd = "/usr/sbin/sendmail -f '" + from_email + "' '" + to + "' < " + tmpfile;
            int ret = system(cmd.c_str());

            // Cleanup
            remove(tmpfile.c_str());

            if (ret == 0) {
                std::cout << "[SEND] " << from_email << " -> " << to << " (" << subject << ")" << std::endl;
                json resp = {{"success", true}, {"message", "Email sent successfully"}};
                res.set_content(resp.dump(), "application/json");
            } else {
                res.status = 500;
                res.set_content(R"({"error":"Failed to send email"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });

    // Health check
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","server":"tempmail-cpp"})", "application/json");
    });

    // Cleanup thread (every hour)
    std::thread([this]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::hours(1));
            int cleaned = db_.cleanup_expired();
            if (cleaned > 0) {
                std::cout << "[CLEANUP] Removed " << cleaned << " expired aliases" << std::endl;
            }
        }
    }).detach();

    std::cout << "TempMail C++ server starting on port " << port_ << std::endl;
    std::cout << "Domain: " << domain_ << std::endl;

    if (!svr.listen("0.0.0.0", port_)) {
        throw std::runtime_error("Failed to start server on port " + std::to_string(port_));
    }
}
