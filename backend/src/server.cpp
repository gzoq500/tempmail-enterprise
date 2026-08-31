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
#include <regex>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using json = nlohmann::json;

namespace {
bool parse_bounded_int(const httplib::Request& req, const char* name, int default_value,
                       int minimum, int maximum, int& output) {
    output = default_value;
    if (!req.has_param(name)) return true;
    try {
        const std::string value = req.get_param_value(name);
        size_t consumed = 0;
        long long parsed = std::stoll(value, &consumed, 10);
        if (consumed != value.size() || parsed < minimum || parsed > maximum) return false;
        output = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool is_valid_email(const std::string& email) {
    if (email.empty() || email.size() > 254 || email.find('\r') != std::string::npos ||
        email.find('\n') != std::string::npos || email.find(' ') != std::string::npos) return false;
    const size_t at = email.find('@');
    if (at == 0 || at == std::string::npos || at != email.rfind('@') || at + 3 > email.size()) return false;
    if (email.find('.', at + 2) == std::string::npos) return false;
    const auto valid_local = [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
    };
    const auto valid_domain = [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-';
    };
    return std::all_of(email.begin(), email.begin() + static_cast<std::ptrdiff_t>(at), valid_local) &&
           std::all_of(email.begin() + static_cast<std::ptrdiff_t>(at + 1), email.end(), valid_domain);
}

bool write_all(int fd, const std::string& data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t result = write(fd, data.data() + written, data.size() - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        written += static_cast<size_t>(result);
    }
    return true;
}

int send_via_sendmail(const std::string& from, const std::string& to, const std::string& message) {
    int input_pipe[2];
    if (pipe(input_pipe) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(input_pipe[0]); close(input_pipe[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        close(input_pipe[0]); close(input_pipe[1]);
        const char* argv[] = {"sendmail", "-f", from.c_str(), "--", to.c_str(), nullptr};
        execv("/usr/sbin/sendmail", const_cast<char* const*>(argv));
        _exit(127);
    }
    close(input_pipe[0]);
    bool wrote = write_all(input_pipe[1], message);
    close(input_pipe[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return wrote && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
}

TempMailServer::TempMailServer(Database& db, const std::string& domain, int port)
    : db_(db), domain_(domain), port_(port) {}

void TempMailServer::stop() {
    stopping_.store(true);
    email_event_.notify_all();
    shutdown_event_.notify_all();
    if (auto* server = server_.load()) server->stop();
}

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
    stopping_.store(false);
    server_.store(&svr);
    svr.set_payload_max_length(10 * 1024 * 1024);
    svr.set_read_timeout(15, 0);
    svr.set_write_timeout(15, 0);
    svr.set_keep_alive_timeout(10);
    svr.set_keep_alive_max_count(100);
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        try { if (ep) std::rethrow_exception(ep); }
        catch (const std::exception& e) { std::cerr << "[REQUEST ERROR] " << e.what() << std::endl; }
        res.status = 400;
        res.set_content(R"({"error":"Invalid request"})", "application/json");
    });

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

    // POST /api/alias - Generate new alias (random or custom, with duration)
    svr.Post("/api/alias", [this](const httplib::Request& req, httplib::Response& res) {
        // Parse request body
        std::string custom_email;
        std::string duration = "24h"; // default
        if (req.has_param("email")) custom_email = req.get_param_value("email");
        if (req.has_param("duration")) duration = req.get_param_value("duration");
        if (!req.body.empty()) {
            try {
                auto j = json::parse(req.body);
                if (j.contains("email")) custom_email = j["email"].get<std::string>();
                if (j.contains("duration")) duration = j["duration"].get<std::string>();
            } catch (...) {}
        }

        // Calculate expiry from duration
        std::string expires_at;
        if (duration == "forever") {
            expires_at = "2099-12-31T23:59:59Z";
        } else {
            long long hours = 24; // default 24h
            if (duration == "1h") hours = 1;
            else if (duration == "24h") hours = 24;
            else if (duration == "7d") hours = 24 * 7;
            else if (duration == "30d") hours = 24 * 30;
            else { try { hours = std::stoll(duration); } catch (...) {} }
            auto now = std::chrono::system_clock::now() + std::chrono::hours(hours);
            auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
            expires_at = oss.str();
        }

        // Custom alias
        if (!custom_email.empty()) {
            if (custom_email.find("@") == std::string::npos) {
                custom_email = custom_email + "@" + domain_;
            }
            const std::string required_suffix = "@" + domain_;
            if (!is_valid_email(custom_email) || custom_email.size() <= required_suffix.size() ||
                custom_email.compare(custom_email.size() - required_suffix.size(), required_suffix.size(), required_suffix) != 0) {
                res.status = 400;
                res.set_content(R"({"error":"Alias must use the configured domain"})", "application/json");
                return;
            }
            if (db_.get_alias(custom_email).has_value()) {
                res.status = 409;
                res.set_content(R"({"error":"Alias already exists"})", "application/json");
                return;
            }
            auto a = db_.create_alias(custom_email, expires_at);
            json j = {{"id", a.id}, {"email", a.email}, {"expires_at", a.expires_at}};
            res.set_content(j.dump(), "application/json");
            return;
        }

        // Random alias
        for (int i = 0; i < 10; ++i) {
            std::string alias = generate_alias();
            std::string email = alias + "@" + domain_;
            if (db_.get_alias(email).has_value()) continue;
            auto a = db_.create_alias(email, expires_at);
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
        int after = 0;
        if (!parse_bounded_int(req, "after", 0, 0, INT_MAX, after)) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid 'after' parameter"})", "application/json");
            return;
        }

        auto emails = db_.get_emails(alias->id, after);
        std::cout << "[EMAILS] " << email << " -> " << emails.size() << " emails found" << std::endl;
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

    // DELETE /api/emails/:email - Clear all emails for alias (keep alias)
    svr.Delete(R"(/api/emails/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string email = req.matches[1];
        auto alias = db_.get_alias(email);
        if (!alias) {
            res.status = 404;
            res.set_content(R"({"error":"Alias not found"})", "application/json");
            return;
        }
        int deleted = db_.clear_emails(email);
        json j = {{"success", true}, {"deleted", deleted}};
        res.set_content(j.dump(), "application/json");
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
        if (!parse_bounded_int(req, "after", 0, 0, INT_MAX, after)) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid 'after' parameter"})", "application/json");
            return;
        }

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
            std::string from = decode_mime_header(j.value("from", "unknown"));
            std::string subject = decode_mime_header(j.value("subject", "(No subject)"));
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

            // ── STORE RAW BODY FIRST (never lose data) ──
            std::string raw_stored = body;
            
            // ── Extract MIME parts ──
            std::string html_part = extract_html_body(body);
            std::string text_part = extract_text_body(body);
            
            // ── body_html: best HTML we can find ──
            std::string clean_html;
            if (!html_part.empty()) {
                clean_html = html_part;
            } else if (body.find("<html") != std::string::npos || body.find("<body") != std::string::npos || body.find("<!DOCTYPE") != std::string::npos) {
                clean_html = body; // Raw body IS html
            } else if (!html.empty() && html.find("<") != std::string::npos) {
                clean_html = html;
            }
            clean_html = quoted_printable_decode(clean_html);
            
            // ── body_text: best plain text we can find ──
            std::string clean_body;
            if (!text_part.empty()) {
                clean_body = strip_html_tags(text_part);
            }
            if (clean_body.length() < 20 && !clean_html.empty()) {
                std::string stripped = strip_html_tags(clean_html);
                if (stripped.length() > clean_body.length()) clean_body = stripped;
            }
            if (clean_body.empty() && !body.empty()) {
                clean_body = strip_html_tags(body);
            }
            clean_body = quoted_printable_decode(clean_body);
            
            // ── Final: if html is empty but body has HTML, use raw body ──
            if (clean_html.empty() && clean_body.find("<") != std::string::npos) {
                clean_html = clean_body;
            }
            // ── NEVER store empty html when we have raw body ──
            if (clean_html.empty() && !raw_stored.empty() && raw_stored.find("<") != std::string::npos) {
                clean_html = raw_stored;
            }
            
            // ── Sanitize UTF-8 (remove invalid bytes) ──
            auto sanitize_utf8 = [](std::string& s) {
                std::string clean;
                clean.reserve(s.size());
                for (size_t i = 0; i < s.size(); i++) {
                    unsigned char c = s[i];
                    if (c < 0x80) { clean += c; }
                    else if (c >= 0xC0 && c <= 0xDF && i + 1 < s.size() && (s[i+1] & 0xC0) == 0x80) { clean += s[i]; clean += s[i+1]; i++; }
                    else if (c >= 0xE0 && c <= 0xEF && i + 2 < s.size() && (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80) { clean += s[i]; clean += s[i+1]; clean += s[i+2]; i += 2; }
                    else if (c >= 0xF0 && c <= 0xF7 && i + 3 < s.size() && (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80 && (s[i+3] & 0xC0) == 0x80) { clean += s[i]; clean += s[i+1]; clean += s[i+2]; clean += s[i+3]; i += 3; }
                    else { clean += '?'; } // Replace invalid byte
                }
                s = clean;
            };
            sanitize_utf8(clean_html);
            sanitize_utf8(clean_body);

            // ── Strip trailing MIME boundary delimiters ──
            // Boundary-based extraction can leave the closing delimiter
            // ("--boundary--") glued to the last MIME part. That delimiter is
            // transport framing, never email content, so drop every trailing
            // line that looks like a MIME boundary marker.
            auto strip_mime_boundaries = [](std::string& s) {
                while (true) {
                    size_t line_begin = s.find_last_of('\n');
                    line_begin = (line_begin == std::string::npos) ? 0 : line_begin + 1;
                    std::string trimmed = s.substr(line_begin);
                    while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n' ||
                                                trimmed.back() == ' ' || trimmed.back() == '\t')) {
                        trimmed.pop_back();
                    }
                    // MIME boundary lines start with "--" and carry a boundary token.
                    const bool looks_like_boundary = trimmed.rfind("--", 0) == 0 && trimmed.size() >= 4;
                    if (!looks_like_boundary) break;
                    s.resize(line_begin == 0 ? 0 : line_begin - 1);
                    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
                }
            };
            strip_mime_boundaries(clean_html);
            strip_mime_boundaries(clean_body);

            int id = db_.store_email(alias->id, from, to, subject, clean_body, clean_html);
            if (id < 0) {
                res.status = 500;
                res.set_content(R"({"error":"Failed to store email"})", "application/json");
                return;
            }
            {
                std::lock_guard<std::mutex> lock(event_mutex_);
                ++email_event_version_;
            }
            email_event_.notify_all();
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
            if (!is_valid_email(from_email) || !is_valid_email(to) ||
                from_name.find('\r') != std::string::npos || from_name.find('\n') != std::string::npos ||
                subject.find('\r') != std::string::npos || subject.find('\n') != std::string::npos) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid email fields"})", "application/json");
                return;
            }
            if (!db_.get_alias(from_email).has_value()) {
                res.status = 403;
                res.set_content(R"({"error":"Sender must be an active alias"})", "application/json");
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

            // Send directly to sendmail stdin without invoking a shell or temp file.
            int ret = send_via_sendmail(from_email, to, email_msg);

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

    // ── AUTOMATION API ──

    // GET /api/wait/{email}?after=0&timeout=30 - Wait for new email (long polling)
    svr.Get(R"(/api/wait/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string email = req.matches[1];
        auto alias = db_.get_alias(email);
        if (!alias) {
            res.status = 404;
            res.set_content(R"({"error":"Alias not found"})", "application/json");
            return;
        }
        int after = 0;
        int timeout = 30;
        if (!parse_bounded_int(req, "after", 0, 0, INT_MAX, after) ||
            !parse_bounded_int(req, "timeout", 30, 1, 120, timeout)) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid wait parameters"})", "application/json");
            return;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
        std::unique_lock<std::mutex> event_lock(event_mutex_);
        std::uint64_t observed_version = email_event_version_;
        while (true) {
            event_lock.unlock();
            auto emails = db_.get_emails(alias->id, after);
            if (!emails.empty()) {
                const auto& e = emails[0];
                json j = {
                    {"id", e.id}, {"from_address", e.from_address},
                    {"subject", e.subject}, {"body_text", e.body_text},
                    {"body_html", e.body_html}, {"received_at", e.received_at},
                    {"is_read", e.is_read}
                };
                res.set_content(j.dump(), "application/json");
                return;
            }
            event_lock.lock();
            if (std::chrono::steady_clock::now() >= deadline || stopping_.load()) {
                res.status = 408;
                res.set_content(R"({"error":"Timeout"})", "application/json");
                return;
            }
            email_event_.wait_until(event_lock, deadline, [&]() {
                return stopping_.load() || email_event_version_ != observed_version;
            });
            observed_version = email_event_version_;
        }
    });

    // GET /api/extract/{email_id} - Extract token/code/link from email
    svr.Get(R"(/api/extract/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        int email_id = std::stoi(req.matches[1]);
        auto email = db_.get_email(email_id);
        if (!email) {
            res.status = 404;
            res.set_content(R"({"error":"Email not found"})", "application/json");
            return;
        }

        std::string content = email->body_text.empty() ? email->body_html : email->body_text;
        json result = {{"id", email->id}, {"subject", email->subject}, {"from", email->from_address}};

        // Extract verification codes (4-8 digit numbers after keywords)
        std::regex code_re("(?:code|otp|pin|verification|verify|confirm)[:\\s]*([0-9]{4,8})", std::regex::icase);
        std::smatch m;
        if (std::regex_search(content, m, code_re)) result["code"] = m[1].str();

        // Extract standalone 6-digit codes
        if (!result.contains("code")) {
            std::regex digit_re("\\b([0-9]{6})\\b");
            if (std::regex_search(content, m, digit_re)) result["code"] = m[1].str();
        }

        // Extract magic links / verification URLs
        std::regex link_re("(https?://[^\\s<>\"']+(?:verify|confirm|activate|validate|auth|login|token|magic)[^\\s<>\"']*)", std::regex::icase);
        if (std::regex_search(content, m, link_re)) result["link"] = m[1].str();

        // Extract any long URL if no magic link found
        if (!result.contains("link")) {
            std::regex url_re("(https?://[^\\s<>\"']{20,})");
            if (std::regex_search(content, m, url_re)) result["link"] = m[1].str();
        }

        // Extract token strings
        std::regex token_re("(?:token|key|hash|secret)[:\\s]*([a-zA-Z0-9_-]{20,})", std::regex::icase);
        if (std::regex_search(content, m, token_re)) result["token"] = m[1].str();

        res.set_content(result.dump(), "application/json");
    });

    // Health check
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","server":"tempmail-cpp"})", "application/json");
    });

    // Interruptible cleanup thread (one wakeup per hour, no detached lifetime).
    cleanup_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(shutdown_mutex_);
        while (!stopping_.load()) {
            if (shutdown_event_.wait_for(lock, std::chrono::hours(1), [&]() { return stopping_.load(); })) break;
            lock.unlock();
            int cleaned = db_.cleanup_expired();
            if (cleaned > 0) {
                std::cout << "[CLEANUP] Removed " << cleaned << " expired aliases" << std::endl;
            }
            lock.lock();
        }
    });

    std::cout << "TempMail C++ server starting on port " << port_ << std::endl;
    std::cout << "Domain: " << domain_ << std::endl;

    bool listened = svr.listen("127.0.0.1", port_);
    stopping_.store(true);
    shutdown_event_.notify_all();
    email_event_.notify_all();
    if (cleanup_thread_.joinable()) cleanup_thread_.join();
    server_.store(nullptr);
    if (!listened) {
        throw std::runtime_error("Failed to start server on port " + std::to_string(port_));
    }
}
