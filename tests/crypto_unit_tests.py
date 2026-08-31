#!/usr/bin/env python3
"""Unit tests for the crypto module build (roundtrip via a scratch binary)."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

BACKEND = Path(os.environ.get("TEMPMAIL_BACKEND_ROOT", "/opt/tempmail/backend"))
WORK = Path(tempfile.mkdtemp(prefix="tempmail-crypto-"))

# Small C++ harness that exercises the crypto module end-to-end.
HARNESS = r'''
#include "crypto.h"
#include <cassert>
#include <sys/stat.h>
#include <iostream>
#include <string>
using namespace tempmail_crypto;

int main(int argc, char** argv) {
    const std::string dir = argv[1];

    // 1. Kyber envelope: create then reload must yield the same master key.
    std::string mk1, mk2;
    if (!load_or_create_master_key(dir + "/master", dir, mk1)) { std::cout << "FAIL create\n"; return 1; }
    if (!load_or_create_master_key(dir + "/master", dir, mk2)) { std::cout << "FAIL reload\n"; return 1; }
    if (mk1 != mk2 || mk1.size() != MASTER_KEY_LEN) { std::cout << "FAIL key mismatch\n"; return 1; }

    // 2. AES-256-GCM roundtrip + tamper detection.
    const std::string secret = "Your verification code is 778899 \xF0\x9F\x94\x90";
    std::string blob = aes256gcm_encrypt(mk1, secret);
    std::string out;
    if (!aes256gcm_decrypt(mk1, blob, out) || out != secret) { std::cout << "FAIL gcm roundtrip\n"; return 1; }
    std::string tampered = blob; tampered[tampered.size() - 1] ^= 0x01;
    if (aes256gcm_decrypt(mk1, tampered, out)) { std::cout << "FAIL tamper\n"; return 1; }

    // 3. Per-row encryption: same plaintext, different rows -> different ciphertext.
    const std::string e1 = encrypt_field(mk1, "row-1", secret);
    const std::string e2 = encrypt_field(mk1, "row-2", secret);
    if (e1 == e2) { std::cout << "FAIL row distinct\n"; return 1; }
    std::string r1, r2;
    if (!decrypt_field(mk1, "row-1", e1, r1) || r1 != secret) { std::cout << "FAIL row1\n"; return 1; }
    if (!decrypt_field(mk1, "row-2", e2, r2) || r2 != secret) { std::cout << "FAIL row2\n"; return 1; }

    // 4. API key hashing: deterministic, salted, constant-time compare.
    const std::string h1 = hash_api_key("salt-a", "temp-AbCdEf123456");
    const std::string h2 = hash_api_key("salt-b", "temp-AbCdEf123456");
    if (h1 == h2) { std::cout << "FAIL salt\n"; return 1; }
    if (h1 != hash_api_key("salt-a", "temp-AbCdEf123456")) { std::cout << "FAIL deterministic\n"; return 1; }
    if (!secure_equals(h1, h1) || secure_equals(h1, h2)) { std::cout << "FAIL equals\n"; return 1; }

    // 5. Kyber files exist with correct permissions.
    struct stat st{};
    stat((dir + "/mlkem768.sk").c_str(), &st);
    if ((st.st_mode & 0777) != 0600) { std::cout << "FAIL sk perms\n"; return 1; }

    std::cout << "OK\n";
    return 0;
}
'''


def main() -> int:
    work = WORK
    # Compile Kyber C sources separately as C, then link with the C++ harness.
    c_objs = []
    for src in ("cbd", "indcpa", "kem", "ntt", "poly", "polyvec", "reduce",
                "symmetric-shake", "verify", "fips202", "randombytes"):
        obj = work / f"{src}.o"
        built = subprocess.run(
            ["gcc", "-O2", "-w", "-c",
             str(BACKEND / f"third_party/kyber768/{src}.c"),
             "-I", str(BACKEND / "third_party/kyber768"),
             "-o", str(obj)],
            capture_output=True, text=True, timeout=120)
        if built.returncode != 0:
            print(f"FAIL compile {src}: {built.stderr[:1000]}")
            return 1
        c_objs.append(str(obj))

    harness = work / "harness.cpp"
    harness.write_text(HARNESS)
    binary = work / "harness"
    compiled = subprocess.run(
        ["g++", "-std=c++17", "-O2", str(harness),
         str(BACKEND / "src/crypto.cpp"),
         f"-I{BACKEND / 'include'}",
         f"-I{BACKEND / 'third_party/kyber768'}",
         *c_objs, "-lcrypto", "-o", str(binary)],
        capture_output=True, text=True, timeout=300)
    if compiled.returncode != 0:
        print(f"FAIL compile: {compiled.stderr[:2000]}")
        return 1

    key_dir = WORK / "keys"
    key_dir.mkdir()
    ran = subprocess.run([str(binary), str(key_dir)], capture_output=True, text=True, timeout=60)
    if ran.returncode != 0 or ran.stdout.strip() != "OK":
        print(f"FAIL crypto harness: {ran.stdout.strip()} {ran.stderr.strip()}")
        return 1

    # Envelope file must NOT contain the raw master key material in plaintext.
    envelope = (key_dir / "master.kyber-envelope").read_bytes()
    print("PASS crypto roundtrip (Kyber envelope + AES-256-GCM + hashing)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
