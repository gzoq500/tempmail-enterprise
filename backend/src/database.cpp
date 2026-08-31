#include "database.h"
#include "crypto.h"
#include <sqlite3.h>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <iostream>

Database::Database(const std::string& db_path, const std::string& master_key)
    : db_(nullptr), master_key_(master_key) {
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
    // Fast deterministic lookup index for hashed API keys. The lookup value is
    // HMAC(master, key) — an attacker with the DB alone cannot reverse it
    // without the Kyber-protected master key, and it lets auth use an indexed
    // O(1) seek instead of scanning every alias.
    sqlite3_exec(db_, "ALTER TABLE aliases ADD COLUMN key_lookup BLOB", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "CREATE INDEX IF NOT EXISTS idx_aliases_key_lookup ON aliases(key_lookup)", nullptr, nullptr, nullptr);
    // Version encrypted rows so authentication failure can never be mistaken
    // for a legacy plaintext row. Existing rows stay version 0 and retain the
    // historical row-key/per-field/plaintext fallback chain.
    sqlite3_exec(db_, "ALTER TABLE emails ADD COLUMN crypto_version INTEGER DEFAULT 0", nullptr, nullptr, nullptr);

    // Backfill the version only when existing bytes authenticate with either
    // historical encryption scheme. Genuine plaintext legacy rows remain 0.
    std::vector<std::pair<int, int>> encrypted_ids;
    sqlite3_stmt* rows = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT id, from_address FROM emails WHERE crypto_version IN (0, 1)",
            -1, &rows, nullptr) == SQLITE_OK) {
        while (sqlite3_step(rows) == SQLITE_ROW) {
            const int id = sqlite3_column_int(rows, 0);
            const void* ptr = sqlite3_column_blob(rows, 1);
            const int len = sqlite3_column_bytes(rows, 1);
            if (!ptr || len <= 0) continue;
            const std::string raw(static_cast<const char*>(ptr), static_cast<size_t>(len));
            const std::string rid = std::to_string(id);
            std::string plain;
            const std::string row_key = tempmail_crypto::derive_row_key_public(master_key_, rid);
            int version = 0;
            if (tempmail_crypto::decrypt_field_with_key(row_key, raw, plain)) {
                version = 1;  // current row-key scheme
            } else if (tempmail_crypto::decrypt_field(master_key_, rid + ":from", raw, plain)) {
                version = 2;  // historical per-field scheme
            }
            if (version != 0) encrypted_ids.emplace_back(id, version);
        }
    }
    sqlite3_finalize(rows);
    sqlite3_stmt* mark = nullptr;
    if (!encrypted_ids.empty() &&
        sqlite3_prepare_v2(db_, "UPDATE emails SET crypto_version = ? WHERE id = ?", -1, &mark, nullptr) == SQLITE_OK) {
        for (const auto& [id, version] : encrypted_ids) {
            sqlite3_bind_int(mark, 1, version);
            sqlite3_bind_int(mark, 2, id);
            sqlite3_step(mark);
            sqlite3_reset(mark);
            sqlite3_clear_bindings(mark);
        }
    }
    sqlite3_finalize(mark);
}

Alias Database::create_alias(const std::string& email, const std::string& expires_at,
                             const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    Alias a;
    // Client-side random id doubles as the per-alias salt for the key hash;
    // only the salted HMAC of the API key is stored, never the raw key.
    static thread_local std::mt19937_64 gen(std::random_device{}());
    std::ostringstream id_stream;
    id_stream << std::hex << std::setfill('0') << std::setw(16) << gen() << std::setw(16) << gen();
    const std::string alias_id = id_stream.str();
    const std::string key_hash = api_key.empty()
        ? std::string()
        : tempmail_crypto::hash_api_key(alias_id, api_key);
    const std::string key_lookup = (api_key.empty() || master_key_.empty())
        ? std::string()
        : tempmail_crypto::hmac_sha256(master_key_, api_key);
    const char* sql = "INSERT INTO aliases (id, email, expires_at, api_key, key_lookup) VALUES (?, ?, ?, NULLIF(?, ''), NULLIF(?, '')) RETURNING id, email, created_at, expires_at";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, alias_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, expires_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, key_hash.c_str(), static_cast<int>(key_hash.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 5, key_lookup.data(), static_cast<int>(key_lookup.size()), SQLITE_TRANSIENT);
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
        WHERE a.email = ? AND datetime(a.expires_at) > datetime('now') GROUP BY a.id
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
    // The stored value is HMAC(alias_id, key); alias_id is unknown before the
    // lookup, so scan active keyed aliases and compare in constant time.
    // Alias counts are small (temporary inboxes), making this scan cheap.
    if (api_key.empty()) return std::nullopt;
    // Fast path: deterministic HMAC(master, key) lookup via index — O(1).
    if (!master_key_.empty()) {
        const std::string lookup = tempmail_crypto::hmac_sha256(master_key_, api_key);
        const char* idx_sql = R"(
            SELECT a.id, a.email, a.created_at, a.expires_at, a.api_key, COUNT(e.id)
            FROM aliases a LEFT JOIN emails e ON a.id = e.alias_id
            WHERE a.key_lookup = ? AND datetime(a.expires_at) > datetime('now')
            GROUP BY a.id
        )";
        sqlite3_stmt* idx_stmt = nullptr;
        sqlite3_prepare_v2(db_, idx_sql, -1, &idx_stmt, nullptr);
        sqlite3_bind_blob(idx_stmt, 1, lookup.data(), static_cast<int>(lookup.size()), SQLITE_STATIC);
        std::optional<Alias> indexed;
        if (sqlite3_step(idx_stmt) == SQLITE_ROW) {
            Alias a;
            a.id = reinterpret_cast<const char*>(sqlite3_column_text(idx_stmt, 0));
            a.email = reinterpret_cast<const char*>(sqlite3_column_text(idx_stmt, 1));
            a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(idx_stmt, 2));
            a.expires_at = reinterpret_cast<const char*>(sqlite3_column_text(idx_stmt, 3));
            a.email_count = sqlite3_column_int(idx_stmt, 5);
            indexed = a;
        }
        sqlite3_finalize(idx_stmt);
        if (indexed) return indexed;
        // Miss on the index: fall through only for legacy rows that predate
        // key_lookup (they still carry hashes/plaintext in api_key).
    }
    const char* sql = R"(
        SELECT a.id, a.email, a.created_at, a.expires_at, a.api_key, COUNT(e.id)
        FROM aliases a LEFT JOIN emails e ON a.id = e.alias_id
        WHERE a.api_key IS NOT NULL AND datetime(a.expires_at) > datetime('now')
        GROUP BY a.id
    )";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    std::optional<Alias> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const std::string alias_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* stored_raw = reinterpret_cast<const char*>(sqlite3_column_blob(stmt, 4));
        const int stored_len = sqlite3_column_bytes(stmt, 4);
        if (!stored_raw || stored_len <= 0) continue;
        const std::string stored(stored_raw, static_cast<size_t>(stored_len));
        // Hashed keys are 32-byte binary HMACs; legacy plaintext keys were
        // 39-char base62 strings. Compare in constant time either way.
        if (stored.size() == 32) {
            const std::string computed = tempmail_crypto::hash_api_key(alias_id, api_key);
            if (!tempmail_crypto::secure_equals(computed, stored)) continue;
        } else if (!tempmail_crypto::secure_equals(stored, api_key)) {
            continue;
        }
        Alias a;
        a.id = alias_id;
        a.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        a.expires_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        a.email_count = sqlite3_column_int(stmt, 5);
        result = a;
        break;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::api_key_owns_alias(const std::string& api_key, const std::string& email) {
    auto alias = get_alias_by_api_key(api_key);
    return alias.has_value() && alias->email == email;
}

bool Database::api_key_owns_email(const std::string& api_key, int email_id) {
    auto alias = get_alias_by_api_key(api_key);
    if (!alias) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM emails WHERE id = ? AND alias_id = ?";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, email_id);
    sqlite3_bind_text(stmt, 2, alias->id.c_str(), -1, SQLITE_TRANSIENT);
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
        WHERE datetime(a.expires_at) > datetime('now') AND (? = '' OR a.api_key = ?)
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
    auto alias = get_alias_by_api_key(api_key);
    if (!alias) return false;
    return delete_alias(alias->email);
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
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return -1;
    }
    // Foreign keys are intentionally not required by the minimal SQLite build;
    // delete dependent rows before aliases so no orphan survives another hour.
    bool ok = sqlite3_exec(db_,
        "DELETE FROM emails WHERE alias_id IN "
        "(SELECT id FROM aliases WHERE datetime(expires_at) < datetime('now'))",
        nullptr, nullptr, &err) == SQLITE_OK;
    if (err) { sqlite3_free(err); err = nullptr; }
    if (ok) {
        ok = sqlite3_exec(db_,
            "DELETE FROM aliases WHERE datetime(expires_at) < datetime('now')",
            nullptr, nullptr, &err) == SQLITE_OK;
    }
    const int deleted_aliases = ok ? sqlite3_changes(db_) : -1;
    if (err) { sqlite3_free(err); err = nullptr; }
    // Also clear any historical orphans from older cleanup behavior.
    if (ok) {
        ok = sqlite3_exec(db_,
            "DELETE FROM emails WHERE alias_id NOT IN (SELECT id FROM aliases)",
            nullptr, nullptr, &err) == SQLITE_OK;
    }
    if (err) sqlite3_free(err);
    sqlite3_exec(db_, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok ? deleted_aliases : -1;
}

int Database::store_email(const std::string& alias_id, const std::string& from,
                          const std::string& to, const std::string& subject,
                          const std::string& body_text, const std::string& body_html) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Two-step write: insert to obtain the row id, then store AES-256-GCM
    // ciphertexts keyed by that row id (HKDF(master, row_id) per row).
    // Keep placeholder INSERT + encrypted UPDATE in one atomic transaction.
    // If encryption/update fails, rollback removes the placeholder row too.
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return -1;
    const char* insert = "INSERT INTO emails (alias_id, from_address, to_address, subject, body_text, body_html) VALUES (?, '', '', '', '', '')";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, insert, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, alias_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return -1;
    }
    sqlite3_finalize(stmt);
    const int row_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    const std::string rid = std::to_string(row_id);
    const char* update = "UPDATE emails SET from_address = ?, to_address = ?, subject = ?, body_text = ?, body_html = ?, crypto_version = 1 WHERE id = ?";
    if (sqlite3_prepare_v2(db_, update, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return -1;
    }
    // Write with the row-key scheme (one HKDF, five random-nonce GCM blobs).
    // Reads try this first; the per-field scheme remains only as a read
    // fallback for rows written before the batching optimization.
    const std::string row_key = tempmail_crypto::derive_row_key_public(master_key_, rid);
    const std::string e_from = tempmail_crypto::encrypt_field_with_key(row_key, from);
    const std::string e_to = tempmail_crypto::encrypt_field_with_key(row_key, to);
    const std::string e_subject = tempmail_crypto::encrypt_field_with_key(row_key, subject);
    const std::string e_text = tempmail_crypto::encrypt_field_with_key(row_key, body_text);
    const std::string e_html = tempmail_crypto::encrypt_field_with_key(row_key, body_html);
    sqlite3_bind_text(stmt, 1, e_from.c_str(), static_cast<int>(e_from.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, e_to.c_str(), static_cast<int>(e_to.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, e_subject.c_str(), static_cast<int>(e_subject.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, e_text.c_str(), static_cast<int>(e_text.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, e_html.c_str(), static_cast<int>(e_html.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, row_id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    sqlite3_exec(db_, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok ? row_id : -1;
}

namespace {
// Read a BLOB-ish TEXT column as raw bytes (length-aware, may contain NULs).
std::string column_blob(sqlite3_stmt* stmt, int col) {
    const void* ptr = sqlite3_column_blob(stmt, col);
    const int len = sqlite3_column_bytes(stmt, col);
    if (!ptr || len <= 0) return std::string();
    return std::string(static_cast<const char*>(ptr), static_cast<size_t>(len));
}
}  // namespace

std::vector<Email> Database::get_emails(const std::string& alias_id, int after_id,
                                        bool include_bodies) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Email> emails;
    const char* sql = "SELECT id, alias_id, from_address, to_address, subject, body_text, body_html, received_at, is_read, crypto_version FROM emails WHERE alias_id = ? AND id > ? ORDER BY id DESC";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, alias_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, after_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Email e;
        e.id = sqlite3_column_int(stmt, 0);
        e.alias_id = column_blob(stmt, 1);
        const std::string rid = std::to_string(e.id);
        e.crypto_version = sqlite3_column_int(stmt, 9);
        // One HKDF derivation per row; all fields decrypt with this key.
        const std::string row_key = tempmail_crypto::derive_row_key_public(master_key_, rid);
        auto decrypt_column = [&](int column, const char* legacy_suffix) -> std::string {
            const std::string raw = column_blob(stmt, column);
            std::string plain;
            if (e.crypto_version == 1) {
                if (tempmail_crypto::decrypt_field_with_key(row_key, raw, plain)) return plain;
                throw std::runtime_error("Encrypted row authentication failed");
            }
            if (e.crypto_version == 2) {
                if (tempmail_crypto::decrypt_field(master_key_, rid + legacy_suffix, raw, plain)) return plain;
                throw std::runtime_error("Legacy encrypted row authentication failed");
            }
            // Unversioned legacy: detect either scheme, then plaintext.
            if (tempmail_crypto::decrypt_field_with_key(row_key, raw, plain)) return plain;
            if (tempmail_crypto::decrypt_field(master_key_, rid + legacy_suffix, raw, plain)) return plain;
            return raw;
        };
        e.from_address = decrypt_column(2, ":from");
        e.to_address = decrypt_column(3, ":to");
        e.subject = decrypt_column(4, ":subj");
        if (include_bodies) {
            e.body_text = decrypt_column(5, ":text");
            e.body_html = decrypt_column(6, ":html");
        }
        e.received_at = column_blob(stmt, 7);
        e.is_read = sqlite3_column_int(stmt, 8) != 0;
        emails.push_back(e);
    }
    sqlite3_finalize(stmt);
    return emails;
}

std::optional<Email> Database::get_email(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "SELECT id, alias_id, from_address, to_address, subject, body_text, body_html, received_at, is_read, crypto_version FROM emails WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    std::optional<Email> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Email e;
        e.id = sqlite3_column_int(stmt, 0);
        e.alias_id = column_blob(stmt, 1);
        const std::string rid = std::to_string(e.id);
        e.crypto_version = sqlite3_column_int(stmt, 9);
        const std::string row_key = tempmail_crypto::derive_row_key_public(master_key_, rid);
        auto decrypt_column = [&](int column, const char* legacy_suffix) -> std::string {
            const std::string raw = column_blob(stmt, column);
            std::string plain;
            if (e.crypto_version == 1) {
                if (tempmail_crypto::decrypt_field_with_key(row_key, raw, plain)) return plain;
                throw std::runtime_error("Encrypted row authentication failed");
            }
            if (e.crypto_version == 2) {
                if (tempmail_crypto::decrypt_field(master_key_, rid + legacy_suffix, raw, plain)) return plain;
                throw std::runtime_error("Legacy encrypted row authentication failed");
            }
            if (tempmail_crypto::decrypt_field_with_key(row_key, raw, plain)) return plain;
            if (tempmail_crypto::decrypt_field(master_key_, rid + legacy_suffix, raw, plain)) return plain;
            return raw;
        };
        e.from_address = decrypt_column(2, ":from");
        e.to_address = decrypt_column(3, ":to");
        e.subject = decrypt_column(4, ":subj");
        e.body_text = decrypt_column(5, ":text");
        e.body_html = decrypt_column(6, ":html");
        e.received_at = column_blob(stmt, 7);
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
