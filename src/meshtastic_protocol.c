#include "meshtastic.h"
#include "meshtastic_crypto.h"
#include "meshtastic_protocol.h"

typedef struct {
    uint32_t from;
    uint32_t id;
    uint8_t channel;
    const uint8_t* encrypted;
    size_t encrypted_len;
} MeshPacketView;

typedef struct {
    uint32_t portnum;
    const uint8_t* payload;
    size_t payload_len;
    uint32_t request_id;
} DataView;

static bool read_varint(const uint8_t* data, size_t len, size_t* offset, uint64_t* out) {
    uint64_t result = 0;
    uint8_t shift = 0;
    size_t i = *offset;
    while(i < len && shift <= 63) {
        uint8_t byte = data[i++];
        result |= (uint64_t)(byte & 0x7F) << shift;
        if((byte & 0x80) == 0) {
            *offset = i;
            *out = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

static bool read_fixed32(const uint8_t* data, size_t len, size_t* offset, uint32_t* out) {
    if(*offset + 4 > len) return false;
    uint32_t val = 0;
    val |= (uint32_t)data[*offset + 0];
    val |= (uint32_t)data[*offset + 1] << 8;
    val |= (uint32_t)data[*offset + 2] << 16;
    val |= (uint32_t)data[*offset + 3] << 24;
    *offset += 4;
    *out = val;
    return true;
}

static bool read_length_delimited(
    const uint8_t* data,
    size_t len,
    size_t* offset,
    const uint8_t** out,
    size_t* out_len) {
    uint64_t l = 0;
    if(!read_varint(data, len, offset, &l)) return false;
    if(*offset + l > len) return false;
    *out = data + *offset;
    *out_len = (size_t)l;
    *offset += (size_t)l;
    return true;
}

static bool parse_mesh_packet(const uint8_t* data, size_t len, MeshPacketView* out) {
    memset(out, 0, sizeof(*out));
    size_t off = 0;
    while(off < len) {
        uint64_t key = 0;
        if(!read_varint(data, len, &off, &key)) return false;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wire = (uint32_t)(key & 0x07);

        switch(field) {
        case 1: { // from (fixed32)
            if(wire != 5) return false;
            if(!read_fixed32(data, len, &off, &out->from)) return false;
            break;
        }
        case 3: { // channel (uint32)
            if(wire != 0) return false;
            uint64_t v = 0;
            if(!read_varint(data, len, &off, &v)) return false;
            out->channel = (uint8_t)v;
            break;
        }
        case 5: { // encrypted (bytes)
            if(wire != 2) return false;
            if(!read_length_delimited(data, len, &off, &out->encrypted, &out->encrypted_len))
                return false;
            break;
        }
        case 6: { // id (fixed32)
            if(wire != 5) return false;
            if(!read_fixed32(data, len, &off, &out->id)) return false;
            break;
        }
        default: {
            // Skip unknown field
            if(wire == 0) {
                uint64_t v = 0;
                if(!read_varint(data, len, &off, &v)) return false;
            } else if(wire == 2) {
                const uint8_t* tmp = NULL;
                size_t tmp_len = 0;
                if(!read_length_delimited(data, len, &off, &tmp, &tmp_len)) return false;
            } else if(wire == 5) {
                uint32_t v = 0;
                if(!read_fixed32(data, len, &off, &v)) return false;
            } else if(wire == 1) {
                if(off + 8 > len) return false;
                off += 8;
            } else {
                return false;
            }
            break;
        }
        }
    }
    return true;
}

static bool parse_data_message(const uint8_t* data, size_t len, DataView* out) {
    memset(out, 0, sizeof(*out));
    size_t off = 0;
    bool got_port = false;
    bool got_payload = false;
    while(off < len) {
        uint64_t key = 0;
        if(!read_varint(data, len, &off, &key)) return got_payload;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wire = (uint32_t)(key & 0x07);

        switch(field) {
        case 1: { // portnum (varint)
            if(wire != 0) return got_payload;
            uint64_t v = 0;
            if(!read_varint(data, len, &off, &v)) return got_payload;
            out->portnum = (uint32_t)v;
            got_port = true;
            break;
        }
        case 2: { // payload (bytes)
            if(wire != 2) return got_payload;
            if(!read_length_delimited(data, len, &off, &out->payload, &out->payload_len)) {
                return got_payload;
            }
            got_payload = true;
            break;
        }
        case 6: { // request_id (varint)
            if(wire != 0) return got_payload;
            uint64_t v = 0;
            if(!read_varint(data, len, &off, &v)) return got_payload;
            out->request_id = (uint32_t)v;
            break;
        }
        default: {
            if(wire == 0) {
                uint64_t v = 0;
                if(!read_varint(data, len, &off, &v)) return got_payload;
            } else if(wire == 2) {
                const uint8_t* tmp = NULL;
                size_t tmp_len = 0;
                if(!read_length_delimited(data, len, &off, &tmp, &tmp_len)) return got_payload;
            } else if(wire == 5) {
                uint32_t v = 0;
                if(!read_fixed32(data, len, &off, &v)) return got_payload;
            } else if(wire == 1) {
                if(off + 8 > len) return got_payload;
                off += 8;
            } else {
                return got_payload;
            }
            break;
        }
        }
    }
    return got_payload || got_port;
}

static bool parse_routing_error(const uint8_t* data, size_t len, uint32_t* out_err) {
    if(!data || !out_err) return false;
    size_t off = 0;
    bool found = false;
    uint32_t err = 0;
    while(off < len) {
        uint64_t key = 0;
        if(!read_varint(data, len, &off, &key)) return false;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wire = (uint32_t)(key & 0x07);
        if(field == 3 && wire == 0) {
            uint64_t v = 0;
            if(!read_varint(data, len, &off, &v)) return false;
            err = (uint32_t)v;
            found = true;
        } else {
            if(wire == 0) {
                uint64_t v = 0;
                if(!read_varint(data, len, &off, &v)) return false;
            } else if(wire == 2) {
                const uint8_t* tmp = NULL;
                size_t tmp_len = 0;
                if(!read_length_delimited(data, len, &off, &tmp, &tmp_len)) return false;
            } else if(wire == 5) {
                uint32_t v = 0;
                if(!read_fixed32(data, len, &off, &v)) return false;
            } else if(wire == 1) {
                if(off + 8 > len) return false;
                off += 8;
            } else {
                return false;
            }
        }
    }
    if(found) *out_err = err;
    return found;
}

static void parse_user_message(
    const uint8_t* data,
    size_t len,
    char* out_id,
    size_t id_cap,
    char* out_long,
    size_t long_cap,
    char* out_short,
    size_t short_cap,
    uint8_t* out_role,
    uint8_t* out_pub,
    size_t pub_cap,
    size_t* out_pub_len) {
    if(!data) return;
    if(out_id && id_cap) out_id[0] = '\0';
    if(out_long && long_cap) out_long[0] = '\0';
    if(out_short && short_cap) out_short[0] = '\0';
    if(out_role) *out_role = 0;
    if(out_pub && pub_cap) memset(out_pub, 0, pub_cap);
    if(out_pub_len) *out_pub_len = 0;
    size_t off = 0;
    while(off < len) {
        uint64_t key = 0;
        if(!read_varint(data, len, &off, &key)) return;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wire = (uint32_t)(key & 0x07);
        if(field == 1 && wire == 2) { // id
            const uint8_t* s = NULL;
            size_t s_len = 0;
            if(!read_length_delimited(data, len, &off, &s, &s_len)) return;
            if(out_id && id_cap) {
                char tmp[64];
                size_t copy = s_len < (sizeof(tmp) - 1) ? s_len : (sizeof(tmp) - 1);
                memcpy(tmp, s, copy);
                tmp[copy] = '\0';
                meshtastic_sanitize_text(out_id, id_cap, tmp);
            }
        } else if(field == 2 && wire == 2) { // long_name
            const uint8_t* s = NULL;
            size_t s_len = 0;
            if(!read_length_delimited(data, len, &off, &s, &s_len)) return;
            if(out_long && long_cap) {
                char tmp[96];
                size_t copy = s_len < (sizeof(tmp) - 1) ? s_len : (sizeof(tmp) - 1);
                memcpy(tmp, s, copy);
                tmp[copy] = '\0';
                meshtastic_sanitize_text(out_long, long_cap, tmp);
            }
        } else if(field == 3 && wire == 2) { // short_name
            const uint8_t* s = NULL;
            size_t s_len = 0;
            if(!read_length_delimited(data, len, &off, &s, &s_len)) return;
            if(out_short && short_cap) {
                char tmp[32];
                size_t copy = s_len < (sizeof(tmp) - 1) ? s_len : (sizeof(tmp) - 1);
                memcpy(tmp, s, copy);
                tmp[copy] = '\0';
                meshtastic_sanitize_text(out_short, short_cap, tmp);
            }
        } else if(field == 7 && wire == 0) { // role
            uint64_t v = 0;
            if(!read_varint(data, len, &off, &v)) return;
            if(out_role) *out_role = (uint8_t)v;
        } else if(field == 8 && wire == 2) { // public_key
            const uint8_t* s = NULL;
            size_t s_len = 0;
            if(!read_length_delimited(data, len, &off, &s, &s_len)) return;
            if(out_pub && pub_cap >= s_len) {
                memcpy(out_pub, s, s_len);
                if(out_pub_len) *out_pub_len = s_len;
            }
        } else {
            if(wire == 0) {
                uint64_t v = 0;
                if(!read_varint(data, len, &off, &v)) return;
            } else if(wire == 2) {
                const uint8_t* tmp = NULL;
                size_t tmp_len = 0;
                if(!read_length_delimited(data, len, &off, &tmp, &tmp_len)) return;
            } else if(wire == 5) {
                uint32_t v = 0;
                if(!read_fixed32(data, len, &off, &v)) return;
            } else if(wire == 1) {
                if(off + 8 > len) return;
                off += 8;
            } else {
                return;
            }
        }
    }
}

static bool parse_nodeinfo_message(
    const uint8_t* data,
    size_t len,
    const uint8_t** out_user,
    size_t* out_user_len) {
    if(!data || !out_user || !out_user_len) return false;
    *out_user = NULL;
    *out_user_len = 0;
    size_t off = 0;
    while(off < len) {
        uint64_t key = 0;
        if(!read_varint(data, len, &off, &key)) return false;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wire = (uint32_t)(key & 0x07);
        if((field == 1 || field == 2) && wire == 2) {
            if(!read_length_delimited(data, len, &off, out_user, out_user_len)) return false;
            return true;
        }
        if(wire == 0) {
            uint64_t v = 0;
            if(!read_varint(data, len, &off, &v)) return false;
        } else if(wire == 2) {
            const uint8_t* tmp = NULL;
            size_t tmp_len = 0;
            if(!read_length_delimited(data, len, &off, &tmp, &tmp_len)) return false;
        } else if(wire == 5) {
            uint32_t v = 0;
            if(!read_fixed32(data, len, &off, &v)) return false;
        } else if(wire == 1) {
            if(off + 8 > len) return false;
            off += 8;
        } else {
            return false;
        }
    }
    return false;
}

static void log_hex(const char* tag, const uint8_t* data, size_t len) {
    char hex[128] = {0};
    size_t max = len > 32 ? 32 : len;
    size_t pos = 0;
    for(size_t i = 0; i < max; i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[i]);
    }
    FURI_LOG_I(tag, "HEX[%u]: %s", (unsigned)max, hex);
}

static bool role_can_relay(const MeshtasticApp* app) {
    if(!app) return false;
    return app->role_index == RoleClient;
}

static void maybe_relay_packet(
    MeshtasticApp* app,
    const uint8_t* data,
    size_t len,
    uint32_t hdr_to,
    uint32_t hdr_from,
    uint32_t hdr_id,
    uint8_t hdr_flags) {
    if(!app) return;
    if(!role_can_relay(app)) return;
    if(hdr_from == app->self_node_id) return;
    if(hdr_to == app->self_node_id) return;
    uint8_t hop_limit = hdr_flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    if(hop_limit == 0) return;
    if(meshtastic_relay_seen(app, hdr_from, hdr_id)) return;
    meshtastic_relay_mark(app, hdr_from, hdr_id);

    if(len < 16 || len > 255) return;
    uint8_t buffer[256];
    memcpy(buffer, data, len);
    uint8_t new_hop = (uint8_t)(hop_limit - 1);
    uint8_t flags = buffer[12];
    flags &= (uint8_t)(~PACKET_FLAGS_HOP_LIMIT_MASK);
    flags |= (new_hop & PACKET_FLAGS_HOP_LIMIT_MASK);
    buffer[12] = flags;
    buffer[14] = 0x00; // next_hop (no preference)
    buffer[15] = (uint8_t)(app->self_node_id & 0xFF); // relay_node

    rfm95w_send_packet(app, buffer, len);
}

void process_received_packet(MeshtasticApp* app, uint8_t* data, size_t len) {
    FURI_LOG_I("Proto", "Packet received: %u bytes", (unsigned)len);
    log_hex("Proto", data, len);

    // Meshtastic on-air packets include a 16-byte header before the protobuf payload.
    uint32_t hdr_to = 0;
    uint32_t hdr_from = 0;
    uint32_t hdr_id = 0;
    uint8_t hdr_flags = 0;
    uint8_t hdr_chash = 0;
    uint16_t hdr_pad = 0;
    const uint8_t* payload = data;
    size_t payload_len = len;
    if(len >= 16) {
        hdr_to = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                 ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        hdr_from = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                   ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
        hdr_id = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
                 ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
        hdr_flags = data[12];
        hdr_chash = data[13];
        hdr_pad = (uint16_t)data[14] | ((uint16_t)data[15] << 8);

        payload = data + 16;
        payload_len = len - 16;

        FURI_LOG_I(
            "Proto",
            "Header to=0x%08lX from=0x%08lX id=0x%08lX flags=0x%02X chash=0x%02X pad=0x%04X",
            (unsigned long)hdr_to,
            (unsigned long)hdr_from,
            (unsigned long)hdr_id,
            hdr_flags,
            hdr_chash,
            hdr_pad);

        maybe_relay_packet(app, data, len, hdr_to, hdr_from, hdr_id, hdr_flags);

        bool want_ack = (hdr_flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;

        uint8_t hop_limit = hdr_flags & PACKET_FLAGS_HOP_LIMIT_MASK;
        uint8_t hop_start = (hdr_flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
        uint8_t hops_used = 0xFF;
        if(hop_start >= hop_limit) {
            hops_used = (uint8_t)(hop_start - hop_limit);
        }

        if(payload_len > 0) {
            if(app && hdr_chash != app->channel_hash) {
                bool is_pki_dm = (hdr_chash == 0x00 && hdr_to == app->self_node_id &&
                                  hdr_to != NODENUM_BROADCAST);
                if(!is_pki_dm) {
                    FURI_LOG_W(
                        "Proto",
                        "Dropping packet with channel hash 0x%02X (expected 0x%02X)",
                        hdr_chash,
                        app->channel_hash);
                    return;
                }
            }
            uint8_t decrypted[256];
            size_t decrypted_len = 0;
            bool decrypted_ok = false;

            if(app && hdr_chash == 0x00 && hdr_to == app->self_node_id && hdr_to != NODENUM_BROADCAST) {
                uint8_t remote_pub[32];
                if(!app->self_keys_valid) {
                    FURI_LOG_W("Proto", "Missing local PKI keys");
                    return;
                }
                if(meshtastic_get_node_public_key(app, hdr_from, remote_pub, sizeof(remote_pub))) {
                    decrypted_ok = crypto_pki_decrypt(
                        hdr_from,
                        hdr_id,
                        remote_pub,
                        app->self_private_key,
                        payload,
                        payload_len,
                        decrypted,
                        sizeof(decrypted),
                        &decrypted_len);
                    if(!decrypted_ok) {
                        uint8_t reversed[32];
                        memcpy(reversed, remote_pub, sizeof(reversed));
                        for(size_t i = 0; i < 16; i++) {
                            uint8_t tmp = reversed[i];
                            reversed[i] = reversed[31 - i];
                            reversed[31 - i] = tmp;
                        }
                        if(crypto_pki_decrypt(
                               hdr_from,
                               hdr_id,
                               reversed,
                               app->self_private_key,
                               payload,
                               payload_len,
                               decrypted,
                               sizeof(decrypted),
                               &decrypted_len)) {
                            decrypted_ok = true;
                            meshtastic_set_node_pubkey_mode(app, hdr_from, 1);
                            FURI_LOG_W("Proto", "PKI decrypt used reversed public key for 0x%08lX", (unsigned long)hdr_from);
                        }
                    }
                    if(!decrypted_ok) {
                        uint8_t swapped[32];
                        memcpy(swapped, remote_pub, sizeof(swapped));
                        for(size_t i = 0; i < 32; i += 4) {
                            uint8_t b0 = swapped[i + 0];
                            uint8_t b1 = swapped[i + 1];
                            uint8_t b2 = swapped[i + 2];
                            uint8_t b3 = swapped[i + 3];
                            swapped[i + 0] = b3;
                            swapped[i + 1] = b2;
                            swapped[i + 2] = b1;
                            swapped[i + 3] = b0;
                        }
                        if(crypto_pki_decrypt(
                               hdr_from,
                               hdr_id,
                               swapped,
                               app->self_private_key,
                               payload,
                               payload_len,
                               decrypted,
                               sizeof(decrypted),
                               &decrypted_len)) {
                            decrypted_ok = true;
                            meshtastic_set_node_pubkey_mode(app, hdr_from, 2);
                            FURI_LOG_W("Proto", "PKI decrypt used word-swapped public key for 0x%08lX", (unsigned long)hdr_from);
                        }
                    }
                    if(!decrypted_ok) {
                        uint8_t wordrev[32];
                        memcpy(wordrev, remote_pub, sizeof(wordrev));
                        for(size_t i = 0; i < 4; i++) {
                            size_t a = i * 4;
                            size_t b = (7 - i) * 4;
                            uint8_t t0 = wordrev[a + 0];
                            uint8_t t1 = wordrev[a + 1];
                            uint8_t t2 = wordrev[a + 2];
                            uint8_t t3 = wordrev[a + 3];
                            wordrev[a + 0] = wordrev[b + 0];
                            wordrev[a + 1] = wordrev[b + 1];
                            wordrev[a + 2] = wordrev[b + 2];
                            wordrev[a + 3] = wordrev[b + 3];
                            wordrev[b + 0] = t0;
                            wordrev[b + 1] = t1;
                            wordrev[b + 2] = t2;
                            wordrev[b + 3] = t3;
                        }
                        if(crypto_pki_decrypt(
                               hdr_from,
                               hdr_id,
                               wordrev,
                               app->self_private_key,
                               payload,
                               payload_len,
                               decrypted,
                               sizeof(decrypted),
                               &decrypted_len)) {
                            decrypted_ok = true;
                            meshtastic_set_node_pubkey_mode(app, hdr_from, 3);
                            FURI_LOG_W("Proto", "PKI decrypt used word-reversed public key for 0x%08lX", (unsigned long)hdr_from);
                        }
                    }
                }
                if(!decrypted_ok) {
                    FURI_LOG_W("Proto", "PKI decrypt failed for DM (len=%u)", (unsigned)payload_len);
                    log_hex("Proto", payload, payload_len);
                    if(app) {
                        static uint32_t last_key_log_node = 0;
                        if(last_key_log_node != hdr_from) {
                            last_key_log_node = hdr_from;
                            FURI_LOG_W("Proto", "PKI local pub/priv and remote pub for 0x%08lX", (unsigned long)hdr_from);
                            log_hex("Proto", app->self_public_key, sizeof(app->self_public_key));
                            uint8_t remote_pub[32];
                            if(meshtastic_get_node_public_key(app, hdr_from, remote_pub, sizeof(remote_pub))) {
                                log_hex("Proto", remote_pub, sizeof(remote_pub));
                            }
                        }
                    }
                    if(app) {
                        uint32_t now = furi_get_tick();
                        if(app->last_nodeinfo_request_ms == 0 ||
                           now - app->last_nodeinfo_request_ms >= 5000) {
                            if(meshtastic_send_nodeinfo(app, hdr_from, true)) {
                                FURI_LOG_I("Proto", "Sent NodeInfo request to 0x%08lX", (unsigned long)hdr_from);
                            }
                            app->last_nodeinfo_request_ms = now;
                        }
                    }
                    return;
                }
            } else {
                uint8_t psk_bytes[32] = {0};
                size_t psk_len = 0;
                if(app && app->channel_psk_len > 0) {
                    memcpy(psk_bytes, app->channel_psk, app->channel_psk_len);
                    psk_len = app->channel_psk_len;
                } else {
                    psk_bytes[0] = 0x01;
                    psk_len = 1;
                }
                CryptoKey key = crypto_expand_psk(psk_bytes, psk_len);
                if(key.length > 0 && payload_len <= sizeof(decrypted)) {
                    memcpy(decrypted, payload, payload_len);
                    if(app && hdr_chash != app->channel_hash) {
                        FURI_LOG_I(
                            "Proto",
                            "Non-default channel hash 0x%02X (expected 0x%02X); decrypt may fail",
                            hdr_chash,
                            app->channel_hash);
                    }
                    crypto_decrypt_packet(key, hdr_from, hdr_id, payload_len, decrypted);
                    decrypted_len = payload_len;
                    decrypted_ok = true;
                }
            }

            if(decrypted_ok) {
                log_hex("Proto", decrypted, decrypted_len);
                DataView data_msg;
                if(parse_data_message(decrypted, decrypted_len, &data_msg) &&
                   data_msg.payload != NULL && data_msg.payload_len > 0) {
                    if(app) {
                        meshtastic_touch_node(app, hdr_from, hops_used);
                    }
                    if(data_msg.portnum == PORTNUM_ROUTING_APP) {
                        uint32_t err = 0;
                        if(parse_routing_error(data_msg.payload, data_msg.payload_len, &err)) {
                            if(app && data_msg.request_id != 0) {
                                meshtastic_mark_ack(app, data_msg.request_id, err == 0, hdr_from);
                            }
                            if(app && hdr_from != 0) {
                                if(err == 6 || err == 34 || err == 35 || err == 39) {
                                    // NO_CHANNEL / PKI_FAILED / PKI_UNKNOWN_PUBKEY / PKI_SEND_FAIL_PUBLIC_KEY
                                    if(meshtastic_send_nodeinfo(app, hdr_from, true)) {
                                        FURI_LOG_I(
                                            "Proto",
                                            "Sent NodeInfo to 0x%08lX after routing error %lu",
                                            (unsigned long)hdr_from,
                                            (unsigned long)err);
                                    }
                                }
                            }
                        }
                        return;
                    } else if(data_msg.portnum == PORTNUM_NODEINFO_APP) {
                        if(app) {
                            char uid[16];
                            char long_name[40];
                            char short_name[5];
                            uint8_t role = 0;
                            uint8_t pub_key[32];
                            size_t pub_len = 0;
                            const uint8_t* user = NULL;
                            size_t user_len = 0;
                            FURI_LOG_I(
                                "Proto",
                                "NodeInfo payload_len=%u",
                                (unsigned)data_msg.payload_len);
                            log_hex("Proto", data_msg.payload, data_msg.payload_len);
                            // First, try parsing payload as a User message directly.
                            parse_user_message(
                                data_msg.payload,
                                data_msg.payload_len,
                                uid,
                                sizeof(uid),
                                long_name,
                                sizeof(long_name),
                                short_name,
                                sizeof(short_name),
                                &role,
                                pub_key,
                                sizeof(pub_key),
                                &pub_len);
                            if(uid[0] == '\0' && long_name[0] == '\0' &&
                               short_name[0] == '\0' && pub_len == 0) {
                                if(!parse_nodeinfo_message(
                                       data_msg.payload,
                                       data_msg.payload_len,
                                       &user,
                                       &user_len)) {
                                    user = data_msg.payload;
                                    user_len = data_msg.payload_len;
                                }
                                FURI_LOG_I(
                                    "Proto",
                                    "NodeInfo inner user_len=%u",
                                    (unsigned)user_len);
                                log_hex("Proto", user, user_len);
                                parse_user_message(
                                    user,
                                    user_len,
                                    uid,
                                    sizeof(uid),
                                    long_name,
                                    sizeof(long_name),
                                    short_name,
                                    sizeof(short_name),
                                    &role,
                                    pub_key,
                                    sizeof(pub_key),
                                    &pub_len);
                            }
                            FURI_LOG_I(
                                "Proto",
                                "NodeInfo user=%s short=%s pub=%u",
                                uid,
                                short_name,
                                (unsigned)pub_len);
                            meshtastic_update_node_info(
                                app,
                                hdr_from,
                                uid,
                                long_name,
                                short_name,
                                role,
                                (pub_len == sizeof(pub_key)) ? pub_key : NULL,
                                pub_len);
                        }
                        return;
                    } else if(data_msg.portnum == PORTNUM_POSITION_APP) {
                        if(app && hdr_from != app->self_node_id && hops_used == 0 &&
                           hdr_to == NODENUM_BROADCAST) {
                            if(data_msg.payload_len > 0 &&
                               data_msg.payload_len <= sizeof(app->pending_pos_payload)) {
                                if(!(app->pending_pos_from == hdr_from &&
                                     app->pending_pos_id == hdr_id)) {
                                    memcpy(app->pending_pos_payload,
                                           data_msg.payload,
                                           data_msg.payload_len);
                                    app->pending_pos_len = (uint8_t)data_msg.payload_len;
                                    app->pending_pos_from = hdr_from;
                                    app->pending_pos_id = hdr_id;
                                    app->pending_pos = true;
                                    FURI_LOG_I(
                                        "Proto",
                                        "Queued position relay from 0x%08lX",
                                        (unsigned long)hdr_from);
                                }
                            }
                        }
                    } else if(data_msg.portnum == PORTNUM_TEXT_MESSAGE_APP) {
                        char text[128];
                        size_t copy_len = data_msg.payload_len;
                        if(copy_len >= sizeof(text)) copy_len = sizeof(text) - 1;
                        memcpy(text, data_msg.payload, copy_len);
                        text[copy_len] = '\0';
                        FURI_LOG_I("Proto", "Text: %s", text);
                        if(app) {
                            if(hdr_from == app->self_node_id) {
                                if(hdr_to == NODENUM_BROADCAST) {
                                    meshtastic_mark_ack(app, hdr_id, true, hdr_from);
                                }
                            } else {
                                if(hdr_to == app->self_node_id && hdr_chash != 0x00) {
                                    FURI_LOG_I("Proto", "Accepting legacy DM");
                                }
                                if(hdr_to != NODENUM_BROADCAST && hdr_to != app->self_node_id) {
                                    return;
                                }
                                bool is_dm = (hdr_to != NODENUM_BROADCAST && hdr_to == app->self_node_id);
                                meshtastic_touch_node(app, hdr_from, hops_used);
                                meshtastic_store_message(
                                    app,
                                    hdr_from,
                                    hdr_to,
                                    hdr_id,
                                    text,
                                    false,
                                    is_dm);
                                if(want_ack && hdr_to == app->self_node_id) {
                                    meshtastic_send_ack(app, hdr_from, hdr_id);
                                }
                            }
                        }
                        return;
                    } else {
                        FURI_LOG_I(
                            "Proto",
                            "Data port=%lu payload_len=%u",
                            (unsigned long)data_msg.portnum,
                            (unsigned)data_msg.payload_len);
                        return;
                    }
                } else {
                    FURI_LOG_W("Proto", "Failed to parse Data payload");
                }
            }
        }
    }

    MeshPacketView pkt;
    if(!parse_mesh_packet(payload, payload_len, &pkt)) {
        // Fallback: try parsing without skipping header for compatibility.
        if(!parse_mesh_packet(data, len, &pkt)) {
            FURI_LOG_W("Proto", "Failed to parse MeshPacket");
            return;
        }
    }

    if(pkt.encrypted == NULL || pkt.encrypted_len == 0) {
        FURI_LOG_W("Proto", "No encrypted payload in MeshPacket");
        return;
    }

    // Meshtastic default PSK index 1: AQ== -> 0x01
    uint8_t psk_bytes[32] = {0};
    size_t psk_len = 0;
    if(app && app->channel_psk_len > 0) {
        memcpy(psk_bytes, app->channel_psk, app->channel_psk_len);
        psk_len = app->channel_psk_len;
    } else {
        psk_bytes[0] = 0x01;
        psk_len = 1;
    }
    CryptoKey key = crypto_expand_psk(psk_bytes, psk_len);
    if(key.length <= 0) {
        FURI_LOG_W("Proto", "Invalid crypto key");
        return;
    }

    // Decrypt into a local buffer to avoid modifying the raw packet buffer.
    uint8_t decrypted[256];
    if(pkt.encrypted_len > sizeof(decrypted)) {
        FURI_LOG_W("Proto", "Encrypted payload too large");
        return;
    }
    memcpy(decrypted, pkt.encrypted, pkt.encrypted_len);

    crypto_decrypt_packet(
        key, pkt.from, pkt.id, pkt.encrypted_len, decrypted);

    FURI_LOG_I(
        "Proto",
        "Decrypted payload len=%u from=0x%08lX id=0x%08lX ch=%u",
        (unsigned)pkt.encrypted_len,
        (unsigned long)pkt.from,
        (unsigned long)pkt.id,
        pkt.channel);
    log_hex("Proto", decrypted, pkt.encrypted_len);

    DataView data_msg;
    if(parse_data_message(decrypted, pkt.encrypted_len, &data_msg) &&
       data_msg.payload != NULL && data_msg.payload_len > 0) {
        if(app) {
            meshtastic_touch_node(app, pkt.from, 0xFF);
        }
        if(data_msg.portnum == PORTNUM_ROUTING_APP) {
            uint32_t err = 0;
            if(parse_routing_error(data_msg.payload, data_msg.payload_len, &err)) {
                if(app && data_msg.request_id != 0) {
                    meshtastic_mark_ack(app, data_msg.request_id, err == 0, pkt.from);
                }
            }
        } else if(data_msg.portnum == PORTNUM_NODEINFO_APP) {
            if(app) {
                char uid[16];
                char long_name[40];
                char short_name[5];
                uint8_t role = 0;
                uint8_t pub_key[32];
                size_t pub_len = 0;
                const uint8_t* user = NULL;
                size_t user_len = 0;
                FURI_LOG_I(
                    "Proto",
                    "NodeInfo payload_len=%u",
                    (unsigned)data_msg.payload_len);
                log_hex("Proto", data_msg.payload, data_msg.payload_len);
                parse_user_message(
                    data_msg.payload,
                    data_msg.payload_len,
                    uid,
                    sizeof(uid),
                    long_name,
                    sizeof(long_name),
                    short_name,
                    sizeof(short_name),
                    &role,
                    pub_key,
                    sizeof(pub_key),
                    &pub_len);
                if(uid[0] == '\0' && long_name[0] == '\0' &&
                   short_name[0] == '\0' && pub_len == 0) {
                    if(!parse_nodeinfo_message(
                           data_msg.payload,
                           data_msg.payload_len,
                           &user,
                           &user_len)) {
                        user = data_msg.payload;
                        user_len = data_msg.payload_len;
                    }
                    FURI_LOG_I(
                        "Proto",
                        "NodeInfo inner user_len=%u",
                        (unsigned)user_len);
                    log_hex("Proto", user, user_len);
                    parse_user_message(
                        user,
                        user_len,
                        uid,
                        sizeof(uid),
                        long_name,
                        sizeof(long_name),
                        short_name,
                        sizeof(short_name),
                        &role,
                        pub_key,
                        sizeof(pub_key),
                        &pub_len);
                }
                FURI_LOG_I(
                    "Proto",
                    "NodeInfo user=%s short=%s pub=%u",
                    uid,
                    short_name,
                    (unsigned)pub_len);
                meshtastic_update_node_info(
                    app,
                    pkt.from,
                    uid,
                    long_name,
                    short_name,
                    role,
                    (pub_len == sizeof(pub_key)) ? pub_key : NULL,
                    pub_len);
            }
        } else if(data_msg.portnum == PORTNUM_TEXT_MESSAGE_APP) {
            char text[128];
            size_t copy_len = data_msg.payload_len;
            if(copy_len >= sizeof(text)) copy_len = sizeof(text) - 1;
            memcpy(text, data_msg.payload, copy_len);
            text[copy_len] = '\0';
            FURI_LOG_I("Proto", "Text: %s", text);
            if(app) {
                if(pkt.from != app->self_node_id) {
                    meshtastic_touch_node(app, pkt.from, 0xFF);
                    meshtastic_store_message(
                        app,
                        pkt.from,
                        NODENUM_BROADCAST,
                        pkt.id,
                        text,
                        false,
                        false);
                }
            }
        } else {
            FURI_LOG_I(
                "Proto",
                "Data port=%lu payload_len=%u",
                (unsigned long)data_msg.portnum,
                (unsigned)data_msg.payload_len);
        }
    } else {
        FURI_LOG_W("Proto", "Failed to parse Data message");
    }
}
