#pragma once
#include <string>
#include <vector>
#include <optional>
#include <ctime>

struct Alias {
    std::string id;
    std::string email;
    std::string created_at;
    std::string expires_at;
    int email_count;
};

struct Email {
    int id;
    std::string alias_id;
    std::string from_address;
    std::string to_address;
    std::string subject;
    std::string body_text;
    std::string body_html;
    std::string received_at;
    bool is_read;
};

class Database {
public:
    Database(const std::string& db_path);
    ~Database();

    Alias create_alias(const std::string& email, const std::string& expires_at);
    std::optional<Alias> get_alias(const std::string& email);
    std::vector<Alias> get_active_aliases();
    bool delete_alias(const std::string& email);
    int cleanup_expired();

    int store_email(const std::string& alias_id, const std::string& from,
                    const std::string& to, const std::string& subject,
                    const std::string& body_text, const std::string& body_html = "");
    std::vector<Email> get_emails(const std::string& alias_id, int after_id = 0);
    std::optional<Email> get_email(int id);
    bool mark_read(int id);
    bool mark_alias_read(const std::string& alias_id);

private:
    struct sqlite3* db_;
    void init_schema();
};
