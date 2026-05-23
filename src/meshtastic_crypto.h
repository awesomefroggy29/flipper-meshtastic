#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ============ CRYPTO KEY ============
typedef struct {
    uint8_t bytes[32];
    int8_t length;  // -1 = invalid, 0 = no crypto, 16 = AES128, 32 = AES256
} CryptoKey;

// ============ PKI CONSTANTS ============
#define MESHTASTIC_PKI_TAG_LEN 8
#define MESHTASTIC_PKI_OVERHEAD 12

// ============ AES CTR ============
void crypto_aes_ctr(CryptoKey key, uint8_t *nonce, size_t numBytes, uint8_t *bytes);

// ============ NONCE GENERATION ============
void crypto_init_nonce(uint8_t *nonce, uint32_t fromNode, uint32_t packetId, uint32_t extraNonce);

// ============ KEY EXPANSION ============
CryptoKey crypto_expand_psk(const uint8_t *psk, size_t psk_len);

// ============ PACKET ENCRYPTION ============
void crypto_encrypt_packet(CryptoKey key, uint32_t fromNode, uint32_t packetId, 
                           size_t numBytes, uint8_t *bytes);

#define crypto_decrypt_packet crypto_encrypt_packet  // CTR mode is same for encrypt/decrypt

// ============ PKI (Curve25519 + AES-CCM + SHA256) ============
bool crypto_curve25519_generate_keypair(uint8_t* pub, uint8_t* priv);
bool crypto_curve25519_public(const uint8_t* priv, uint8_t* pub);
bool crypto_curve25519_shared(const uint8_t* priv, const uint8_t* pub, uint8_t* shared_out);
bool crypto_pki_encrypt(
    uint32_t toNode,
    uint32_t fromNode,
    uint32_t packetId,
    const uint8_t* remote_pub,
    const uint8_t* local_priv,
    const uint8_t* plain,
    size_t plain_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
bool crypto_pki_decrypt(
    uint32_t fromNode,
    uint32_t packetId,
    const uint8_t* remote_pub,
    const uint8_t* local_priv,
    const uint8_t* crypt,
    size_t crypt_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
