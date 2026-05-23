#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/modules/popup.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <stdbool.h>
#include <storage/storage.h>

typedef struct ViewDispatcher ViewDispatcher;
typedef struct Submenu Submenu;
typedef struct TextInput TextInput;
typedef struct View View;
typedef struct VariableItemList VariableItemList;
typedef struct VariableItem VariableItem;
typedef struct Popup Popup;

// ============ HARDWARE PINS (hardware SPI external handle) ============
// External bus fixed pins: MOSI=A7(2), MISO=A6(3), SCK=B3(5), CS=A4(4)
#define PIN_MOSI &gpio_ext_pa7   // Pin 2
#define PIN_MISO &gpio_ext_pa6   // Pin 3
#define PIN_CS &gpio_ext_pa4     // Pin 4 (NSS/CS, software controlled)
#define PIN_SCK &gpio_ext_pb3    // Pin 5
#define PIN_DIO0 &gpio_ext_pb2   // Pin 6 (G0 / DIO0)
#define PIN_EN &gpio_ext_pc3     // Pin 7 (EN)

// ============ RFM95W REGISTERS (SX1276) ============
#define REG_FIFO 0x00
#define REG_OPMODE 0x01
#define REG_FR_MSB 0x06
#define REG_FR_MID 0x07
#define REG_FR_LSB 0x08
#define REG_PA_CONFIG 0x09
#define REG_OCP 0x0B
#define REG_LNA 0x0C
#define REG_FIFO_ADDR_PTR 0x0D
#define REG_FIFO_TX_BASE_ADDR 0x0E
#define REG_FIFO_RX_BASE_ADDR 0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS_MASK 0x11
#define REG_IRQ_FLAGS 0x12
#define REG_RX_NB_BYTES 0x13
#define REG_PKT_SNR_VALUE 0x19
#define REG_PKT_RSSI_VALUE 0x1A
#define REG_RSSI_VALUE 0x1B
#define REG_MODEM_CONFIG_1 0x1D
#define REG_MODEM_CONFIG_2 0x1E
#define REG_PREAMBLE_MSB 0x20
#define REG_PREAMBLE_LSB 0x21
#define REG_PAYLOAD_LENGTH 0x22
#define REG_MAX_PAYLOAD_LENGTH 0x23
#define REG_MODEM_CONFIG_3 0x26
#define REG_SYNC_WORD 0x39
#define REG_DIO_MAPPING_1 0x40
#define REG_VERSION 0x42
#define REG_PA_DAC 0x4D

// ============ MODES ============
#define MODE_LORA 0x80
#define MODE_SLEEP 0x00
#define MODE_STDBY 0x01
#define MODE_TX 0x03
#define MODE_RXCONTINUOUS 0x05

// ============ IRQ FLAGS ============
#define IRQ_RX_TIMEOUT 0x80
#define IRQ_RX_DONE 0x40
#define IRQ_PAYLOAD_CRC_ERROR 0x20
#define IRQ_VALID_HEADER 0x10
#define IRQ_TX_DONE 0x08

// ============ PACKET HEADER FLAGS ============
#define PACKET_FLAGS_HOP_LIMIT_MASK 0x07
#define PACKET_FLAGS_WANT_ACK_MASK 0x08
#define PACKET_FLAGS_VIA_MQTT_MASK 0x10
#define PACKET_FLAGS_HOP_START_MASK 0xE0
#define PACKET_FLAGS_HOP_START_SHIFT 5

// ============ APP STATES ============
typedef enum {
    AppStateInit,
    AppStateIdle,
    AppStateSending,
    AppStateReceiving,
    AppStateError
} AppState;

// ============ NODE ROLES ============
typedef enum {
    RoleClient,
    RoleClientMute,
    RoleCount,
} MeshtasticRole;

// ============ MESSAGE STORAGE ============
typedef struct {
    uint32_t from;
    uint32_t to;
    uint32_t id;
    uint8_t payload[256];
    uint8_t len;
    int8_t rssi;
    int8_t snr;
    uint32_t timestamp;
    bool outgoing;
    bool unread;
    bool is_dm;
    uint8_t ack_state;
    uint8_t retries;
    uint32_t last_tx_ms;
} Message;

#define MAX_MESSAGES 20
#define MAX_NODES 20

typedef struct {
    uint32_t node_id;
    char user_id[16];
    char long_name[40];
    char short_name[5];
    uint8_t role_index;
    uint32_t last_heard;
    uint8_t last_hops;
    int8_t last_rssi;
    int8_t last_snr;
    uint8_t public_key[32];
    bool has_public_key;
    uint8_t pubkey_mode;
} NodeInfo;

// ============ MAIN APP CONTEXT ============
typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    NotificationApp* notifications;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Submenu* nodes_menu;
    Submenu* contacts_menu;
    TextInput* text_input;
    View* inbox_view;
    View* dm_inbox_view;
    View* node_detail_view;
    View* error_view;
    Popup* popup;
    VariableItemList* settings_list;
    VariableItem* settings_max_hops;
    VariableItem* settings_freq_slot;
    VariableItem* settings_preset;
    VariableItem* settings_tx_power;
    VariableItem* settings_role;
    VariableItem* settings_nodeinfo_interval;
    VariableItem* settings_region;
    VariableItem* settings_short_name;
    VariableItem* settings_long_name;
    VariableItem* settings_channel_name;
    VariableItem* settings_clear;
    VariableItem* settings_clear_nodes;
    VariableItem* settings_reset_keys;
    VariableItem* settings_channel_preset;
    VariableItem* settings_channel_custom;
    VariableItem* settings_reset;

    AppState state;
    bool running;
    bool packet_available;

    Message messages[MAX_MESSAGES];
    uint8_t msg_count;
    Message dm_messages[MAX_MESSAGES];
    uint8_t dm_count;
    uint8_t dm_inbox_scroll;

    NodeInfo nodes[MAX_NODES];
    uint8_t node_count;
    uint8_t selected_node_index;

    uint32_t packets_sent;
    uint32_t packets_received;
    int8_t last_rssi;
    int8_t last_snr;
    uint32_t self_node_id;
    char self_short_name[5];
    char self_long_name[37];
    char self_id_str[12];
    uint8_t self_public_key[32];
    uint8_t self_private_key[32];
    bool self_keys_valid;
    bool inbox_unread;
    bool dm_inbox_unread;
    uint8_t inbox_scroll;
    uint8_t current_view;
    char text_buffer[128];
    char pending_tx_text[128];
    bool pending_tx;
    bool pending_tx_is_dm;
    uint32_t pending_tx_id;
    uint32_t pending_tx_to;
    int8_t pending_tx_msg_index;
    bool sending_dm;
    uint32_t dm_target_id;
    char dm_target_short[5];
    uint32_t last_nodeinfo_sent;
    uint8_t nodeinfo_burst_left;
    uint8_t nodeinfo_interval_index;
    uint32_t nodeinfo_interval_ms;
    uint32_t last_nodeinfo_request_ms;
    bool pending_pos;
    uint8_t pending_pos_payload[64];
    uint8_t pending_pos_len;
    uint32_t pending_pos_from;
    uint32_t pending_pos_id;

    // Settings
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    float current_freq_mhz;
    float current_bw_khz;
    uint8_t current_sf;
    uint8_t current_cr;
    uint8_t channel_psk[32];
    uint8_t channel_psk_len;
    uint8_t channel_preset_index;
    uint8_t channel_hash;
    char channel_key_input[80];
    char channel_name[16];
    char channel_name_input[16];
    char short_name_input[5];
    char long_name_input[37];

    // For UI
    uint8_t scroll_position;
    uint8_t total_lines;
    uint8_t error_scroll;

    uint32_t last_rssi_log;

    // Relay de-dup cache
    uint32_t relay_from[16];
    uint32_t relay_id[16];
    uint8_t relay_count;
    uint8_t relay_index;
} MeshtasticApp;

// ============ FUNCTION PROTOTYPES ============
// SPI
void spi_init(void);
uint8_t spi_transfer(uint8_t tx);
void spi_begin(void);
void spi_end(void);

// Radio
bool rfm95w_init(MeshtasticApp* app);
void rfm95w_set_mode_standby(void);
void rfm95w_set_mode_rx(void);
bool rfm95w_send_packet(MeshtasticApp* app, uint8_t* data, size_t len);
int rfm95w_read_packet(MeshtasticApp* app, uint8_t* buffer);
void rfm95w_set_dio0_interrupt(MeshtasticApp* app, bool enable);
bool rfm95w_dio0_status(void);
void rfm95w_dio0_clear(void);
int8_t rfm95w_get_last_rssi(void);
int8_t rfm95w_get_last_snr(void);
int16_t rfm95w_get_current_rssi(void);
uint8_t read_reg(uint8_t reg);
void write_reg(uint8_t reg, uint8_t val);
bool rfm95w_apply_config(
    MeshtasticApp* app,
    float freq_mhz,
    float bw_khz,
    uint8_t sf,
    uint8_t cr,
    uint8_t tx_power_dbm);

// Protocol
void process_received_packet(MeshtasticApp* app, uint8_t* data, size_t len);
void meshtastic_store_message(
    MeshtasticApp* app,
    uint32_t from,
    uint32_t to,
    uint32_t id,
    const char* text,
    bool outgoing,
    bool is_dm);
size_t meshtastic_sanitize_text(char* out, size_t out_cap, const char* in);
size_t meshtastic_sanitize_bytes(char* out, size_t out_cap, const uint8_t* in, size_t in_len);
bool meshtastic_send_ack(MeshtasticApp* app, uint32_t to, uint32_t request_id);
bool meshtastic_send_nodeinfo(MeshtasticApp* app, uint32_t to, bool want_response);
void meshtastic_mark_ack(MeshtasticApp* app, uint32_t request_id, bool ok, uint32_t ack_from);
bool meshtastic_relay_seen(MeshtasticApp* app, uint32_t from, uint32_t id);
void meshtastic_relay_mark(MeshtasticApp* app, uint32_t from, uint32_t id);
void meshtastic_update_node_info(
    MeshtasticApp* app,
    uint32_t node_id,
    const char* user_id,
    const char* long_name,
    const char* short_name,
    uint8_t role_index,
    const uint8_t* public_key,
    size_t public_key_len);
void meshtastic_touch_node(MeshtasticApp* app, uint32_t node_id, uint8_t hops_used);
bool meshtastic_get_node_public_key(MeshtasticApp* app, uint32_t node_id, uint8_t* out, size_t out_len);
void meshtastic_set_node_pubkey_mode(MeshtasticApp* app, uint32_t node_id, uint8_t mode);
