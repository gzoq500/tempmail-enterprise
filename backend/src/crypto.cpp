#include "crypto.h"

#include <sys/stat.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>

#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>

extern "C" {
#include "api.h"  // PQClean ML-KEM-768
}

namespace tempmail_crypto {
namespace {

std::string random_bytes(size_t n) {
    std::string out(n, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&out[0]), static_cast<int>(n)) != 1) {
        // Fallback to std::random_device (should never happen on OpenSSL 3).
        std::random_device rd;
        for (size_t i = 0; i < n; ++i) out[i] = static_cast<char>(rd() & 0xff);
    }
    return out;
}

void throw_if(bool cond, const char* what) {
    if (cond) throw std::runtime_error(what);
}

}  // namespace

// --- SHA-256 / HMAC ---

std::string sha256(const std::string& data) {
    std::string out(SHA256_LEN, '\0');
    unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    throw_if(!ctx, "EVP_MD_CTX_new failed");
    try {
        throw_if(EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1, "sha256 init");
        throw_if(EVP_DigestUpdate(ctx, data.data(), data.size()) != 1, "sha256 update");
        throw_if(EVP_DigestFinal_ex(ctx, reinterpret_cast<unsigned char*>(&out[0]), &len) != 1,
                 "sha256 final");
    } catch (...) {
        EVP_MD_CTX_free(ctx);
        throw;
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

std::string hmac_sha256(const std::string& key, const std::string& data) {
    std::string out(SHA256_LEN, '\0');
    unsigned int len = 0;
    throw_if(HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                  reinterpret_cast<const unsigned char*>(data.data()), data.size(),
                  reinterpret_cast<unsigned char*>(&out[0]), &len) == nullptr,
             "hmac failed");
    return out;
}

// --- AES-256-GCM ---

std::string aes256gcm_encrypt(const std::string& key, const std::string& plaintext) {
    throw_if(key.size() != MASTER_KEY_LEN, "bad AES key length");
    std::string nonce = random_bytes(GCM_NONCE_LEN);
    std::string blob(nonce);
    blob.resize(GCM_NONCE_LEN + plaintext.size() + GCM_TAG_LEN);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    throw_if(!ctx, "EVP_CIPHER_CTX_new failed");
    try {
        throw_if(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1, "gcm init");
        throw_if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_NONCE_LEN, nullptr) != 1, "gcm ivlen");
        throw_if(EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                                    reinterpret_cast<const unsigned char*>(key.data()),
                                    reinterpret_cast<const unsigned char*>(nonce.data())) != 1,
                 "gcm key+iv");
        int out_len = 0;
        size_t offset = GCM_NONCE_LEN;
        if (!plaintext.empty()) {
            throw_if(EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(&blob[offset]), &out_len,
                                       reinterpret_cast<const unsigned char*>(plaintext.data()),
                                       static_cast<int>(plaintext.size())) != 1,
                     "gcm update");
            offset += static_cast<size_t>(out_len);
        }
        throw_if(EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&blob[offset]), &out_len) != 1,
                 "gcm final");
        throw_if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LEN,
                                     &blob[GCM_NONCE_LEN + plaintext.size()]) != 1,
                 "gcm tag");
    } catch (...) {
        EVP_CIPHER_CTX_free(ctx);
        throw;
    }
    EVP_CIPHER_CTX_free(ctx);
    return blob;
}

bool aes256gcm_decrypt(const std::string& key, const std::string& blob, std::string& plaintext) {
    if (key.size() != MASTER_KEY_LEN) return false;
    if (blob.size() < GCM_NONCE_LEN + GCM_TAG_LEN) return false;
    const size_t ct_len = blob.size() - GCM_NONCE_LEN - GCM_TAG_LEN;
    plaintext.assign(ct_len, '\0');

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = true;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) { ok = false; break; }
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_NONCE_LEN, nullptr) != 1) { ok = false; break; }
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                               reinterpret_cast<const unsigned char*>(key.data()),
                               reinterpret_cast<const unsigned char*>(blob.data())) != 1) { ok = false; break; }
        int out_len = 0;
        if (ct_len > 0) {
            if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(&plaintext[0]), &out_len,
                                  reinterpret_cast<const unsigned char*>(blob.data()) + GCM_NONCE_LEN,
                                  static_cast<int>(ct_len)) != 1) { ok = false; break; }
        }
        std::string tag = blob.substr(GCM_NONCE_LEN + ct_len, GCM_TAG_LEN);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LEN, &tag[0]) != 1) { ok = false; break; }
        if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&plaintext[0]) + out_len, &out_len) != 1) {
            ok = false; break;
        }
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) plaintext.clear();
    return ok;
}

// --- API key hashing ---

std::string hash_api_key(const std::string& salt, const std::string& api_key) {
    return hmac_sha256(salt, api_key);
}

bool secure_equals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

// --- Per-row email encryption ---

namespace {

// HKDF-SHA256: extract(master) -> expand(info=row_id) -> 32-byte row key.
std::string derive_row_key(const std::string& master_key, const std::string& row_id) {
    const std::string salt = "tempmail-row-v1";
    std::string prk(SHA256_LEN, '\0');
    unsigned int prk_len = 0;
    throw_if(HMAC(EVP_sha256(), salt.data(), static_cast<int>(salt.size()),
                  reinterpret_cast<const unsigned char*>(master_key.data()), master_key.size(),
                  reinterpret_cast<unsigned char*>(&prk[0]), &prk_len) == nullptr,
             "hkdf extract failed");

    std::string okm(MASTER_KEY_LEN, '\0');
    size_t out_len = 0;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    throw_if(!ctx, "hkdf ctx failed");
    try {
        throw_if(EVP_PKEY_derive_init(ctx) != 1, "hkdf init");
        throw_if(EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) != 1, "hkdf md");
        throw_if(EVP_PKEY_CTX_set1_hkdf_salt(ctx,
                    reinterpret_cast<const unsigned char*>(salt.data()),
                    static_cast<int>(salt.size())) != 1, "hkdf salt");
        throw_if(EVP_PKEY_CTX_set1_hkdf_key(ctx,
                    reinterpret_cast<const unsigned char*>(prk.data()),
                    static_cast<int>(prk.size())) != 1, "hkdf key");
        throw_if(EVP_PKEY_CTX_add1_hkdf_info(ctx,
                    reinterpret_cast<const unsigned char*>(row_id.data()),
                    static_cast<int>(row_id.size())) != 1, "hkdf info");
        out_len = okm.size();
        throw_if(EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char*>(&okm[0]), &out_len) != 1,
                 "hkdf derive");
    } catch (...) {
        EVP_PKEY_CTX_free(ctx);
        throw;
    }
    EVP_PKEY_CTX_free(ctx);
    return okm;
}

}  // namespace

std::string encrypt_field(const std::string& master_key, const std::string& row_id,
                          const std::string& plaintext) {
    if (plaintext.empty()) return std::string();
    return aes256gcm_encrypt(derive_row_key(master_key, row_id), plaintext);
}

bool decrypt_field(const std::string& master_key, const std::string& row_id,
                   const std::string& blob, std::string& plaintext) {
    if (blob.empty()) { plaintext.clear(); return true; }
    return aes256gcm_decrypt(derive_row_key(master_key, row_id), blob, plaintext);
}

// --- Kyber (ML-KEM-768) master key envelope ---

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file_mode(const std::string& path, const std::string& data, mode_t mode) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    throw_if(!out, ("cannot write " + path).c_str());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    throw_if(chmod(path.c_str(), mode) != 0, ("cannot chmod " + path).c_str());
}

}  // namespace

bool load_or_create_master_key(const std::string& path_base,
                               const std::string& kyber_dir,
                               std::string& master_key) {
    const std::string pk_path = kyber_dir + "/mlkem768.pk";
    const std::string sk_path = kyber_dir + "/mlkem768.sk";
    const std::string env_path = path_base + ".kyber-envelope";

    std::string pk = read_file(pk_path);
    std::string sk = read_file(sk_path);
    const std::string envelope = read_file(env_path);

    if (pk.size() == KYBER_PK_LEN && sk.size() == KYBER_SK_LEN && envelope.size() > KYBER_CT_LEN) {
        // Existing envelope: decapsulate with the Kyber secret key.
        const std::string ct = envelope.substr(0, KYBER_CT_LEN);
        const std::string wrapped = envelope.substr(KYBER_CT_LEN);
        std::string ss(32, '\0');
        if (PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(
                reinterpret_cast<uint8_t*>(&ss[0]),
                reinterpret_cast<const uint8_t*>(ct.data()),
                reinterpret_cast<const uint8_t*>(sk.data())) != 0) {
            return false;
        }
        // wrapped = AES-GCM(ss, master_key)
        return aes256gcm_decrypt(ss, wrapped, master_key);
    }

    if (!pk.empty() || !sk.empty() || !envelope.empty()) {
        // Partial state: refuse rather than silently replace keys.
        return false;
    }

    // First boot: generate Kyber keypair + master key, write the envelope.
    try {
        pk.resize(KYBER_PK_LEN);
        sk.resize(KYBER_SK_LEN);
        if (PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(
                reinterpret_cast<uint8_t*>(&pk[0]),
                reinterpret_cast<uint8_t*>(&sk[0])) != 0) {
            return false;
        }
        std::string ct(KYBER_CT_LEN, '\0');
        std::string ss(32, '\0');
        if (PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(
                reinterpret_cast<uint8_t*>(&ct[0]),
                reinterpret_cast<uint8_t*>(&ss[0]),
                reinterpret_cast<const uint8_t*>(pk.data())) != 0) {
            return false;
        }
        master_key = random_bytes(MASTER_KEY_LEN);
        const std::string wrapped = aes256gcm_encrypt(ss, master_key);
        write_file_mode(pk_path, pk, 0644);
        write_file_mode(sk_path, sk, 0600);
        write_file_mode(env_path, ct + wrapped, 0600);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace tempmail_crypto
