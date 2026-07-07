#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============ CONSTANTS ============
#define NODENUM_BROADCAST 0xFFFFFFFF
#define MAX_PAYLOAD_SIZE 237
#define MAX_HOP_LIMIT 7
#define DEFAULT_HOP_LIMIT 3

// ============ PORT NUMBERS ============
typedef enum {
    PORTNUM_UNKNOWN_APP = 0,
    PORTNUM_TEXT_MESSAGE_APP = 1,
    PORTNUM_REMOTE_HARDWARE_APP = 2,
    PORTNUM_POSITION_APP = 3,
    PORTNUM_NODEINFO_APP = 4,
    PORTNUM_ROUTING_APP = 5,
    PORTNUM_ADMIN_APP = 6,
    PORTNUM_TEXT_MESSAGE_COMPRESSED_APP = 7,
    PORTNUM_WAYPOINT_APP = 8,
    PORTNUM_AUDIO_APP = 9,
    PORTNUM_DETECTION_SENSOR_APP = 10,
    PORTNUM_ALERT_APP = 11,
    PORTNUM_TELEMETRY_APP = 67,
    PORTNUM_PRIVATE_APP = 256,
} Meshtastic_PortNum;

// ============ PACKET HEADER ============
typedef struct __attribute__((packed)) {
    uint32_t from;
    uint32_t to;
    uint32_t id;
    uint8_t channel;
    uint8_t hop_limit;
    uint8_t hop_start;
    uint8_t flags;
    uint8_t payload_len;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} Meshtastic_Packet;

// ============ NODE INFO ============
typedef struct __attribute__((packed)) {
    char id[16];
    char long_name[40];
    char short_name[5];
    uint16_t hw_model;
    uint8_t role;
    uint32_t uptime;
    int8_t battery_level;
} Meshtastic_NodeInfo;

// ============ CRYPTO KEY ============
typedef struct {
    uint8_t bytes[32];
    uint8_t length;
} Meshtastic_CryptoKey;

// ============ FUNCTIONS ============
uint32_t meshtastic_generate_packet_id(void);
Meshtastic_CryptoKey meshtastic_expand_psk(const uint8_t* psk, uint8_t psk_len);
void meshtastic_encrypt_packet(Meshtastic_CryptoKey key, uint32_t from_node,
                               uint32_t packet_id, uint8_t* data, size_t len);
void meshtastic_decrypt_packet(Meshtastic_CryptoKey key, uint32_t from_node,
                               uint32_t packet_id, uint8_t* data, size_t len);
void meshtastic_init_nonce(uint8_t* nonce, uint32_t from_node, uint32_t packet_id);
