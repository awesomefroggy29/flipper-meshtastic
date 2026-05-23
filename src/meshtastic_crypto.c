#include "meshtastic_crypto.h"
#include <furi_hal_random.h>
#include "third_party/curve25519-donna.h"
#include "third_party/sha256.h"

#define AES_BLOCK_SIZE 16

// AES S-box
static const uint8_t sbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16};

static const uint8_t rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};

static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0x00));
}

static void sub_bytes(uint8_t* state) {
    for(size_t i = 0; i < 16; i++) state[i] = sbox[state[i]];
}

static void shift_rows(uint8_t* state) {
    uint8_t tmp;
    // Row 1
    tmp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = tmp;
    // Row 2
    tmp = state[2];
    state[2] = state[10];
    state[10] = tmp;
    tmp = state[6];
    state[6] = state[14];
    state[14] = tmp;
    // Row 3
    tmp = state[3];
    state[3] = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = tmp;
}

static void mix_columns(uint8_t* state) {
    for(uint8_t c = 0; c < 4; c++) {
        uint8_t* col = state + (c * 4);
        uint8_t t = col[0] ^ col[1] ^ col[2] ^ col[3];
        uint8_t u = col[0];
        col[0] ^= t ^ xtime(col[0] ^ col[1]);
        col[1] ^= t ^ xtime(col[1] ^ col[2]);
        col[2] ^= t ^ xtime(col[2] ^ col[3]);
        col[3] ^= t ^ xtime(col[3] ^ u);
    }
}

static void add_round_key(uint8_t* state, const uint8_t* round_key) {
    for(size_t i = 0; i < 16; i++) state[i] ^= round_key[i];
}

static void sub_word(uint8_t* w) {
    w[0] = sbox[w[0]];
    w[1] = sbox[w[1]];
    w[2] = sbox[w[2]];
    w[3] = sbox[w[3]];
}

static void rot_word(uint8_t* w) {
    uint8_t t = w[0];
    w[0] = w[1];
    w[1] = w[2];
    w[2] = w[3];
    w[3] = t;
}

static bool aes_key_expand(const uint8_t* key, size_t key_len, uint8_t* round_keys, uint8_t* rounds_out) {
    uint8_t Nk;
    uint8_t Nr;
    if(key_len == 16) {
        Nk = 4;
        Nr = 10;
    } else if(key_len == 32) {
        Nk = 8;
        Nr = 14;
    } else {
        return false;
    }
    if(rounds_out) *rounds_out = Nr;

    const uint16_t words = 4 * (Nr + 1);
    for(size_t i = 0; i < key_len; i++) round_keys[i] = key[i];

    uint8_t temp[4];
    for(uint16_t i = Nk; i < words; i++) {
        temp[0] = round_keys[(i - 1) * 4 + 0];
        temp[1] = round_keys[(i - 1) * 4 + 1];
        temp[2] = round_keys[(i - 1) * 4 + 2];
        temp[3] = round_keys[(i - 1) * 4 + 3];

        if(i % Nk == 0) {
            rot_word(temp);
            sub_word(temp);
            temp[0] ^= rcon[i / Nk];
        } else if(Nk > 6 && (i % Nk) == 4) {
            sub_word(temp);
        }

        round_keys[i * 4 + 0] = round_keys[(i - Nk) * 4 + 0] ^ temp[0];
        round_keys[i * 4 + 1] = round_keys[(i - Nk) * 4 + 1] ^ temp[1];
        round_keys[i * 4 + 2] = round_keys[(i - Nk) * 4 + 2] ^ temp[2];
        round_keys[i * 4 + 3] = round_keys[(i - Nk) * 4 + 3] ^ temp[3];
    }
    return true;
}

static void aes_encrypt_block(const uint8_t* in, uint8_t* out, const uint8_t* round_keys, uint8_t rounds) {
    uint8_t state[16];
    memcpy(state, in, sizeof(state));

    add_round_key(state, round_keys);
    for(uint8_t round = 1; round < rounds; round++) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, round_keys + (round * 16));
    }
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, round_keys + (rounds * 16));

    memcpy(out, state, sizeof(state));
}

static void aes_encrypt_block_key(const uint8_t* key, size_t key_len, const uint8_t* in, uint8_t* out) {
    uint8_t rounds = 0;
    uint8_t round_keys[240];
    if(!aes_key_expand(key, key_len, round_keys, &rounds)) {
        memset(out, 0, AES_BLOCK_SIZE);
        return;
    }
    aes_encrypt_block(in, out, round_keys, rounds);
}

static void ctr_inc_be(uint8_t* counter, size_t counter_size) {
    for(size_t i = 0; i < counter_size; i++) {
        size_t idx = counter_size - 1 - i;
        counter[idx]++;
        if(counter[idx] != 0) break;
    }
}

// ============ CRYPTO KEY ============
CryptoKey crypto_expand_psk(const uint8_t* psk, size_t psk_len) {
    CryptoKey key;
    memset(&key, 0, sizeof(key));
    key.length = -1;

    // Meshtastic default PSK (AES-128) from firmware Channels.h.
    static const uint8_t default_psk[16] = {
        0xD4, 0xF1, 0xBB, 0x3A, 0x20, 0x29, 0x07, 0x59,
        0xF0, 0xBC, 0xFF, 0xAB, 0xCF, 0x4E, 0x69, 0x01};

    if(psk_len == 0) {
        key.length = 0;
        return key;
    }

    if(psk_len == 1) {
        uint8_t psk_index = psk[0];
        if(psk_index == 0) {
            key.length = 0;
            return key;
        }
        memcpy(key.bytes, default_psk, sizeof(default_psk));
        key.length = sizeof(default_psk);
        key.bytes[sizeof(default_psk) - 1] =
            (uint8_t)(key.bytes[sizeof(default_psk) - 1] + psk_index - 1);
        return key;
    }

    if(psk_len <= 16) {
        memcpy(key.bytes, psk, psk_len);
        key.length = 16;
        return key;
    }

    if(psk_len <= 32) {
        memcpy(key.bytes, psk, psk_len);
        key.length = 32;
        return key;
    }

    return key;
}

// ============ NONCE GENERATION ============
void crypto_init_nonce(uint8_t* nonce, uint32_t fromNode, uint32_t packetId, uint32_t extraNonce) {
    memset(nonce, 0, 16);

    uint64_t pid64 = (uint64_t)packetId;
    // Match firmware behavior: memcpy into nonce (little-endian on ARM).
    memcpy(nonce, &pid64, sizeof(uint64_t));
    memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t));
    if(extraNonce) {
        // Firmware uses offset 4 for extraNonce (see CryptoEngine::initNonce)
        memcpy(nonce + sizeof(uint32_t), &extraNonce, sizeof(uint32_t));
    }
}

// ============ AES CTR ============
void crypto_aes_ctr(CryptoKey key, uint8_t* nonce, size_t numBytes, uint8_t* bytes) {
    if(key.length != 16 && key.length != 32) return;

    uint8_t rounds = 0;
    uint8_t round_keys[16 * (14 + 1)];
    if(!aes_key_expand(key.bytes, (size_t)key.length, round_keys, &rounds)) return;

    uint8_t counter[AES_BLOCK_SIZE];
    memcpy(counter, nonce, sizeof(counter));

    size_t offset = 0;
    while(offset < numBytes) {
        uint8_t stream[AES_BLOCK_SIZE];
        aes_encrypt_block(counter, stream, round_keys, rounds);

        size_t block_len = numBytes - offset;
        if(block_len > AES_BLOCK_SIZE) block_len = AES_BLOCK_SIZE;
        for(size_t i = 0; i < block_len; i++) {
            bytes[offset + i] ^= stream[i];
        }

        ctr_inc_be(counter + 12, 4);
        offset += block_len;
    }
}

// ============ PACKET ENCRYPTION ============
void crypto_encrypt_packet(CryptoKey key, uint32_t fromNode, uint32_t packetId, size_t numBytes, uint8_t* bytes) {
    if(key.length <= 0 || bytes == NULL || numBytes == 0) return;
    uint8_t nonce[16];
    crypto_init_nonce(nonce, fromNode, packetId, 0);
    crypto_aes_ctr(key, nonce, numBytes, bytes);
}

// ============ AES-CCM (PKI) ============
static int constant_time_compare(const void* a_, const void* b_, size_t len) {
    const volatile uint8_t* volatile a = (const volatile uint8_t* volatile)a_;
    const volatile uint8_t* volatile b = (const volatile uint8_t* volatile)b_;
    if(len == 0) return 0;
    if(a == NULL || b == NULL) return -1;
    volatile uint8_t d = 0U;
    for(size_t i = 0; i < len; i++) {
        d |= (uint8_t)(a[i] ^ b[i]);
    }
    return (1 & (((unsigned int)d - 1) >> 8)) - 1;
}

static void wpa_put_be16(uint8_t* a, uint16_t val) {
    a[0] = (uint8_t)(val >> 8);
    a[1] = (uint8_t)(val & 0xFF);
}

static void xor_aes_block(uint8_t* dst, const uint8_t* src) {
    for(uint8_t i = 0; i < AES_BLOCK_SIZE; i++) {
        dst[i] ^= src[i];
    }
}

static void aes_ccm_auth_start(
    const uint8_t* key,
    size_t key_len,
    size_t M,
    size_t L,
    const uint8_t* nonce,
    const uint8_t* aad,
    size_t aad_len,
    size_t plain_len,
    uint8_t* x) {
    uint8_t aad_buf[2 * AES_BLOCK_SIZE];
    uint8_t b[AES_BLOCK_SIZE];
    b[0] = aad_len ? 0x40 : 0;
    b[0] |= (uint8_t)(((M - 2) / 2) << 3);
    b[0] |= (uint8_t)(L - 1);
    memcpy(&b[1], nonce, 15 - L);
    wpa_put_be16(&b[AES_BLOCK_SIZE - L], (uint16_t)plain_len);
    aes_encrypt_block_key(key, key_len, b, x);
    if(!aad_len) return;
    wpa_put_be16(aad_buf, (uint16_t)aad_len);
    memcpy(aad_buf + 2, aad, aad_len);
    memset(aad_buf + 2 + aad_len, 0, sizeof(aad_buf) - 2 - aad_len);
    xor_aes_block(aad_buf, x);
    aes_encrypt_block_key(key, key_len, aad_buf, x);
    if(aad_len > AES_BLOCK_SIZE - 2) {
        xor_aes_block(&aad_buf[AES_BLOCK_SIZE], x);
        aes_encrypt_block_key(key, key_len, &aad_buf[AES_BLOCK_SIZE], x);
    }
}

static void aes_ccm_auth(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t* x) {
    size_t last = len % AES_BLOCK_SIZE;
    for(size_t i = 0; i < len / AES_BLOCK_SIZE; i++) {
        xor_aes_block(x, data);
        data += AES_BLOCK_SIZE;
        aes_encrypt_block_key(key, key_len, x, x);
    }
    if(last) {
        for(size_t i = 0; i < last; i++) {
            x[i] ^= *data++;
        }
        aes_encrypt_block_key(key, key_len, x, x);
    }
}

static void aes_ccm_encr_start(size_t L, const uint8_t* nonce, uint8_t* a) {
    a[0] = (uint8_t)(L - 1);
    memcpy(&a[1], nonce, 15 - L);
}

static void aes_ccm_encr(const uint8_t* key, size_t key_len, size_t L, const uint8_t* in, size_t len, uint8_t* out, uint8_t* a) {
    (void)L;
    size_t last = len % AES_BLOCK_SIZE;
    size_t i;
    for(i = 1; i <= len / AES_BLOCK_SIZE; i++) {
        wpa_put_be16(&a[AES_BLOCK_SIZE - 2], (uint16_t)i);
        aes_encrypt_block_key(key, key_len, a, out);
        xor_aes_block(out, in);
        out += AES_BLOCK_SIZE;
        in += AES_BLOCK_SIZE;
    }
    if(last) {
        wpa_put_be16(&a[AES_BLOCK_SIZE - 2], (uint16_t)i);
        aes_encrypt_block_key(key, key_len, a, out);
        for(i = 0; i < last; i++) {
            *out++ ^= *in++;
        }
    }
}

static void aes_ccm_encr_auth(const uint8_t* key, size_t key_len, size_t M, const uint8_t* x, uint8_t* a, uint8_t* auth) {
    uint8_t tmp[AES_BLOCK_SIZE];
    wpa_put_be16(&a[AES_BLOCK_SIZE - 2], 0);
    aes_encrypt_block_key(key, key_len, a, tmp);
    for(size_t i = 0; i < M; i++) {
        auth[i] = x[i] ^ tmp[i];
    }
}

static void aes_ccm_decr_auth(const uint8_t* key, size_t key_len, size_t M, uint8_t* a, const uint8_t* auth, uint8_t* t) {
    uint8_t tmp[AES_BLOCK_SIZE];
    wpa_put_be16(&a[AES_BLOCK_SIZE - 2], 0);
    aes_encrypt_block_key(key, key_len, a, tmp);
    for(size_t i = 0; i < M; i++) {
        t[i] = auth[i] ^ tmp[i];
    }
}

static int aes_ccm_ae(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* nonce,
    size_t M,
    const uint8_t* plain,
    size_t plain_len,
    const uint8_t* aad,
    size_t aad_len,
    uint8_t* crypt,
    uint8_t* auth) {
    const size_t L = 2;
    uint8_t x[AES_BLOCK_SIZE], a[AES_BLOCK_SIZE];
    if(aad_len > 30 || M > AES_BLOCK_SIZE) return -1;
    aes_ccm_auth_start(key, key_len, M, L, nonce, aad, aad_len, plain_len, x);
    aes_ccm_auth(key, key_len, plain, plain_len, x);
    aes_ccm_encr_start(L, nonce, a);
    aes_ccm_encr(key, key_len, L, plain, plain_len, crypt, a);
    aes_ccm_encr_auth(key, key_len, M, x, a, auth);
    return 0;
}

static bool aes_ccm_ad(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* nonce,
    size_t M,
    const uint8_t* crypt,
    size_t crypt_len,
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* auth,
    uint8_t* plain) {
    const size_t L = 2;
    uint8_t x[AES_BLOCK_SIZE], a[AES_BLOCK_SIZE], t[AES_BLOCK_SIZE];
    if(aad_len > 30 || M > AES_BLOCK_SIZE) return false;
    aes_ccm_encr_start(L, nonce, a);
    aes_ccm_decr_auth(key, key_len, M, a, auth, t);
    aes_ccm_encr(key, key_len, L, crypt, crypt_len, plain, a);
    aes_ccm_auth_start(key, key_len, M, L, nonce, aad, aad_len, crypt_len, x);
    aes_ccm_auth(key, key_len, plain, crypt_len, x);
    return constant_time_compare(x, t, M) == 0;
}

// ============ PKI (Curve25519 + AES-CCM + SHA256) ============
static void clamp_curve25519(uint8_t* priv) {
    priv[0] &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
}

static void crypto_sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

bool crypto_curve25519_public(const uint8_t* priv, uint8_t* pub) {
    if(!priv || !pub) return false;
    uint8_t sk[32];
    memcpy(sk, priv, sizeof(sk));
    clamp_curve25519(sk);
    uint8_t basepoint[32] = {9};
    return curve25519_donna(pub, sk, basepoint) == 0;
}

bool crypto_curve25519_generate_keypair(uint8_t* pub, uint8_t* priv) {
    if(!pub || !priv) return false;
    furi_hal_random_init();
    furi_hal_random_fill_buf(priv, 32);
    clamp_curve25519(priv);
    return crypto_curve25519_public(priv, pub);
}

bool crypto_curve25519_shared(const uint8_t* priv, const uint8_t* pub, uint8_t* shared_out) {
    if(!priv || !pub || !shared_out) return false;
    uint8_t sk[32];
    memcpy(sk, priv, sizeof(sk));
    clamp_curve25519(sk);
    return curve25519_donna(shared_out, sk, pub) == 0;
}

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
    size_t* out_len) {
    (void)toNode;
    if(!remote_pub || !local_priv || !plain || !out || !out_len) return false;
    if(out_cap < plain_len + MESHTASTIC_PKI_OVERHEAD) return false;
    if(plain_len > 240) return false;
    uint8_t shared[32];
    if(!crypto_curve25519_shared(local_priv, remote_pub, shared)) return false;
    crypto_sha256(shared, sizeof(shared), shared);

    uint8_t nonce[16];
    uint32_t extra_nonce = furi_hal_random_get();
    crypto_init_nonce(nonce, fromNode, packetId, extra_nonce);

    uint8_t* auth = out + plain_len;
    if(aes_ccm_ae(shared, sizeof(shared), nonce, MESHTASTIC_PKI_TAG_LEN, plain, plain_len, NULL, 0, out, auth) != 0)
        return false;
    memcpy(auth + MESHTASTIC_PKI_TAG_LEN, &extra_nonce, sizeof(extra_nonce));
    *out_len = plain_len + MESHTASTIC_PKI_OVERHEAD;
    return true;
}

bool crypto_pki_decrypt(
    uint32_t fromNode,
    uint32_t packetId,
    const uint8_t* remote_pub,
    const uint8_t* local_priv,
    const uint8_t* crypt,
    size_t crypt_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!remote_pub || !local_priv || !crypt || !out || !out_len) return false;
    if(crypt_len <= MESHTASTIC_PKI_OVERHEAD) return false;
    size_t cipher_len = crypt_len - MESHTASTIC_PKI_OVERHEAD;
    if(out_cap < cipher_len) return false;
    uint32_t extra_nonce = 0;
    memcpy(&extra_nonce, crypt + cipher_len + MESHTASTIC_PKI_TAG_LEN, sizeof(extra_nonce));

    uint8_t shared[32];
    if(!crypto_curve25519_shared(local_priv, remote_pub, shared)) return false;
    crypto_sha256(shared, sizeof(shared), shared);

    uint8_t nonce[16];
    crypto_init_nonce(nonce, fromNode, packetId, extra_nonce);
    const uint8_t* auth = crypt + cipher_len;
    if(!aes_ccm_ad(shared, sizeof(shared), nonce, MESHTASTIC_PKI_TAG_LEN, crypt, cipher_len, NULL, 0, auth, out))
        return false;
    *out_len = cipher_len;
    return true;
}
