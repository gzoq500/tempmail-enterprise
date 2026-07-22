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
            expires_at DATETIME NOT NULL
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
}

Alias Database::create_alias(const std::string& email, const std::string& expires_at) {
    Alias a;
    const char* sql = "INSERT INTO aliases (id, email, expires_at) VALUES (lower(hex(randomblob(16))), ?, ?) RETURNING id, email, created_at, expires_at";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, expires_at.c_str(), -1, SQLITE_TRANSIENT);
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

std::vector<Alias> Database::get_active_aliases() {
    std::vector<Alias> aliases;
    const char* sql = R"(
        SELECT a.id, a.email, a.created_at, a.expires_at, COUNT(e.id)
        FROM aliases a LEFT JOIN emails e ON a.id = e.alias_id
        WHERE a.expires_at > datetime('now')
        GROUP BY a.id ORDER BY a.created_at DESC
    )";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
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
    char* err = nullptr;
    std::string sql = "DELETE FROM emails WHERE alias_id IN (SELECT id FROM aliases WHERE email='" + email + "')";
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    sql = "DELETE FROM aliases WHERE email='" + email + "'";
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    return sqlite3_changes(db_) > 0;
}

int Database::cleanup_expired() {
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
    const char* sql = "UPDATE emails SET is_read = 1 WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

bool Database::mark_alias_read(const std::string& alias_id) {
    const char* sql = "UPDATE emails SET is_read = 1 WHERE alias_id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, alias_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}
