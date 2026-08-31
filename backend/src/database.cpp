#include "database.h"
#include <sqlite3.h>
#include <stdexcept>
#include <iostream>

Database::Database(const std::string& db_path) : db_(nullptr) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(db_)));
    }
    // Performance optimizations
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA cache_size=-64000;", nullptr, nullptr, nullptr); // 64MB
    sqlite3_exec(db_, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
    init_schema();
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::init_schema() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS aliases (
            id TEXT PRIMARY KEY,
            email TEXT UNIQUE NOT NULL,
            created_at DATETIME DEFAULT (datetime('now')),
            expires_at DATETIME NOT NULL,
            api_key TEXT UNIQUE
        );
        CREATE TABLE IF NOT EXISTS emails (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            alias_id TEXT NOT NULL,
            from_address TEXT NOT NULL,
            to_address TEXT NOT NULL,
            subject TEXT DEFAULT '',
            body_text TEXT DEFAULT '',
            body_html TEXT DEFAULT '',
            received_at DATETIME DEFAULT (datetime('now')),
            is_read INTEGER DEFAULT 0,
            FOREIGN KEY (alias_id) REFERENCES aliases(id)
        );
        CREATE INDEX IF NOT EXISTS idx_aliases_email ON aliases(email);
        CREATE INDEX IF NOT EXISTS idx_aliases_expires ON aliases(expires_at);
        CREATE INDEX IF NOT EXISTS idx_emails_alias ON emails(alias_id);
        CREATE INDEX IF NOT EXISTS idx_emails_received ON emails(received_at);
    )";
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string e = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("Schema init failed: " + e);
    }

    // Backward-compatible migration: existing aliases remain valid and keep
    // api_key NULL. Every newly generated alias receives its own key.
    bool has_api_key = false;
    sqlite3_stmt* info = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(aliases)", -1, &info, nullptr) == SQLITE_OK) {
        while (sqlite3_step(info) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(info, 1);
            if (name && std::string(reinterpret_cast<const char*>(name)) == "api_key") {
                has_api_key = true;
                break;
            }
        }
    }
    sqlite3_finalize(info);
    if (!has_api_key) {
        if (sqlite3_exec(db_, "ALTER TABLE aliases ADD COLUMN api_key TEXT", nullptr, nullptr, &err) != SQLITE_OK) {
            std::string e = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("API key migration failed: " + e);
        }
    }
    sqlite3_exec(db_, "CREATE UNIQUE INDEX IF NOT EXISTS idx_aliases_api_key ON aliases(api_key) WHERE api_key IS NOT NULL", nullptr, nullptr, nullptr);
}

Alias Database::create_alias(const std::string& email, const std::string& expires_at,
                             const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    Alias a;
    const char* sql = "INSERT INTO aliases (id, email, expires_at, api_key) VALUES (lower(hex(randomblob(16))), ?, ?, NULLIF(?, '')) RETURNING id, email, created_at, expires_at";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, expires_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, api_key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        a.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        a.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        a.expires_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        a.email_count = 0;
    }
    sqlite3_finalize(stmt);
    return a;
}

std::optional<Alias> Database::get_alias(const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = R"(
        SELECT a.id, a.email, a.created_at, a.expires_at, COUNT(e.id)
        FROM aliases a LEFT JOIN emails e ON a.id = e.alias_id
        WHERE a.email = ? GROUP BY a.id
    )";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Alias> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Alias a;
        a.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        a.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        a.expires_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        a.email_count = sqlite3_column_int(stmt, 4);
        result = a;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<Alias> Database::get_alias_by_api_key(const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = R"(
        SELECT a.id, a.email, a.created_at, a.expires_at, COUNT(e.id)
        FROM aliases a LEFT JOIN emails e ON a.id = e.alias_id
        WHERE a.api_key = ? AND a.expires_at > datetime('now') GROUP BY a.id
    )";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, api_key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Alias> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Alias a;
        a.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        a.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        a.expires_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        a.email_count = sqlite3_column_int(stmt, 4);
        result = a;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::api_key_owns_alias(const std::string& api_key, const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT 1 FROM aliases WHERE api_key = ? AND email = ? AND expires_at > datetime('now')", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, api_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool Database::api_key_owns_email(const std::string& api_key, int email_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM emails e JOIN aliases a ON a.id = e.alias_id WHERE e.id = ? AND a.api_key = ? AND a.expires_at > datetime('now')";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, email_id);
    sqlite3_bind_text(stmt, 2, api_key.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

std::vector<Alias> Database::get_active_aliases(const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Alias> aliases;
    const char* sql = R"(
        SELECT a.id, a.email, a.created_at, a.expires_at, COUNT(e.id)
        FROM aliases a LEFT JOIN emails e ON a.id = e.alias_id
        WHERE a.expires_at > datetime('now') AND (? = '' OR a.api_key = ?)
        GROUP BY a.id ORDER BY a.created_at DESC
    )";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, api_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, api_key.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Alias a;
        a.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        a.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        a.expires_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        a.email_count = sqlite3_column_int(stmt, 4);
        aliases.push_back(a);
    }
    sqlite3_finalize(stmt);
    return aliases;
}

bool Database::delete_alias(const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* delete_emails = "DELETE FROM emails WHERE alias_id IN (SELECT id FROM aliases WHERE email = ?)";
    if (sqlite3_prepare_v2(db_, delete_emails, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok || sqlite3_prepare_v2(db_, "DELETE FROM aliases WHERE email = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
    int deleted = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    sqlite3_exec(db_, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && deleted > 0;
}

bool Database::delete_alias_by_api_key(const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* delete_emails = "DELETE FROM emails WHERE alias_id IN (SELECT id FROM aliases WHERE api_key = ?)";
    if (sqlite3_prepare_v2(db_, delete_emails, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(stmt, 1, api_key.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok || sqlite3_prepare_v2(db_, "DELETE FROM aliases WHERE api_key = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(stmt, 1, api_key.c_str(), -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
    const int deleted = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    sqlite3_exec(db_, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && deleted > 0;
}

int Database::clear_emails(const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM emails WHERE alias_id IN (SELECT id FROM aliases WHERE email = ?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    int deleted = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return ok ? deleted : -1;
}

int Database::cleanup_expired() {
    std::lock_guard<std::mutex> lock(mutex_);
    char* err = nullptr;
    sqlite3_exec(db_, "DELETE FROM emails WHERE alias_id NOT IN (SELECT id FROM aliases)", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    sqlite3_exec(db_, "DELETE FROM aliases WHERE expires_at < datetime('now')", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    return sqlite3_changes(db_);
}

int Database::store_email(const std::string& alias_id, const std::string& from,
                          const std::string& to, const std::string& subject,
                          const std::string& body_text, const std::string& body_html) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "INSERT INTO emails (alias_id, from_address, to_address, subject, body_text, body_html) VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, alias_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, body_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, body_html.c_str(), -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (result == SQLITE_DONE) ? static_cast<int>(sqlite3_last_insert_rowid(db_)) : -1;
}

std::vector<Email> Database::get_emails(const std::string& alias_id, int after_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Email> emails;
    const char* sql = "SELECT id, alias_id, from_address, to_address, subject, body_text, body_html, received_at, is_read FROM emails WHERE alias_id = ? AND id > ? ORDER BY received_at DESC";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, alias_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, after_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Email e;
        e.id = sqlite3_column_int(stmt, 0);
        e.alias_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.from_address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.to_address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        e.subject = sqlite3_column_text(stmt, 4) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)) : "";
        e.body_text = sqlite3_column_text(stmt, 5) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) : "";
        e.body_html = sqlite3_column_text(stmt, 6) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) : "";
        e.received_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        e.is_read = sqlite3_column_int(stmt, 8) != 0;
        emails.push_back(e);
    }
    sqlite3_finalize(stmt);
    return emails;
}

std::optional<Email> Database::get_email(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "SELECT id, alias_id, from_address, to_address, subject, body_text, body_html, received_at, is_read FROM emails WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    std::optional<Email> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Email e;
        e.id = sqlite3_column_int(stmt, 0);
        e.alias_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.from_address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.to_address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        e.subject = sqlite3_column_text(stmt, 4) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)) : "";
        e.body_text = sqlite3_column_text(stmt, 5) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) : "";
        e.body_html = sqlite3_column_text(stmt, 6) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) : "";
        e.received_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        e.is_read = sqlite3_column_int(stmt, 8) != 0;
        result = e;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::mark_read(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "UPDATE emails SET is_read = 1 WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

bool Database::mark_alias_read(const std::string& alias_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "UPDATE emails SET is_read = 1 WHERE alias_id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, alias_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}
