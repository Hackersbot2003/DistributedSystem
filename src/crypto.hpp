#pragma once
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <stdexcept>
#include <array>

namespace dfs::crypto {

constexpr size_t KEY_SIZE = 32;   // AES-256 = 32-byte key
constexpr size_t IV_SIZE = 12;    // GCM standard nonce size
constexpr size_t TAG_SIZE = 16;   // GCM authentication tag

using Key = std::array<unsigned char, KEY_SIZE>;

inline Key generateKey() {
    Key key;
    if (RAND_bytes(key.data(), KEY_SIZE) != 1)
        throw std::runtime_error("Failed to generate random key");
    return key;
}

// Encrypts plaintext with AES-256-GCM.
// Output layout: [12-byte IV][ciphertext][16-byte auth tag]
inline std::vector<unsigned char> encrypt(const std::vector<unsigned char>& plaintext,
                                            const Key& key) {
    std::vector<unsigned char> iv(IV_SIZE);
    if (RAND_bytes(iv.data(), IV_SIZE) != 1)
        throw std::runtime_error("Failed to generate IV");

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create cipher context");

    std::vector<unsigned char> ciphertext(plaintext.size());
    std::vector<unsigned char> tag(TAG_SIZE);
    int outLen = 0, totalLen = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext.data(), &outLen, plaintext.data(), (int)plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption failed");
    }
    totalLen = outLen;

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + totalLen, &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption finalization failed");
    }
    totalLen += outLen;
    ciphertext.resize(totalLen);

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to get auth tag");
    }
    EVP_CIPHER_CTX_free(ctx);

    // Concatenate: IV + ciphertext + tag
    std::vector<unsigned char> output;
    output.reserve(IV_SIZE + ciphertext.size() + TAG_SIZE);
    output.insert(output.end(), iv.begin(), iv.end());
    output.insert(output.end(), ciphertext.begin(), ciphertext.end());
    output.insert(output.end(), tag.begin(), tag.end());
    return output;
}

// Decrypts data produced by encrypt(). Throws if authentication fails (tampered data).
inline std::vector<unsigned char> decrypt(const std::vector<unsigned char>& data,
                                            const Key& key) {
    if (data.size() < IV_SIZE + TAG_SIZE)
        throw std::runtime_error("Encrypted data too short");

    const unsigned char* iv = data.data();
    const unsigned char* ciphertext = data.data() + IV_SIZE;
    size_t ciphertextLen = data.size() - IV_SIZE - TAG_SIZE;
    const unsigned char* tag = data.data() + IV_SIZE + ciphertextLen;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create cipher context");

    std::vector<unsigned char> plaintext(ciphertextLen);
    int outLen = 0, totalLen = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) != 1 ||
        EVP_DecryptUpdate(ctx, plaintext.data(), &outLen, ciphertext, (int)ciphertextLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption failed");
    }
    totalLen = outLen;

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, (void*)tag);

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + totalLen, &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption failed: authentication tag mismatch (data tampered or wrong key)");
    }
    totalLen += outLen;
    plaintext.resize(totalLen);

    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

inline std::string keyToHex(const Key& key) {
    static const char* hexChars = "0123456789abcdef";
    std::string out;
    for (auto b : key) {
        out += hexChars[(b >> 4) & 0xF];
        out += hexChars[b & 0xF];
    }
    return out;
}

inline Key keyFromHex(const std::string& hex) {
    Key key;
    for (size_t i = 0; i < KEY_SIZE; ++i) {
        key[i] = static_cast<unsigned char>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }
    return key;
}

} // namespace dfs::crypto