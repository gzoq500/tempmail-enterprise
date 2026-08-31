#pragma once
// TempMail cryptography module.
//
// - API keys are stored as SHA-256(salt || key); the raw key never touches disk.
// - Email bodies are encrypted at rest with AES-256-GCM using per-row random
//   nonces. Row keys are derived from a master key.
// - The master key is generated once, then protected with ML-KEM-768 (Kyber):
//   a Kyber keypair lives beside the database; the master key is encapsulated
//   to the Kyber public key and only decapsulated at startup.

#include <cstdint>
#include <string>
#include <vector>

namespace tempmail_crypto {

constexpr size_t MASTER_KEY_LEN = 32;    // AES-256
constexpr size_t GCM_NONCE_LEN = 12;
constexpr size_t GCM_TAG_LEN = 16;
constexpr size_t SHA256_LEN = 32;
constexpr size_t KYBER_CT_LEN = 1088;    // ML-KEM-768 ciphertext
constexpr size_t KYBER_PK_LEN = 1184;
constexpr size_t KYBER_SK_LEN = 2400;

// --- SHA-256 ---
std::string sha256(const std::string& data);
std::string hmac_sha256(const std::string& key, const std::string& data);

// --- AES-256-GCM ---
// Returns nonce(12) || ciphertext || tag(16).
std::string aes256gcm_encrypt(const std::string& key, const std::string& plaintext);
// Returns false on authentication failure.
bool aes256gcm_decrypt(const std::string& key, const std::string& blob, std::string& plaintext);

// --- API key hashing ---
std::string hash_api_key(const std::string& salt, const std::string& api_key);
// Constant-time equality.
bool secure_equals(const std::string& a, const std::string& b);

// --- Per-row email encryption (HKDF(master, row_id) -> AES-GCM) ---
std::string encrypt_field(const std::string& master_key, const std::string& row_id,
                          const std::string& plaintext);
bool decrypt_field(const std::string& master_key, const std::string& row_id,
                   const std::string& blob, std::string& plaintext);

// --- Kyber (ML-KEM-768) master key envelope ---
// Loads or creates <path>.pk / <path>.sk, then reads <path>.bin
// (Kyber ct || AES-GCM wrapped master key). On first run, generates a fresh
// master key and writes the envelope. Returns the raw master key.
// Fails (returns false) when the envelope exists but cannot be opened.
bool load_or_create_master_key(const std::string& path_base,
                               const std::string& kyber_dir,
                               std::string& master_key);

}  // namespace tempmail_crypto
