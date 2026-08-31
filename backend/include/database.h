#pragma once
#include <string>
#include <vector>
#include <optional>
#include <ctime>
#include <mutex>

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
    int crypto_version = 0;
};

class Database {
public:
    // master_key enables at-rest encryption (AES-256-GCM) for email fields
    // and salted HMAC storage for API keys. Empty key disables encryption
    // (legacy/test mode).
    Database(const std::string& db_path, const std::string& master_key = "");
    ~Database();

    Alias create_alias(const std::string& email, const std::string& expires_at,
                       const std::string& api_key = "");
    std::optional<Alias> get_alias(const std::string& email);
    std::optional<Alias> get_alias_by_api_key(const std::string& api_key);
    bool api_key_owns_alias(const std::string& api_key, const std::string& email);
    bool api_key_owns_email(const std::string& api_key, int email_id);
    std::vector<Alias> get_active_aliases(const std::string& api_key = "");
    bool delete_alias(const std::string& email);
    bool delete_alias_by_api_key(const std::string& api_key);
    int clear_emails(const std::string& email);  // Delete all emails for alias
    int cleanup_expired();

    int store_email(const std::string& alias_id, const std::string& from,
                    const std::string& to, const std::string& subject,
                    const std::string& body_text, const std::string& body_html = "");
    // include_bodies=false skips body decryption for fast inbox listings.
    std::vector<Email> get_emails(const std::string& alias_id, int after_id = 0,
                                  bool include_bodies = true);
    std::optional<Email> get_email(int id);
    bool mark_read(int id);
    bool mark_alias_read(const std::string& alias_id);

private:
    struct sqlite3* db_;
    std::string master_key_;
    mutable std::mutex mutex_;
    void init_schema();
};
