#include "meshtastic.h"
#include "meshtastic_crypto.h"
#include "meshtastic_protocol.h"
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/text_box.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/popup.h>
#include <gui/view.h>
#include <stdarg.h>

#define MESHTASTIC_DEFAULT_CHASH 0x08
#define MESHTASTIC_SELF_LONG_NAME "FlipperMeshtastic"
#define MESHTASTIC_SELF_SHORT_NAME "FLIP"
#define MESHTASTIC_CHANNEL_NAME "LongFast"
#define DEFAULT_MAX_HOPS 3
#define MESSAGES_DIR APP_DATA_PATH("meshtastic")
#define MESSAGES_PATH APP_DATA_PATH("meshtastic/messages.bin")
#define DM_MESSAGES_PATH APP_DATA_PATH("meshtastic/dm_messages.bin")
#define NODES_PATH APP_DATA_PATH("meshtastic/nodes.bin")
#define WEATHER_PATH APP_DATA_PATH("meshtastic/weather.bin")
#define PACKET_CACHE_PATH APP_DATA_PATH("meshtastic/packet_cache.bin")
#define LOGS_DIR APP_DATA_PATH("meshtastic/logs")
#define MESSAGES_MAGIC 0x4D534854u
#define MESSAGES_VERSION 2
#define WEATHER_MAGIC 0x57454852u
#define WEATHER_VERSION 1
#define PACKET_CACHE_MAGIC 0x504B5443u
#define PACKET_CACHE_VERSION 1
#define PACKET_CACHE_TTL_SEC (30 * 60)
#define SETTINGS_PATH APP_DATA_PATH("meshtastic/settings_v6.bin")
#define SETTINGS_MAGIC 0x4D534747u
#define SETTINGS_VERSION 10
#define NODEINFO_BURST_COUNT 0
#define NODEINFO_BURST_INTERVAL_MS 1000
#define RADIO_STARTUP_DELAY_MS 1000
#define ACK_RETRY_INTERVAL_MS 5000
#define ACK_MAX_ATTEMPTS 3
#define SETTINGS_FREQ_SLOT_UI_COUNT 256
#define SETTINGS_HOLD_DELAY_MS 400
#define SETTINGS_HOLD_REPEAT_MS 143
#define LOG_BROWSER_MAX_FILES 12
#define DEFAULT_MIN_BACKOFF_MS 250
#define DEFAULT_MAX_BACKOFF_MS 1500
#define DEFAULT_MAX_TX_WAIT_MS 5000
#define DEFAULT_REBROADCAST_MIN_DELAY_MS 500
#define DEFAULT_REBROADCAST_MAX_DELAY_MS 3000

#define INBOX_VISIBLE_LINES 5
#define INBOX_LINE_HEIGHT 10
#define INBOX_TOP_Y 18
#define INBOX_BOTTOM_Y 124

typedef enum {
    AckStateNone = 0,
    AckStatePending = 1,
    AckStateOk = 2,
    AckStateFailed = 3,
    AckStateRelay = 4,
    AckStateBusy = 5,
} AckState;

typedef enum {
    ViewIdMenu,
    ViewIdInbox,
    ViewIdDmInbox,
    ViewIdSend,
    ViewIdSettings,
    ViewIdPopup,
    ViewIdNodes,
    ViewIdContacts,
    ViewIdNodeDetail,
    ViewIdWeather,
    ViewIdLogs,
    ViewIdLogFiles,
    ViewIdLogText,
} MeshtasticViewId;

typedef enum {
    MenuItemInbox,
    MenuItemSend,
    MenuItemBroadcastNodeinfo,
    MenuItemNodes,
    MenuItemContacts,
    MenuItemWeather,
    MenuItemSettings,
    MenuItemLogs,
} MeshtasticMenuItem;

typedef enum {
    SettingItemMaxHops,
    SettingItemFreqSlot,
    SettingItemPreset,
    SettingItemTxPower,
    SettingItemRole,
    SettingItemNodeinfoInterval,
    SettingItemMessageLimit,
    SettingItemTemperatureUnit,
    SettingItemPolitenessAdvanced,
    SettingItemClearLogs,
    SettingItemRegion,
    SettingItemShortName,
    SettingItemLongName,
    SettingItemChannelName,
    SettingItemChannelPreset,
    SettingItemChannelCustom,
    SettingItemClear,
    SettingItemClearNodes,
    SettingItemClearWeather,
    SettingItemResetKeys,
    SettingItemReset,
    SettingItemCount,
} MeshtasticSettingItem;

typedef enum {
    PoliteItemEnabled,
    PoliteItemCadEnabled,
    PoliteItemRssiFallback,
    PoliteItemRssiThreshold,
    PoliteItemMinBackoff,
    PoliteItemMaxBackoff,
    PoliteItemMaxWait,
    PoliteItemRelayMinDelay,
    PoliteItemRelayMaxDelay,
    PoliteItemBack,
    PoliteItemCount,
} PolitenessSettingItem;

typedef enum {
    PresetLongFast,
    PresetLongTurbo,
    PresetLongModerate,
    PresetLongSlow,
    PresetMediumFast,
    PresetMediumSlow,
    PresetShortFast,
    PresetShortSlow,
    PresetShortTurbo,
    PresetCount,
} MeshtasticPreset;

static const char* preset_labels[] = {
    "Long Fast",
    "Long Turbo",
    "Long Moderate",
    "Long Slow",
    "Medium Fast",
    "Medium Slow",
    "Short Fast",
    "Short Slow",
    "Short Turbo",
};

static const char* role_labels[] = {
    "Client",
    "Client Mute",
};

static const char* role_label_from_index(uint8_t role_index) {
    switch(role_index) {
    case 0: return "Client";
    case 1: return "Client Mute";
    case 2: return "Router";
    case 3: return "Router Client";
    case 4: return "Repeater";
    case 5: return "Tracker";
    case 6: return "Sensor";
    case 7: return "TAK";
    case 8: return "Client Hidden";
    case 9: return "Lost & Found";
    case 10: return "TAK Tracker";
    case 11: return "Router Late";
    case 12: return "Client Base";
    default: return "Unknown";
    }
}

static const uint32_t nodeinfo_intervals_ms[] = {
    30 * 1000,
    60 * 1000,
    2 * 60 * 1000,
    3 * 60 * 1000,
    4 * 60 * 1000,
    5 * 60 * 1000,
    10 * 60 * 1000,
    20 * 60 * 1000,
    30 * 60 * 1000,
    40 * 60 * 1000,
    50 * 60 * 1000,
    60 * 60 * 1000,
};

static const char* nodeinfo_interval_labels[] = {
    "30 sec",
    "1 min",
    "2 min",
    "3 min",
    "4 min",
    "5 min",
    "10 min",
    "20 min",
    "30 min",
    "40 min",
    "50 min",
    "1 hour",
};

static const uint8_t message_limit_values[] = {
    10,
    20,
    30,
    40,
    50,
};

static const char* message_limit_labels[] = {
    "10",
    "20",
    "30",
    "40",
    "50",
};

static const char* channel_preset_labels[] = {
    "PSK 1 (AQ==)",
    "PSK 2 (Ag==)",
    "PSK 3 (Aw==)",
    "PSK 4 (BA==)",
    "Custom",
};

static const uint8_t channel_preset_psk_bytes[] = {1, 2, 3, 4};

static const char* log_category_labels[] = {
    "Received messages",
    "Sent messages",
    "Rebroadcasting",
    "Politeness",
    "NodeInfo",
    "Telemetry",
};

static const char* log_category_dirs[] = {
    "received",
    "sent",
    "rebroadcast",
    "politeness",
    "nodeinfo",
    "telemetry",
};

// Forward declarations
static void meshtastic_nodes_menu_callback(void* ctx, uint32_t index);
static void meshtastic_contacts_menu_callback(void* ctx, uint32_t index);
static void meshtastic_logs_menu_callback(void* ctx, uint32_t index);
static void meshtastic_log_files_callback(void* ctx, uint32_t index);
static void meshtastic_switch_view(MeshtasticApp* app, uint32_t view_id);
static void settings_enter(void* ctx, uint32_t index);
static void meshtastic_update_inbox_label(MeshtasticApp* app);
static void meshtastic_update_dm_inbox_label(MeshtasticApp* app);
static void meshtastic_update_weather_label(MeshtasticApp* app);
static void meshtastic_channel_key_done(void* ctx);
static void meshtastic_channel_name_done(void* ctx);
static void meshtastic_short_name_done(void* ctx);
static void meshtastic_long_name_done(void* ctx);
static void inbox_redraw(MeshtasticApp* app);
static void dm_inbox_redraw(MeshtasticApp* app);
static void weather_redraw(MeshtasticApp* app);
static void get_node_display_name(MeshtasticApp* app, uint32_t node_id, char* out, size_t cap);
static bool log_name_is_log_file(const char* name);

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
} MessagesFileHeader;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
} NodesFileHeader;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
} PacketCacheFileHeader;

typedef struct {
    uint32_t from;
    uint32_t id;
    uint32_t seen_at;
} PacketCacheRecord;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint32_t saved_ts;
} NodesFileHeaderV2;

typedef struct {
    uint32_t node_id;
    char user_id[16];
    char long_name[40];
    char short_name[5];
    uint8_t role_index;
    uint32_t age;
    uint8_t has_public_key;
    uint8_t public_key[32];
    uint8_t pubkey_mode;
    uint8_t last_hops;
} NodeFileRecordV7;

typedef struct {
    uint32_t node_id;
    char user_id[16];
    char long_name[40];
    char short_name[5];
    uint8_t role_index;
    uint32_t age;
    uint8_t has_public_key;
    uint8_t public_key[32];
    uint8_t pubkey_mode;
    uint8_t last_hops;
    int8_t last_rssi;
    int8_t last_snr;
} NodeFileRecordV8;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint32_t saved_ts;
} WeatherFileHeaderV1;

typedef struct {
    uint32_t node_id;
    uint32_t age;
    uint8_t valid;
    uint8_t has_temperature;
    float temperature;
    uint8_t has_humidity;
    float humidity;
    uint8_t has_pressure;
    float pressure;
    uint8_t has_wind_direction;
    uint16_t wind_direction;
    uint8_t has_wind_speed;
    float wind_speed;
    uint8_t has_wind_gust;
    float wind_gust;
} WeatherFileRecordV1;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t reserved[7];
} SettingsFileV2;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t reserved[7];
    uint8_t private_key[32];
    uint8_t public_key[32];
} SettingsFileV3;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t channel_preset_index;
    uint8_t channel_psk_len;
    uint8_t reserved[5];
    uint8_t channel_psk[32];
    uint8_t private_key[32];
    uint8_t public_key[32];
} SettingsFileV4;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t channel_preset_index;
    uint8_t channel_psk_len;
    char channel_name[16];
    uint8_t reserved[1];
    uint8_t channel_psk[32];
    uint8_t private_key[32];
    uint8_t public_key[32];
} SettingsFileV5;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t channel_preset_index;
    uint8_t channel_psk_len;
    char channel_name[16];
    char self_short_name[5];
    char self_long_name[37];
    uint8_t channel_psk[32];
    uint8_t private_key[32];
    uint8_t public_key[32];
} SettingsFileV6;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t channel_preset_index;
    uint8_t channel_psk_len;
    char channel_name[16];
    char self_short_name[5];
    char self_long_name[37];
    uint8_t channel_psk[32];
    uint8_t private_key[32];
    uint8_t public_key[32];
    uint8_t message_limit_index;
} SettingsFileV7;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t channel_preset_index;
    uint8_t channel_psk_len;
    char channel_name[16];
    char self_short_name[5];
    char self_long_name[37];
    uint8_t channel_psk[32];
    uint8_t private_key[32];
    uint8_t public_key[32];
    uint8_t message_limit_index;
    uint8_t politeness_enabled;
    uint8_t cad_enabled;
    uint8_t rssi_fallback_enabled;
    int8_t rssi_busy_threshold_dbm;
    uint16_t min_backoff_ms;
    uint16_t max_backoff_ms;
    uint16_t max_tx_wait_ms;
    uint16_t rebroadcast_min_delay_ms;
    uint16_t rebroadcast_max_delay_ms;
} SettingsFileV8;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t channel_preset_index;
    uint8_t channel_psk_len;
    char channel_name[16];
    char self_short_name[5];
    char self_long_name[37];
    uint8_t channel_psk[32];
    uint8_t private_key[32];
    uint8_t public_key[32];
    uint8_t message_limit_index;
    uint8_t politeness_enabled;
    uint8_t cad_enabled;
    uint8_t rssi_fallback_enabled;
    int8_t rssi_busy_threshold_dbm;
    uint16_t min_backoff_ms;
    uint16_t max_backoff_ms;
    uint16_t max_tx_wait_ms;
    uint16_t rebroadcast_min_delay_ms;
    uint16_t rebroadcast_max_delay_ms;
    uint8_t weather_fahrenheit;
} SettingsFileV9;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t max_hops;
    uint8_t preset_index;
    uint8_t freq_slot;
    uint8_t tx_power_dbm;
    uint8_t role_index;
    uint8_t nodeinfo_interval_index;
    uint8_t channel_preset_index;
    uint8_t channel_psk_len;
    char channel_name[16];
    char self_short_name[5];
    char self_long_name[37];
    uint8_t channel_psk[32];
    uint8_t private_key[32];
    uint8_t public_key[32];
    uint8_t message_limit_index;
    uint8_t politeness_enabled;
    uint8_t cad_enabled;
    uint8_t rssi_fallback_enabled;
    int8_t rssi_busy_threshold_dbm; // Legacy absolute threshold; ignored by v10+.
    uint16_t min_backoff_ms;
    uint16_t max_backoff_ms;
    uint16_t max_tx_wait_ms;
    uint16_t rebroadcast_min_delay_ms;
    uint16_t rebroadcast_max_delay_ms;
    uint8_t weather_fahrenheit;
    uint8_t rssi_busy_margin_db;
} SettingsFileV10;

// US region (Meshtastic)
#define REGION_US_FREQ_START_MHZ 902.0f
#define REGION_US_FREQ_END_MHZ 928.0f
#define REGION_US_SPACING_MHZ 0.0f

typedef struct {
    MeshtasticApp* app;
} MeshtasticViewModel;

static uint32_t fnv1a32(const char* s) {
    uint32_t hash = 2166136261u;
    if(!s) return hash;
    while(*s) {
        hash ^= (uint8_t)(*s++);
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t fnv1a32_bytes(const uint8_t* data, size_t len) {
    uint32_t hash = 2166136261u;
    if(!data) return hash;
    for(size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool mem_is_all_zero(const uint8_t* data, size_t len) {
    if(!data) return true;
    for(size_t i = 0; i < len; i++) {
        if(data[i] != 0) return false;
    }
    return true;
}

static uint32_t meshtastic_get_node_id(void) {
    const char* name = furi_hal_version_get_name_ptr();
    if(!name || name[0] == '\0') name = "flipper";
    return fnv1a32(name);
}

static size_t encode_varint(uint64_t value, uint8_t* out, size_t cap) {
    size_t i = 0;
    while(value > 0x7F) {
        if(i >= cap) return 0;
        out[i++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    if(i >= cap) return 0;
    out[i++] = (uint8_t)(value & 0x7F);
    return i;
}

static uint32_t djb2_hash(const char* str) {
    uint32_t hash = 5381;
    if(!str) return hash;
    int c;
    while((c = *str++)) {
        hash = ((hash << 5) + hash) + (uint32_t)c;
    }
    return hash;
}

static int base64_val(char c) {
    if(c >= 'A' && c <= 'Z') return c - 'A';
    if(c >= 'a' && c <= 'z') return c - 'a' + 26;
    if(c >= '0' && c <= '9') return c - '0' + 52;
    if(c == '+' || c == '-') return 62;
    if(c == '/' || c == '_') return 63;
    return -1;
}

static bool base64_decode(const char* in, uint8_t* out, size_t out_cap, size_t* out_len) {
    if(!in || !out || !out_len) return false;
    size_t len = strlen(in);
    size_t out_i = 0;
    uint32_t acc = 0;
    int acc_bits = 0;
    for(size_t i = 0; i < len; i++) {
        char c = in[i];
        if(c == '\r' || c == '\n' || c == ' ') continue;
        if(c == '=') {
            break;
        }
        int v = base64_val(c);
        if(v < 0) return false;
        acc = (acc << 6) | (uint32_t)v;
        acc_bits += 6;
        if(acc_bits >= 8) {
            acc_bits -= 8;
            if(out_i >= out_cap) return false;
            out[out_i++] = (uint8_t)((acc >> acc_bits) & 0xFF);
        }
    }
    *out_len = out_i;
    return true;
}

static uint8_t xor_hash_bytes(const uint8_t* data, size_t len) {
    uint8_t h = 0;
    if(!data) return 0;
    for(size_t i = 0; i < len; i++) h ^= data[i];
    return h;
}

static size_t safe_strnlen(const char* s, size_t max_len) {
    if(!s) return 0;
    size_t n = 0;
    while(n < max_len && s[n] != '\0') n++;
    return n;
}

static void log_make_timestamp(char* out, size_t cap) {
    if(!out || cap == 0) return;
    DateTime dt = {0};
    furi_hal_rtc_get_datetime(&dt);
    if(dt.year < 2000 || dt.month == 0 || dt.day == 0) {
        snprintf(out, cap, "%lu", (unsigned long)furi_hal_rtc_get_timestamp());
        return;
    }
    snprintf(
        out,
        cap,
        "%04u-%02u-%02u_%02u-%02u-%02u",
        (unsigned)dt.year,
        (unsigned)dt.month,
        (unsigned)dt.day,
        (unsigned)dt.hour,
        (unsigned)dt.minute,
        (unsigned)dt.second);
}

static bool log_category_path(MeshtasticLogCategory category, const char* name, char* out, size_t cap) {
    if(!out || cap == 0 || category >= LogCategoryCount) return false;
    if(name && name[0] != '\0') {
        snprintf(out, cap, "%s/%s/%s", LOGS_DIR, log_category_dirs[category], name);
    } else {
        snprintf(out, cap, "%s/%s", LOGS_DIR, log_category_dirs[category]);
    }
    return true;
}

static void log_sanitize_filename(char* text) {
    if(!text) return;
    for(size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
        if(!ok) text[i] = '_';
    }
}

static void logs_write_header(MeshtasticApp* app, MeshtasticLogCategory category) {
    if(!app || !app->storage || category >= LogCategoryCount) return;
    char path[128];
    if(!log_category_path(category, "latest.log", path, sizeof(path))) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char ts[32];
        log_make_timestamp(ts, sizeof(ts));
        char line[96];
        snprintf(line, sizeof(line), "%s\n%s\n", ts, log_category_labels[category]);
        storage_file_write(file, line, strlen(line));
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void logs_rotate_category(MeshtasticApp* app, MeshtasticLogCategory category) {
    if(!app || !app->storage || category >= LogCategoryCount) return;
    char dir[128];
    char latest[128];
    if(!log_category_path(category, NULL, dir, sizeof(dir))) return;
    if(!log_category_path(category, "latest.log", latest, sizeof(latest))) return;

    storage_common_mkdir(app->storage, dir);
    if(storage_file_exists(app->storage, latest)) {
        char stamp[40] = {0};
        File* file = storage_file_alloc(app->storage);
        if(file && storage_file_open(file, latest, FSAM_READ, FSOM_OPEN_EXISTING)) {
            char raw[40] = {0};
            size_t read = storage_file_read(file, raw, sizeof(raw) - 1);
            raw[read] = '\0';
            storage_file_close(file);
            size_t n = 0;
            while(raw[n] != '\0' && raw[n] != '\r' && raw[n] != '\n' && n + 1 < sizeof(stamp)) {
                stamp[n] = raw[n];
                n++;
            }
            stamp[n] = '\0';
        }
        if(file) storage_file_free(file);
        if(stamp[0] == '\0') log_make_timestamp(stamp, sizeof(stamp));
        log_sanitize_filename(stamp);

        char archive[224];
        snprintf(archive, sizeof(archive), "%s/%s.log", dir, stamp);
        if(storage_file_exists(app->storage, archive)) {
            snprintf(archive, sizeof(archive), "%s/%s_%lu.log", dir, stamp, (unsigned long)furi_get_tick());
        }
        storage_common_rename(app->storage, latest, archive);
    }
    logs_write_header(app, category);
}

static void logs_init(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    storage_common_mkdir(app->storage, LOGS_DIR);
    for(uint8_t i = 0; i < LogCategoryCount; i++) {
        logs_rotate_category(app, (MeshtasticLogCategory)i);
    }
}

void meshtastic_log_event(MeshtasticApp* app, MeshtasticLogCategory category, const char* format, ...) {
    if(!app || !app->storage || !format || category >= LogCategoryCount) return;
    char path[128];
    if(!log_category_path(category, "latest.log", path, sizeof(path))) return;
    if(!storage_file_exists(app->storage, path)) logs_write_header(app, category);

    char message[192];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    char ts[32];
    log_make_timestamp(ts, sizeof(ts));
    char line[256];
    snprintf(line, sizeof(line), "%s %s\n", ts, message);

    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(file, line, strlen(line));
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void logs_clear_category(MeshtasticApp* app, MeshtasticLogCategory category) {
    if(!app || !app->storage || category >= LogCategoryCount) return;

    char dir[128];
    if(!log_category_path(category, NULL, dir, sizeof(dir))) return;
    storage_common_mkdir(app->storage, dir);

    // Delete one file per pass so directory iteration is not invalidated by removal.
    for(uint8_t pass = 0; pass < 64; pass++) {
        char remove_name[40] = {0};
        File* file = storage_file_alloc(app->storage);
        if(!file) break;
        if(storage_dir_open(file, dir)) {
            FileInfo info = {0};
            char name[40];
            while(storage_dir_read(file, &info, name, sizeof(name))) {
                if(!file_info_is_dir(&info) && log_name_is_log_file(name)) {
                    snprintf(remove_name, sizeof(remove_name), "%s", name);
                    break;
                }
            }
            storage_dir_close(file);
        }
        storage_file_free(file);

        if(remove_name[0] == '\0') break;
        char path[224];
        if(log_category_path(category, remove_name, path, sizeof(path))) {
            storage_common_remove(app->storage, path);
        }
    }

    logs_write_header(app, category);
}

static void logs_clear_all(MeshtasticApp* app) {
    if(!app) return;
    for(uint8_t i = 0; i < LogCategoryCount; i++) {
        logs_clear_category(app, (MeshtasticLogCategory)i);
    }
}

size_t meshtastic_sanitize_text(char* out, size_t out_cap, const char* in) {
    if(!out || out_cap == 0) return 0;
    out[0] = '\0';
    if(!in) return 0;

    char scratch[256];
    if(out == in) {
        size_t copy_len = safe_strnlen(in, sizeof(scratch) - 1);
        memcpy(scratch, in, copy_len);
        scratch[copy_len] = '\0';
        in = scratch;
    }

    size_t oi = 0;
    bool in_unknown = false;
    for(size_t i = 0; in[i] != '\0'; i++) {
        uint8_t c = (uint8_t)in[i];
        bool allowed = (c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t';
        if(allowed) {
            if(oi + 1 >= out_cap) break;
            out[oi++] = (char)c;
            in_unknown = false;
        } else {
            if(!in_unknown) {
                if(oi + 2 >= out_cap) break;
                out[oi++] = '<';
                out[oi++] = '>';
                in_unknown = true;
            }
        }
    }
    out[oi] = '\0';
    return oi;
}

size_t meshtastic_sanitize_bytes(char* out, size_t out_cap, const uint8_t* in, size_t in_len) {
    if(!out || out_cap == 0) return 0;
    out[0] = '\0';
    if(!in || in_len == 0) return 0;

    uint8_t scratch[256];
    if((const uint8_t*)out == in) {
        if(in_len > sizeof(scratch)) in_len = sizeof(scratch);
        memcpy(scratch, in, in_len);
        in = scratch;
    }

    size_t oi = 0;
    bool in_unknown = false;
    for(size_t i = 0; i < in_len; i++) {
        uint8_t c = in[i];
        if(c == '\0') break;
        bool allowed = (c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t';
        if(allowed) {
            if(oi + 1 >= out_cap) break;
            out[oi++] = (char)c;
            in_unknown = false;
        } else {
            if(!in_unknown) {
                if(oi + 2 >= out_cap) break;
                out[oi++] = '<';
                out[oi++] = '>';
                in_unknown = true;
            }
        }
    }
    out[oi] = '\0';
    return oi;
}

static const char* get_channel_name(const MeshtasticApp* app) {
    if(app) return app->channel_name;
    return "";
}

static const char* get_channel_name_display(const MeshtasticApp* app) {
    if(app && app->channel_name[0] != '\0') return app->channel_name;
    return "<empty>";
}

static uint8_t compute_channel_hash(MeshtasticApp* app) {
    const char* name = get_channel_name(app);
    uint8_t h = xor_hash_bytes((const uint8_t*)name, strlen(name));
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
    if(key.length > 0) {
        h ^= xor_hash_bytes(key.bytes, key.length);
    }
    return h;
}

static void preset_to_params(
    MeshtasticPreset preset,
    float* bw_khz,
    uint8_t* sf,
    uint8_t* cr) {
    if(!bw_khz || !sf || !cr) return;
    switch(preset) {
    case PresetShortTurbo:
        *bw_khz = 500.0f;
        *cr = 5;
        *sf = 7;
        break;
    case PresetShortFast:
        *bw_khz = 250.0f;
        *cr = 5;
        *sf = 7;
        break;
    case PresetShortSlow:
        *bw_khz = 250.0f;
        *cr = 5;
        *sf = 8;
        break;
    case PresetMediumFast:
        *bw_khz = 250.0f;
        *cr = 5;
        *sf = 9;
        break;
    case PresetMediumSlow:
        *bw_khz = 250.0f;
        *cr = 5;
        *sf = 10;
        break;
    case PresetLongTurbo:
        *bw_khz = 500.0f;
        *cr = 8;
        *sf = 11;
        break;
    case PresetLongModerate:
        *bw_khz = 125.0f;
        *cr = 8;
        *sf = 11;
        break;
    case PresetLongSlow:
        *bw_khz = 125.0f;
        *cr = 8;
        *sf = 12;
        break;
    case PresetLongFast:
    default:
        *bw_khz = 250.0f;
        *cr = 5;
        *sf = 11;
        break;
    }
}

static uint32_t compute_num_channels(float bw_khz) {
    float width_mhz = bw_khz / 1000.0f;
    float denom = REGION_US_SPACING_MHZ + width_mhz;
    if(denom <= 0.0f) return 1;
    float span = REGION_US_FREQ_END_MHZ - REGION_US_FREQ_START_MHZ;
    uint32_t n = (uint32_t)(span / denom);
    if(n == 0) n = 1;
    return n;
}

static void update_radio_settings(MeshtasticApp* app, bool apply_radio) {
    if(!app) return;
    preset_to_params((MeshtasticPreset)app->preset_index, &app->current_bw_khz, &app->current_sf, &app->current_cr);
    uint32_t num_channels = compute_num_channels(app->current_bw_khz);
    uint32_t channel_num = 0;
    if(app->freq_slot == 0) {
        // Keep "Auto" on the normal LongFast slot. The editable channel name is only
        // used for Meshtastic's packet channel hash, not for retuning the radio.
        uint32_t hash = djb2_hash(MESHTASTIC_CHANNEL_NAME);
        channel_num = hash % num_channels;
    } else {
        channel_num = (app->freq_slot - 1) % num_channels;
    }
    app->current_freq_mhz = REGION_US_FREQ_START_MHZ +
                            (app->current_bw_khz / 2000.0f) +
                            ((float)channel_num * (app->current_bw_khz / 1000.0f));
    if(apply_radio && app->state != AppStateError) {
        rfm95w_apply_config(
            app,
            app->current_freq_mhz,
            app->current_bw_khz,
            app->current_sf,
            app->current_cr,
            app->tx_power_dbm);
    }
}

static uint32_t ack_retry_interval_ms(MeshtasticApp* app) {
    if(!app) return 5000;
    float bw_khz = 250.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;
    preset_to_params((MeshtasticPreset)app->preset_index, &bw_khz, &sf, &cr);
    if(bw_khz <= 0.0f) bw_khz = 250.0f;

    float symbol_ms = (float)(1UL << sf) / bw_khz;
    float coding_factor = 1.0f + ((cr > 5) ? ((float)(cr - 5) * 0.12f) : 0.0f);
    uint8_t hops = app->max_hops;
    if(hops < 1) hops = 1;
    if(hops > 7) hops = 7;

    uint32_t timeout = (uint32_t)(((symbol_ms * 160.0f * coding_factor) + 750.0f) * (float)(hops + 1));
    if(timeout < 5000) timeout = 5000;
    if(timeout > 120000) timeout = 120000;
    return timeout;
}

static void messages_reset(MeshtasticApp* app) {
    if(!app) return;
    memset(app->messages, 0, sizeof(app->messages));
    app->msg_count = 0;
    app->inbox_scroll = 0;
    app->inbox_unread = false;
}

static void dm_messages_reset(MeshtasticApp* app) {
    if(!app) return;
    memset(app->dm_messages, 0, sizeof(app->dm_messages));
    app->dm_count = 0;
    app->dm_inbox_scroll = 0;
}

static void nodes_reset(MeshtasticApp* app) {
    if(!app) return;
    memset(app->nodes, 0, sizeof(app->nodes));
    app->node_count = 0;
    app->selected_node_index = 0;
}

static void weather_reset(MeshtasticApp* app) {
    if(!app) return;
    memset(app->weather, 0, sizeof(app->weather));
    app->weather_count = 0;
    app->weather_scroll = 0;
}

static void settings_clamp_politeness(MeshtasticApp* app) {
    if(!app) return;
    if(app->rssi_busy_margin_db < 6 || app->rssi_busy_margin_db > 10) {
        app->rssi_busy_margin_db = DEFAULT_RSSI_BUSY_MARGIN_DB;
    }
    if(app->rssi_noise_floor_dbm < -127 || app->rssi_noise_floor_dbm > 0) {
        app->rssi_noise_floor_dbm = DEFAULT_RSSI_NOISE_FLOOR_DBM;
        app->rssi_noise_floor_valid = false;
    }
    if(app->min_backoff_ms < 50) app->min_backoff_ms = 50;
    if(app->max_backoff_ms < app->min_backoff_ms) app->max_backoff_ms = app->min_backoff_ms;
    if(app->max_backoff_ms > 10000) app->max_backoff_ms = 10000;
    if(app->max_tx_wait_ms < 250) app->max_tx_wait_ms = DEFAULT_MAX_TX_WAIT_MS;
    if(app->max_tx_wait_ms > 30000) app->max_tx_wait_ms = 30000;
    if(app->rebroadcast_min_delay_ms > 30000) {
        app->rebroadcast_min_delay_ms = DEFAULT_REBROADCAST_MIN_DELAY_MS;
    }
    if(app->rebroadcast_max_delay_ms < app->rebroadcast_min_delay_ms) {
        app->rebroadcast_max_delay_ms = app->rebroadcast_min_delay_ms;
    }
    if(app->rebroadcast_max_delay_ms > 30000) app->rebroadcast_max_delay_ms = 30000;
}

static void settings_defaults(MeshtasticApp* app) {
    if(!app) return;
    app->max_hops = DEFAULT_MAX_HOPS;
    app->preset_index = PresetLongFast;
    app->freq_slot = 0;
    app->tx_power_dbm = 20;
    app->role_index = RoleClient;
    app->nodeinfo_interval_index = 8;
    app->nodeinfo_interval_ms = nodeinfo_intervals_ms[app->nodeinfo_interval_index];
    app->message_limit_index = 0;
    app->message_limit = message_limit_values[app->message_limit_index];
    app->weather_fahrenheit = false;
    app->channel_preset_index = 0;
    app->channel_psk_len = 1;
    app->channel_psk[0] = 0x01;
    app->politeness_enabled = true;
    app->cad_enabled = true;
    app->rssi_fallback_enabled = true;
    app->rssi_busy_margin_db = DEFAULT_RSSI_BUSY_MARGIN_DB;
    app->rssi_noise_floor_dbm = DEFAULT_RSSI_NOISE_FLOOR_DBM;
    app->rssi_noise_floor_valid = false;
    app->min_backoff_ms = DEFAULT_MIN_BACKOFF_MS;
    app->max_backoff_ms = DEFAULT_MAX_BACKOFF_MS;
    app->max_tx_wait_ms = DEFAULT_MAX_TX_WAIT_MS;
    app->rebroadcast_min_delay_ms = DEFAULT_REBROADCAST_MIN_DELAY_MS;
    app->rebroadcast_max_delay_ms = DEFAULT_REBROADCAST_MAX_DELAY_MS;
    meshtastic_sanitize_text(app->self_short_name, sizeof(app->self_short_name), MESHTASTIC_SELF_SHORT_NAME);
    meshtastic_sanitize_text(app->self_long_name, sizeof(app->self_long_name), MESHTASTIC_SELF_LONG_NAME);
    memset(app->channel_name, 0, sizeof(app->channel_name));
    strncpy(app->channel_name, MESHTASTIC_CHANNEL_NAME, sizeof(app->channel_name) - 1);
    app->channel_hash = compute_channel_hash(app);
    memset(app->self_private_key, 0, sizeof(app->self_private_key));
    memset(app->self_public_key, 0, sizeof(app->self_public_key));
    app->self_keys_valid = crypto_curve25519_generate_keypair(app->self_public_key, app->self_private_key);
}

static void settings_save(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        SettingsFileV10 data = {
            .magic = SETTINGS_MAGIC,
            .version = SETTINGS_VERSION,
            .max_hops = app->max_hops,
            .preset_index = app->preset_index,
            .freq_slot = app->freq_slot,
            .tx_power_dbm = app->tx_power_dbm,
            .role_index = app->role_index,
            .nodeinfo_interval_index = app->nodeinfo_interval_index,
            .channel_preset_index = app->channel_preset_index,
            .channel_psk_len = app->channel_psk_len,
            .message_limit_index = app->message_limit_index,
            .politeness_enabled = app->politeness_enabled ? 1 : 0,
            .cad_enabled = app->cad_enabled ? 1 : 0,
            .rssi_fallback_enabled = app->rssi_fallback_enabled ? 1 : 0,
            .rssi_busy_threshold_dbm = (int8_t)(app->rssi_noise_floor_dbm + app->rssi_busy_margin_db),
            .min_backoff_ms = app->min_backoff_ms,
            .max_backoff_ms = app->max_backoff_ms,
            .max_tx_wait_ms = app->max_tx_wait_ms,
            .rebroadcast_min_delay_ms = app->rebroadcast_min_delay_ms,
            .rebroadcast_max_delay_ms = app->rebroadcast_max_delay_ms,
            .weather_fahrenheit = app->weather_fahrenheit ? 1 : 0,
            .rssi_busy_margin_db = app->rssi_busy_margin_db,
        };
        memcpy(data.channel_name, app->channel_name, sizeof(data.channel_name));
        memcpy(data.self_short_name, app->self_short_name, sizeof(data.self_short_name));
        memcpy(data.self_long_name, app->self_long_name, sizeof(data.self_long_name));
        memcpy(data.channel_psk, app->channel_psk, sizeof(data.channel_psk));
        memcpy(data.private_key, app->self_private_key, sizeof(data.private_key));
        memcpy(data.public_key, app->self_public_key, sizeof(data.public_key));
        storage_file_write(file, &data, sizeof(data));
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void settings_load(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    bool settings_dirty = false;
    FURI_LOG_I("Main", "Settings load: start");
    if(!storage_file_exists(app->storage, SETTINGS_PATH)) return;
    FURI_LOG_I("Main", "Settings load: file exists");
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    FURI_LOG_I("Main", "Settings load: file alloc");
    if(storage_file_open(file, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_I("Main", "Settings load: file open");
        SettingsFileV10 data = {0};
        size_t read = storage_file_read(file, &data, sizeof(data));
        FURI_LOG_I("Main", "Settings load: read %u", (unsigned)read);
        if(read >= sizeof(SettingsFileV2) && data.magic == SETTINGS_MAGIC) {
            if(data.version >= 2 && data.version <= SETTINGS_VERSION) {
                if(data.max_hops < 1 || data.max_hops > 7) data.max_hops = DEFAULT_MAX_HOPS;
                if(data.preset_index >= PresetCount) data.preset_index = PresetLongFast;
                if(data.tx_power_dbm < 2 || data.tx_power_dbm > 20) data.tx_power_dbm = 20;
                if(data.role_index >= RoleCount) data.role_index = RoleClient;
                if(data.nodeinfo_interval_index >=
                   (sizeof(nodeinfo_intervals_ms) / sizeof(nodeinfo_intervals_ms[0]))) {
                    data.nodeinfo_interval_index = 8;
                }
                app->max_hops = data.max_hops;
                app->preset_index = data.preset_index;
                app->freq_slot = data.freq_slot;
                app->tx_power_dbm = data.tx_power_dbm;
                app->role_index = data.role_index;
                app->nodeinfo_interval_index = data.nodeinfo_interval_index;
                app->nodeinfo_interval_ms = nodeinfo_intervals_ms[app->nodeinfo_interval_index];

                if(data.version >= 6 && read >= sizeof(SettingsFileV6)) {
                    SettingsFileV6* v6 = (SettingsFileV6*)&data;
                    if(v6->channel_preset_index >=
                       (sizeof(channel_preset_labels) / sizeof(channel_preset_labels[0]))) {
                        v6->channel_preset_index = 0;
                    }
                    if(v6->channel_psk_len > sizeof(v6->channel_psk)) v6->channel_psk_len = 0;
                    app->channel_preset_index = v6->channel_preset_index;
                    app->channel_psk_len = v6->channel_psk_len;
                    memcpy(app->channel_psk, v6->channel_psk, sizeof(app->channel_psk));
                    memcpy(app->channel_name, v6->channel_name, sizeof(app->channel_name));
                    app->channel_name[sizeof(app->channel_name) - 1] = '\0';
                    if(app->channel_psk_len == 0) {
                        app->channel_psk_len = 1;
                        app->channel_psk[0] = 0x01;
                    }
                    meshtastic_sanitize_bytes(
                        app->self_short_name,
                        sizeof(app->self_short_name),
                        (const uint8_t*)v6->self_short_name,
                        sizeof(v6->self_short_name));
                    meshtastic_sanitize_bytes(
                        app->self_long_name,
                        sizeof(app->self_long_name),
                        (const uint8_t*)v6->self_long_name,
                        sizeof(v6->self_long_name));
                    if(app->self_short_name[0] == '\0') {
                        meshtastic_sanitize_text(
                            app->self_short_name,
                            sizeof(app->self_short_name),
                            MESHTASTIC_SELF_SHORT_NAME);
                        settings_dirty = true;
                    }
                    if(app->self_long_name[0] == '\0') {
                        meshtastic_sanitize_text(
                            app->self_long_name,
                            sizeof(app->self_long_name),
                            MESHTASTIC_SELF_LONG_NAME);
                        settings_dirty = true;
                    }
                    if(data.version >= 7 && read >= sizeof(SettingsFileV7)) {
                        SettingsFileV7* v7 = (SettingsFileV7*)&data;
                        if(v7->message_limit_index >=
                           (sizeof(message_limit_values) / sizeof(message_limit_values[0]))) {
                            v7->message_limit_index = 0;
                            settings_dirty = true;
                        }
                        app->message_limit_index = v7->message_limit_index;
                        app->message_limit = message_limit_values[app->message_limit_index];
                    } else {
                        app->message_limit_index = 0;
                        app->message_limit = message_limit_values[app->message_limit_index];
                        settings_dirty = true;
                    }
                    if(data.version >= 8 && read >= sizeof(SettingsFileV8)) {
                        SettingsFileV8* v8 = (SettingsFileV8*)&data;
                        app->politeness_enabled = v8->politeness_enabled ? true : false;
                        app->cad_enabled = v8->cad_enabled ? true : false;
                        app->rssi_fallback_enabled = v8->rssi_fallback_enabled ? true : false;
                        app->min_backoff_ms = v8->min_backoff_ms;
                        app->max_backoff_ms = v8->max_backoff_ms;
                        app->max_tx_wait_ms = v8->max_tx_wait_ms;
                        app->rebroadcast_min_delay_ms = v8->rebroadcast_min_delay_ms;
                        app->rebroadcast_max_delay_ms = v8->rebroadcast_max_delay_ms;
                        app->rssi_busy_margin_db = DEFAULT_RSSI_BUSY_MARGIN_DB;
                        settings_clamp_politeness(app);
                        if(data.version >= 9 && read >= sizeof(SettingsFileV9)) {
                            SettingsFileV9* v9 = (SettingsFileV9*)&data;
                            app->weather_fahrenheit = v9->weather_fahrenheit ? true : false;
                        } else {
                            app->weather_fahrenheit = false;
                            settings_dirty = true;
                        }
                        if(data.version >= 10 && read >= sizeof(SettingsFileV10)) {
                            SettingsFileV10* v10 = (SettingsFileV10*)&data;
                            app->rssi_busy_margin_db = v10->rssi_busy_margin_db;
                            settings_clamp_politeness(app);
                        } else {
                            settings_dirty = true;
                        }
                    } else {
                        settings_dirty = true;
                    }
                    app->channel_hash = compute_channel_hash(app);
                } else if(data.version == 5 && read >= sizeof(SettingsFileV5)) {
                    SettingsFileV5* v5 = (SettingsFileV5*)&data;
                    if(v5->channel_preset_index >=
                       (sizeof(channel_preset_labels) / sizeof(channel_preset_labels[0]))) {
                        v5->channel_preset_index = 0;
                    }
                    if(v5->channel_psk_len > sizeof(v5->channel_psk)) v5->channel_psk_len = 0;
                    app->channel_preset_index = v5->channel_preset_index;
                    app->channel_psk_len = v5->channel_psk_len;
                    memcpy(app->channel_psk, v5->channel_psk, sizeof(app->channel_psk));
                    memcpy(app->channel_name, v5->channel_name, sizeof(app->channel_name));
                    app->channel_name[sizeof(app->channel_name) - 1] = '\0';
                    if(app->channel_psk_len == 0) {
                        app->channel_psk_len = 1;
                        app->channel_psk[0] = 0x01;
                    }
                    meshtastic_sanitize_text(
                        app->self_short_name,
                        sizeof(app->self_short_name),
                        MESHTASTIC_SELF_SHORT_NAME);
                    meshtastic_sanitize_text(
                        app->self_long_name,
                        sizeof(app->self_long_name),
                        MESHTASTIC_SELF_LONG_NAME);
                    settings_dirty = true;
                    app->channel_hash = compute_channel_hash(app);
                } else if(data.version == 4 && read >= sizeof(SettingsFileV4)) {
                    SettingsFileV4* v4 = (SettingsFileV4*)&data;
                    if(v4->channel_preset_index >=
                       (sizeof(channel_preset_labels) / sizeof(channel_preset_labels[0]))) {
                        v4->channel_preset_index = 0;
                    }
                    if(v4->channel_psk_len > sizeof(v4->channel_psk)) v4->channel_psk_len = 0;
                    app->channel_preset_index = v4->channel_preset_index;
                    app->channel_psk_len = v4->channel_psk_len;
                    memcpy(app->channel_psk, v4->channel_psk, sizeof(app->channel_psk));
                    memset(app->channel_name, 0, sizeof(app->channel_name));
                    strncpy(app->channel_name, MESHTASTIC_CHANNEL_NAME, sizeof(app->channel_name) - 1);
                    if(app->channel_psk_len == 0) {
                        app->channel_psk_len = 1;
                        app->channel_psk[0] = 0x01;
                    }
                    meshtastic_sanitize_text(
                        app->self_short_name,
                        sizeof(app->self_short_name),
                        MESHTASTIC_SELF_SHORT_NAME);
                    meshtastic_sanitize_text(
                        app->self_long_name,
                        sizeof(app->self_long_name),
                        MESHTASTIC_SELF_LONG_NAME);
                    settings_dirty = true;
                    app->channel_hash = compute_channel_hash(app);
                } else if(data.version == 3 && read >= sizeof(SettingsFileV3)) {
                    SettingsFileV3* v3 = (SettingsFileV3*)&data;
                    memcpy(app->self_private_key, v3->private_key, sizeof(v3->private_key));
                    memcpy(app->self_public_key, v3->public_key, sizeof(v3->public_key));
                    bool have_priv = !mem_is_all_zero(app->self_private_key, sizeof(app->self_private_key));
                    bool have_pub = !mem_is_all_zero(app->self_public_key, sizeof(app->self_public_key));
                    app->self_keys_valid = have_priv && have_pub;
                    if(have_priv) {
                        uint8_t computed_pub[32] = {0};
                        if(crypto_curve25519_public(app->self_private_key, computed_pub) &&
                           !mem_is_all_zero(computed_pub, sizeof(computed_pub))) {
                            if(!have_pub ||
                               memcmp(computed_pub, app->self_public_key, sizeof(computed_pub)) != 0) {
                                memcpy(app->self_public_key, computed_pub, sizeof(computed_pub));
                                app->self_keys_valid = true;
                                settings_save(app);
                            }
                        } else {
                            app->self_keys_valid = false;
                        }
                    }
                }
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);

    if(app->channel_psk_len == 0) {
        app->channel_psk_len = 1;
        app->channel_psk[0] = 0x01;
        app->channel_hash = compute_channel_hash(app);
        settings_dirty = true;
    }
    if(app->channel_name[0] == '\0') {
        strncpy(app->channel_name, MESHTASTIC_CHANNEL_NAME, sizeof(app->channel_name) - 1);
        app->channel_name[sizeof(app->channel_name) - 1] = '\0';
        app->channel_hash = compute_channel_hash(app);
        settings_dirty = true;
    }
    if(app->self_short_name[0] == '\0') {
        meshtastic_sanitize_text(app->self_short_name, sizeof(app->self_short_name), MESHTASTIC_SELF_SHORT_NAME);
        settings_dirty = true;
    }
    if(app->self_long_name[0] == '\0') {
        meshtastic_sanitize_text(app->self_long_name, sizeof(app->self_long_name), MESHTASTIC_SELF_LONG_NAME);
        settings_dirty = true;
    }
    if(app->message_limit == 0 || app->message_limit > MAX_MESSAGES) {
        app->message_limit_index = 0;
        app->message_limit = message_limit_values[app->message_limit_index];
        settings_dirty = true;
    }
    settings_clamp_politeness(app);
    if(!app->self_keys_valid) {
        memset(app->self_private_key, 0, sizeof(app->self_private_key));
        memset(app->self_public_key, 0, sizeof(app->self_public_key));
        app->self_keys_valid = crypto_curve25519_generate_keypair(app->self_public_key, app->self_private_key);
        settings_save(app);
    } else if(settings_dirty) {
        settings_save(app);
    }
}

static uint8_t message_limit_for_app(MeshtasticApp* app) {
    if(!app || app->message_limit == 0 || app->message_limit > MAX_MESSAGES) return 10;
    return app->message_limit;
}

static void trim_message_list_to_limit(
    Message* list,
    uint8_t* count,
    uint8_t* scroll,
    uint8_t limit) {
    if(!list || !count || !scroll || limit == 0) return;
    if(*count > limit) {
        uint8_t drop = (uint8_t)(*count - limit);
        memmove(&list[0], &list[drop], sizeof(Message) * limit);
        *count = limit;
    }
    if(*count <= INBOX_VISIBLE_LINES) {
        *scroll = 0;
    } else if(*scroll + INBOX_VISIBLE_LINES > *count) {
        *scroll = *count - INBOX_VISIBLE_LINES;
    }
}

static void trim_messages_to_limit(MeshtasticApp* app) {
    if(!app) return;
    uint8_t limit = message_limit_for_app(app);
    trim_message_list_to_limit(app->messages, &app->msg_count, &app->inbox_scroll, limit);
    trim_message_list_to_limit(app->dm_messages, &app->dm_count, &app->dm_inbox_scroll, limit);
}

static void messages_save(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, MESSAGES_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        MessagesFileHeader hdr = { .magic = MESSAGES_MAGIC, .version = MESSAGES_VERSION, .count = app->msg_count };
        storage_file_write(file, &hdr, sizeof(hdr));
        for(uint8_t i = 0; i < app->msg_count; i++) {
            Message* msg = &app->messages[i];
            storage_file_write(file, &msg->from, sizeof(msg->from));
            storage_file_write(file, &msg->to, sizeof(msg->to));
            storage_file_write(file, &msg->id, sizeof(msg->id));
            storage_file_write(file, &msg->len, sizeof(msg->len));
            uint8_t out = msg->outgoing ? 1 : 0;
            storage_file_write(file, &out, sizeof(out));
            storage_file_write(file, &msg->ack_state, sizeof(msg->ack_state));
            if(msg->len > 0) {
                storage_file_write(file, msg->payload, msg->len);
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void messages_load(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    if(!storage_file_exists(app->storage, MESSAGES_PATH)) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, MESSAGES_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        MessagesFileHeader hdr = {0};
        if(storage_file_read(file, &hdr, sizeof(hdr)) == sizeof(hdr) &&
           hdr.magic == MESSAGES_MAGIC &&
           hdr.version == MESSAGES_VERSION) {
            messages_reset(app);
            uint8_t count = hdr.count;
            if(count > MAX_MESSAGES) count = MAX_MESSAGES;
            for(uint8_t i = 0; i < count; i++) {
                Message msg = {0};
                if(storage_file_read(file, &msg.from, sizeof(msg.from)) != sizeof(msg.from)) break;
                if(storage_file_read(file, &msg.to, sizeof(msg.to)) != sizeof(msg.to)) break;
                if(storage_file_read(file, &msg.id, sizeof(msg.id)) != sizeof(msg.id)) break;
                if(storage_file_read(file, &msg.len, sizeof(msg.len)) != sizeof(msg.len)) break;
                uint8_t out = 0;
                if(storage_file_read(file, &out, sizeof(out)) != sizeof(out)) break;
                msg.outgoing = out ? true : false;
                if(storage_file_read(file, &msg.ack_state, sizeof(msg.ack_state)) != sizeof(msg.ack_state)) break;
                msg.is_dm = false;
                UNUSED(msg);
                if(msg.len > 0) {
                    uint8_t raw_payload[256] = {0};
                    if(storage_file_read(file, raw_payload, msg.len) != msg.len) break;
                    msg.len = (uint8_t)meshtastic_sanitize_bytes(
                        (char*)msg.payload, sizeof(msg.payload), raw_payload, msg.len);
                }
                app->messages[app->msg_count++] = msg;
            }
            trim_message_list_to_limit(
                app->messages,
                &app->msg_count,
                &app->inbox_scroll,
                message_limit_for_app(app));
            if(app->msg_count > INBOX_VISIBLE_LINES) {
                app->inbox_scroll = app->msg_count - INBOX_VISIBLE_LINES;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void dm_messages_save(MeshtasticApp* app);

static void messages_clear(MeshtasticApp* app) {
    messages_reset(app);
    messages_save(app);
    if(app) {
        app->inbox_unread = false;
        meshtastic_update_inbox_label(app);
        if(app->current_view == ViewIdInbox) inbox_redraw(app);
    }
}

static void dm_messages_clear(MeshtasticApp* app) {
    dm_messages_reset(app);
    dm_messages_save(app);
    if(app) {
        app->dm_inbox_unread = false;
        meshtastic_update_dm_inbox_label(app);
        if(app->current_view == ViewIdDmInbox) dm_inbox_redraw(app);
    }
}

static void dm_messages_save(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, DM_MESSAGES_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        MessagesFileHeader hdr = { .magic = MESSAGES_MAGIC, .version = MESSAGES_VERSION, .count = app->dm_count };
        storage_file_write(file, &hdr, sizeof(hdr));
        for(uint8_t i = 0; i < app->dm_count; i++) {
            Message* msg = &app->dm_messages[i];
            storage_file_write(file, &msg->from, sizeof(msg->from));
            storage_file_write(file, &msg->to, sizeof(msg->to));
            storage_file_write(file, &msg->id, sizeof(msg->id));
            storage_file_write(file, &msg->len, sizeof(msg->len));
            uint8_t out = msg->outgoing ? 1 : 0;
            storage_file_write(file, &out, sizeof(out));
            storage_file_write(file, &msg->ack_state, sizeof(msg->ack_state));
            if(msg->len > 0) {
                storage_file_write(file, msg->payload, msg->len);
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void dm_messages_load(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    if(!storage_file_exists(app->storage, DM_MESSAGES_PATH)) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, DM_MESSAGES_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        MessagesFileHeader hdr = {0};
        if(storage_file_read(file, &hdr, sizeof(hdr)) == sizeof(hdr) &&
           hdr.magic == MESSAGES_MAGIC &&
           hdr.version == MESSAGES_VERSION) {
            dm_messages_reset(app);
            uint8_t count = hdr.count;
            if(count > MAX_MESSAGES) count = MAX_MESSAGES;
            for(uint8_t i = 0; i < count; i++) {
                Message msg = {0};
                if(storage_file_read(file, &msg.from, sizeof(msg.from)) != sizeof(msg.from)) break;
                if(storage_file_read(file, &msg.to, sizeof(msg.to)) != sizeof(msg.to)) break;
                if(storage_file_read(file, &msg.id, sizeof(msg.id)) != sizeof(msg.id)) break;
                if(storage_file_read(file, &msg.len, sizeof(msg.len)) != sizeof(msg.len)) break;
                uint8_t out = 0;
                if(storage_file_read(file, &out, sizeof(out)) != sizeof(out)) break;
                msg.outgoing = out ? true : false;
                if(storage_file_read(file, &msg.ack_state, sizeof(msg.ack_state)) != sizeof(msg.ack_state)) break;
                if(msg.len > 0) {
                    uint8_t raw_payload[256] = {0};
                    if(storage_file_read(file, raw_payload, msg.len) != msg.len) break;
                    msg.len = (uint8_t)meshtastic_sanitize_bytes(
                        (char*)msg.payload, sizeof(msg.payload), raw_payload, msg.len);
                }
                msg.is_dm = true;
                app->dm_messages[app->dm_count++] = msg;
            }
            trim_message_list_to_limit(
                app->dm_messages,
                &app->dm_count,
                &app->dm_inbox_scroll,
                message_limit_for_app(app));
            if(app->dm_count > INBOX_VISIBLE_LINES) {
                app->dm_inbox_scroll = app->dm_count - INBOX_VISIBLE_LINES;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void nodes_save(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, NODES_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        NodesFileHeaderV2 hdr = {
            .magic = MESSAGES_MAGIC,
            .version = 8,
            .count = app->node_count,
            .saved_ts = furi_hal_rtc_get_timestamp(),
        };
        storage_file_write(file, &hdr, sizeof(hdr));
        for(uint8_t i = 0; i < app->node_count; i++) {
            NodeInfo* node = &app->nodes[i];
            NodeFileRecordV8 rec = {0};
            rec.node_id = node->node_id;
            memcpy(rec.user_id, node->user_id, sizeof(rec.user_id));
            memcpy(rec.long_name, node->long_name, sizeof(rec.long_name));
            memcpy(rec.short_name, node->short_name, sizeof(rec.short_name));
            rec.role_index = node->role_index;
            if(hdr.saved_ts >= node->last_heard) {
                rec.age = hdr.saved_ts - node->last_heard;
            }
            rec.has_public_key = node->has_public_key ? 1 : 0;
            if(rec.has_public_key) {
                memcpy(rec.public_key, node->public_key, sizeof(rec.public_key));
            }
            rec.pubkey_mode = node->pubkey_mode;
            rec.last_hops = node->last_hops;
            rec.last_rssi = node->last_rssi;
            rec.last_snr = node->last_snr;
            storage_file_write(file, &rec, sizeof(rec));
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void merge_duplicate_node(NodeInfo* dst, const NodeInfo* src) {
    if(!dst || !src) return;
    if(dst->user_id[0] == '\0' && src->user_id[0] != '\0') {
        memcpy(dst->user_id, src->user_id, sizeof(dst->user_id));
    }
    if(dst->long_name[0] == '\0' && src->long_name[0] != '\0') {
        memcpy(dst->long_name, src->long_name, sizeof(dst->long_name));
    }
    if(dst->short_name[0] == '\0' && src->short_name[0] != '\0') {
        memcpy(dst->short_name, src->short_name, sizeof(dst->short_name));
    }
    if(src->last_heard > dst->last_heard) {
        dst->last_heard = src->last_heard;
        dst->role_index = src->role_index;
    }
    if(dst->last_hops == 0xFF || (src->last_hops != 0xFF && src->last_hops < dst->last_hops)) {
        dst->last_hops = src->last_hops;
    }
    if(src->last_hops == 0 && src->last_rssi != 127) {
        dst->last_rssi = src->last_rssi;
        dst->last_snr = src->last_snr;
    }
    if(!dst->has_public_key && src->has_public_key) {
        dst->has_public_key = true;
        memcpy(dst->public_key, src->public_key, sizeof(dst->public_key));
        dst->pubkey_mode = src->pubkey_mode;
    }
}

static bool nodes_dedupe(MeshtasticApp* app) {
    if(!app || app->node_count < 2) return false;
    bool changed = false;
    for(uint8_t i = 0; i < app->node_count; i++) {
        for(uint8_t j = i + 1; j < app->node_count;) {
            if(app->nodes[i].node_id == app->nodes[j].node_id) {
                merge_duplicate_node(&app->nodes[i], &app->nodes[j]);
                if(j + 1 < app->node_count) {
                    memmove(
                        &app->nodes[j],
                        &app->nodes[j + 1],
                        sizeof(NodeInfo) * (app->node_count - j - 1));
                }
                app->node_count--;
                changed = true;
            } else {
                j++;
            }
        }
    }
    return changed;
}

static void nodes_load(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    if(!storage_file_exists(app->storage, NODES_PATH)) return;
    bool save_after_close = false;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, NODES_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        NodesFileHeaderV2 hdr = {0};
        if(storage_file_read(file, &hdr, sizeof(hdr)) == sizeof(hdr) &&
           hdr.magic == MESSAGES_MAGIC) {
            nodes_reset(app);
            uint8_t count = hdr.count;
            if(count > MAX_NODES) count = MAX_NODES;
            uint32_t now = furi_hal_rtc_get_timestamp();
            uint32_t delta = 0;
            if(hdr.saved_ts > 0 && now > hdr.saved_ts) {
                delta = now - hdr.saved_ts;
            }
            bool migrated = false;
            for(uint8_t i = 0; i < count; i++) {
                if(hdr.version >= 8) {
                    NodeFileRecordV8 rec = {0};
                    if(storage_file_read(file, &rec, sizeof(rec)) != sizeof(rec)) break;
                    NodeInfo node = {0};
                    node.node_id = rec.node_id;
                    meshtastic_sanitize_bytes(
                        node.user_id, sizeof(node.user_id), (const uint8_t*)rec.user_id, sizeof(rec.user_id));
                    meshtastic_sanitize_bytes(
                        node.long_name, sizeof(node.long_name), (const uint8_t*)rec.long_name, sizeof(rec.long_name));
                    meshtastic_sanitize_bytes(
                        node.short_name, sizeof(node.short_name), (const uint8_t*)rec.short_name, sizeof(rec.short_name));
                    node.role_index = rec.role_index;
                    node.has_public_key = rec.has_public_key ? true : false;
                    if(node.has_public_key) {
                        memcpy(node.public_key, rec.public_key, sizeof(node.public_key));
                    }
                    node.pubkey_mode = rec.pubkey_mode;
                    node.last_hops = rec.last_hops;
                    node.last_rssi = rec.last_rssi;
                    node.last_snr = rec.last_snr;
                    uint32_t age = rec.age + delta;
                    node.last_heard = (now > age) ? (now - age) : 0;
                    app->nodes[app->node_count++] = node;
                } else if(hdr.version >= 7) {
                    NodeFileRecordV7 rec = {0};
                    if(storage_file_read(file, &rec, sizeof(rec)) != sizeof(rec)) break;
                    NodeInfo node = {0};
                    node.node_id = rec.node_id;
                    meshtastic_sanitize_bytes(
                        node.user_id, sizeof(node.user_id), (const uint8_t*)rec.user_id, sizeof(rec.user_id));
                    meshtastic_sanitize_bytes(
                        node.long_name, sizeof(node.long_name), (const uint8_t*)rec.long_name, sizeof(rec.long_name));
                    meshtastic_sanitize_bytes(
                        node.short_name, sizeof(node.short_name), (const uint8_t*)rec.short_name, sizeof(rec.short_name));
                    node.role_index = rec.role_index;
                    node.has_public_key = rec.has_public_key ? true : false;
                    if(node.has_public_key) {
                        memcpy(node.public_key, rec.public_key, sizeof(node.public_key));
                    }
                    node.pubkey_mode = rec.pubkey_mode;
                    node.last_hops = rec.last_hops;
                    uint32_t age = rec.age + delta;
                    node.last_heard = (now > age) ? (now - age) : 0;
                    node.last_rssi = 127;
                    node.last_snr = 127;
                    app->nodes[app->node_count++] = node;
                    migrated = true;
                } else {
                    NodeInfo node = {0};
                    if(storage_file_read(file, &node.node_id, sizeof(node.node_id)) != sizeof(node.node_id)) break;
                    if(storage_file_read(file, node.user_id, sizeof(node.user_id)) != sizeof(node.user_id)) break;
                    migrated = true;
                    if(storage_file_read(file, node.short_name, sizeof(node.short_name)) != sizeof(node.short_name)) break;
                    meshtastic_sanitize_bytes(
                        node.user_id,
                        sizeof(node.user_id),
                        (const uint8_t*)node.user_id,
                        sizeof(node.user_id));
                    meshtastic_sanitize_bytes(
                        node.short_name,
                        sizeof(node.short_name),
                        (const uint8_t*)node.short_name,
                        sizeof(node.short_name));
                    if(storage_file_read(file, &node.role_index, sizeof(node.role_index)) != sizeof(node.role_index)) break;
                    uint32_t stored = 0;
                    if(storage_file_read(file, &stored, sizeof(stored)) != sizeof(stored)) break;
                    if(hdr.version >= 3) {
                        uint8_t has_pub = 0;
                        if(storage_file_read(file, &has_pub, sizeof(has_pub)) != sizeof(has_pub)) break;
                        node.has_public_key = has_pub ? true : false;
                        if(node.has_public_key) {
                            if(storage_file_read(file, node.public_key, sizeof(node.public_key)) != sizeof(node.public_key)) break;
                        }
                        if(hdr.version >= 5) {
                            uint8_t mode = 0;
                            if(storage_file_read(file, &mode, sizeof(mode)) != sizeof(mode)) break;
                            node.pubkey_mode = mode;
                            if(hdr.version >= 6) {
                                uint8_t hops = 0xFF;
                                if(storage_file_read(file, &hops, sizeof(hops)) != sizeof(hops)) break;
                                node.last_hops = hops;
                            } else {
                                node.last_hops = 0xFF;
                            }
                        } else if(hdr.version >= 4) {
                            uint8_t reversed = 0;
                            if(storage_file_read(file, &reversed, sizeof(reversed)) != sizeof(reversed)) break;
                            node.pubkey_mode = reversed ? 1 : 0;
                            node.last_hops = 0xFF;
                        } else {
                            node.pubkey_mode = 0;
                            node.last_hops = 0xFF;
                        }
                        uint32_t age = stored + delta;
                        node.last_heard = (now > age) ? (now - age) : 0;
                    } else {
                        node.last_heard = stored;
                        node.last_hops = 0xFF;
                    }
                    node.last_rssi = 127;
                    node.last_snr = 127;
                    app->nodes[app->node_count++] = node;
                }
            }
            if(nodes_dedupe(app)) {
                migrated = true;
            }
            if(migrated) {
                save_after_close = true;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    if(save_after_close) {
        nodes_save(app);
    }
}

static void weather_sort(MeshtasticApp* app) {
    if(!app) return;
    for(uint8_t i = 0; i < app->weather_count; i++) {
        for(uint8_t j = 0; j + 1 < app->weather_count - i; j++) {
            if(app->weather[j].last_heard < app->weather[j + 1].last_heard) {
                WeatherInfo tmp = app->weather[j];
                app->weather[j] = app->weather[j + 1];
                app->weather[j + 1] = tmp;
            }
        }
    }
    if(app->weather_scroll >= app->weather_count) {
        app->weather_scroll = app->weather_count ? app->weather_count - 1 : 0;
    }
}

static void weather_save(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, WEATHER_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        WeatherFileHeaderV1 hdr = {
            .magic = WEATHER_MAGIC,
            .version = WEATHER_VERSION,
            .count = app->weather_count,
            .saved_ts = furi_hal_rtc_get_timestamp(),
        };
        storage_file_write(file, &hdr, sizeof(hdr));
        for(uint8_t i = 0; i < app->weather_count; i++) {
            WeatherInfo* weather = &app->weather[i];
            WeatherFileRecordV1 rec = {0};
            rec.node_id = weather->node_id;
            if(hdr.saved_ts >= weather->last_heard) {
                rec.age = hdr.saved_ts - weather->last_heard;
            }
            rec.valid = weather->valid;
            rec.has_temperature = weather->has_temperature ? 1 : 0;
            rec.temperature = weather->temperature;
            rec.has_humidity = weather->has_humidity ? 1 : 0;
            rec.humidity = weather->humidity;
            rec.has_pressure = weather->has_pressure ? 1 : 0;
            rec.pressure = weather->pressure;
            rec.has_wind_direction = weather->has_wind_direction ? 1 : 0;
            rec.wind_direction = weather->wind_direction;
            rec.has_wind_speed = weather->has_wind_speed ? 1 : 0;
            rec.wind_speed = weather->wind_speed;
            rec.has_wind_gust = weather->has_wind_gust ? 1 : 0;
            rec.wind_gust = weather->wind_gust;
            storage_file_write(file, &rec, sizeof(rec));
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void weather_load(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    if(!storage_file_exists(app->storage, WEATHER_PATH)) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_file_open(file, WEATHER_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        WeatherFileHeaderV1 hdr = {0};
        if(storage_file_read(file, &hdr, sizeof(hdr)) == sizeof(hdr) &&
           hdr.magic == WEATHER_MAGIC &&
           hdr.version == WEATHER_VERSION) {
            weather_reset(app);
            uint8_t count = hdr.count;
            if(count > MAX_WEATHER_NODES) count = MAX_WEATHER_NODES;
            uint32_t now = furi_hal_rtc_get_timestamp();
            uint32_t delta = 0;
            if(hdr.saved_ts > 0 && now > hdr.saved_ts) {
                delta = now - hdr.saved_ts;
            }
            for(uint8_t i = 0; i < count; i++) {
                WeatherFileRecordV1 rec = {0};
                if(storage_file_read(file, &rec, sizeof(rec)) != sizeof(rec)) break;
                if(rec.node_id == 0) continue;
                WeatherInfo weather = {0};
                weather.node_id = rec.node_id;
                uint32_t age = rec.age + delta;
                weather.last_heard = (now > age) ? (now - age) : 0;
                weather.valid = rec.valid;
                weather.has_temperature = rec.has_temperature ? true : false;
                weather.temperature = rec.temperature;
                weather.has_humidity = rec.has_humidity ? true : false;
                weather.humidity = rec.humidity;
                weather.has_pressure = rec.has_pressure ? true : false;
                weather.pressure = rec.pressure;
                weather.has_wind_direction = rec.has_wind_direction ? true : false;
                weather.wind_direction = rec.wind_direction;
                weather.has_wind_speed = rec.has_wind_speed ? true : false;
                weather.wind_speed = rec.wind_speed;
                weather.has_wind_gust = rec.has_wind_gust ? true : false;
                weather.wind_gust = rec.wind_gust;
                app->weather[app->weather_count++] = weather;
            }
            weather_sort(app);
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void weather_clear(MeshtasticApp* app) {
    weather_reset(app);
    weather_save(app);
    if(app) {
        meshtastic_update_weather_label(app);
        if(app->current_view == ViewIdWeather) weather_redraw(app);
    }
}

static void nodes_clear(MeshtasticApp* app) {
    nodes_reset(app);
    nodes_save(app);
}

static size_t encode_key(uint32_t field, uint8_t wire, uint8_t* out, size_t cap) {
    return encode_varint(((uint64_t)field << 3) | (wire & 0x07), out, cap);
}

static size_t build_data_message(
    uint32_t portnum,
    const uint8_t* payload,
    size_t payload_len,
    uint32_t request_id,
    uint32_t dest,
    uint32_t source,
    uint8_t* out,
    size_t cap) {
    if(!out || cap < 4) return 0;
    size_t off = 0;

    // field 1: portnum (varint)
    size_t w = encode_key(1, 0, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(portnum, out + off, cap - off);
    if(w == 0) return 0;
    off += w;

    // field 2: payload (bytes)
    w = encode_key(2, 2, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(payload_len, out + off, cap - off);
    if(w == 0 || off + w + payload_len > cap) return 0;
    off += w;
    if(payload_len > 0 && payload) {
        memcpy(out + off, payload, payload_len);
        off += payload_len;
    }

    // field 4: dest (varint)
    if(dest != 0) {
        w = encode_key(4, 0, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(dest, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    // field 5: source (varint)
    if(source != 0) {
        w = encode_key(5, 0, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(source, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    // field 6: request_id (varint)
    if(request_id != 0) {
        w = encode_key(6, 0, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(request_id, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    return off;
}

static size_t build_data_message_with_response(
    uint32_t portnum,
    const uint8_t* payload,
    size_t payload_len,
    bool want_response,
    uint32_t request_id,
    uint32_t dest,
    uint32_t source,
    uint8_t* out,
    size_t cap) {
    if(!out || cap < 4) return 0;
    size_t off = 0;
    size_t w = encode_key(1, 0, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(portnum, out + off, cap - off);
    if(w == 0) return 0;
    off += w;

    w = encode_key(2, 2, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(payload_len, out + off, cap - off);
    if(w == 0 || off + w + payload_len > cap) return 0;
    off += w;
    if(payload_len > 0 && payload) {
        memcpy(out + off, payload, payload_len);
        off += payload_len;
    }

    if(want_response) {
        w = encode_key(3, 0, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(1, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    if(dest != 0) {
        w = encode_key(4, 0, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(dest, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    if(source != 0) {
        w = encode_key(5, 0, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(source, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    if(request_id != 0) {
        w = encode_key(6, 0, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(request_id, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    return off;
}

static size_t build_text_data_message(
    const char* text,
    uint32_t dest,
    uint32_t source,
    bool want_response,
    uint8_t* out,
    size_t cap) {
    if(!text || !out) return 0;
    size_t text_len = strlen(text);
    uint32_t msg_dest = dest;
    uint32_t msg_source = source;
    if(dest == NODENUM_BROADCAST) {
        msg_dest = 0;
        msg_source = 0;
    }
    return build_data_message(
        PORTNUM_TEXT_MESSAGE_APP,
        (const uint8_t*)text,
        text_len,
        want_response ? 1 : 0,
        msg_dest,
        msg_source,
        out,
        cap);
}

static size_t build_position_data_message(
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t cap) {
    if(!payload || !out || payload_len == 0) return 0;
    return build_data_message(
        PORTNUM_POSITION_APP,
        payload,
        payload_len,
        0,
        0,
        0,
        out,
        cap);
}

static size_t build_user_message(MeshtasticApp* app, uint8_t* out, size_t cap) {
    if(!app || !out) return 0;
    size_t off = 0;

    // field 1: id (string)
    size_t w = encode_key(1, 2, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    size_t id_len = strlen(app->self_id_str);
    w = encode_varint(id_len, out + off, cap - off);
    if(w == 0 || off + w + id_len > cap) return 0;
    off += w;
    memcpy(out + off, app->self_id_str, id_len);
    off += id_len;

    // field 2: long_name (string)
    w = encode_key(2, 2, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    size_t long_len = strlen(app->self_long_name);
    w = encode_varint(long_len, out + off, cap - off);
    if(w == 0 || off + w + long_len > cap) return 0;
    off += w;
    memcpy(out + off, app->self_long_name, long_len);
    off += long_len;

    // field 3: short_name (string)
    w = encode_key(3, 2, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    size_t short_len = strlen(app->self_short_name);
    w = encode_varint(short_len, out + off, cap - off);
    if(w == 0 || off + w + short_len > cap) return 0;
    off += w;
    memcpy(out + off, app->self_short_name, short_len);
    off += short_len;

    if(app->self_keys_valid) {
        // field 8: public_key (bytes)
        w = encode_key(8, 2, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
        w = encode_varint(sizeof(app->self_public_key), out + off, cap - off);
        if(w == 0 || off + w + sizeof(app->self_public_key) > cap) return 0;
        off += w;
        memcpy(out + off, app->self_public_key, sizeof(app->self_public_key));
        off += sizeof(app->self_public_key);
    }

    // field 7: role (varint)
    w = encode_key(7, 0, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(app->role_index, out + off, cap - off);
    if(w == 0) return 0;
    off += w;

    return off;
}

static size_t build_routing_ack_payload(uint8_t* out, size_t cap, uint32_t error_reason) {
    if(!out || cap < 2) return 0;
    size_t off = 0;
    size_t w = encode_key(3, 0, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(error_reason, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    return off;
}

static void write_u32_le(uint8_t* out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

static size_t encode_u32_field(uint32_t field, uint32_t value, uint8_t* out, size_t cap) {
    size_t off = 0;
    size_t w = encode_key(field, 0, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(value, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    return off;
}

static size_t build_device_metrics_telemetry(uint8_t* out, size_t cap) {
    if(!out || cap < 8) return 0;

    uint8_t device[32];
    size_t dev_len = 0;
    uint8_t battery = furi_hal_power_get_pct();
    if(battery > 100) battery = 100;

    size_t w = encode_u32_field(1, battery, device + dev_len, sizeof(device) - dev_len);
    if(w == 0) return 0;
    dev_len += w;

    w = encode_u32_field(5, furi_get_tick() / 1000, device + dev_len, sizeof(device) - dev_len);
    if(w == 0) return 0;
    dev_len += w;

    size_t off = 0;
    uint32_t ts = furi_hal_rtc_get_timestamp();
    if(ts != 0) {
        w = encode_u32_field(1, ts, out + off, cap - off);
        if(w == 0) return 0;
        off += w;
    }

    w = encode_key(2, 2, out + off, cap - off);
    if(w == 0) return 0;
    off += w;
    w = encode_varint(dev_len, out + off, cap - off);
    if(w == 0 || off + w + dev_len > cap) return 0;
    off += w;
    memcpy(out + off, device, dev_len);
    off += dev_len;

    return off;
}

static uint32_t meshtastic_generate_packet_id_local(void) {
    static uint32_t counter = 0;
    if(counter == 0) counter = (uint32_t)furi_get_tick();
    return ++counter;
}

static void meshtastic_update_inbox_label(MeshtasticApp* app) {
    if(!app || !app->submenu) return;
    submenu_change_item_label(
        app->submenu,
        MenuItemInbox,
        app->inbox_unread ? "Inbox *" : "Inbox");
}

static void meshtastic_update_dm_inbox_label(MeshtasticApp* app) {
    if(!app || !app->contacts_menu) return;
    submenu_change_item_label(
        app->contacts_menu,
        0,
        app->dm_inbox_unread ? "DM Inbox *" : "DM Inbox");
}

static void meshtastic_update_nodes_labels(MeshtasticApp* app) {
    if(!app || !app->submenu) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "Nodes (%u)", app->node_count);
    submenu_change_item_label(app->submenu, MenuItemNodes, buf);
    snprintf(buf, sizeof(buf), "Contacts (%u)", app->node_count);
    submenu_change_item_label(app->submenu, MenuItemContacts, buf);
}

static void meshtastic_update_weather_label(MeshtasticApp* app) {
    if(!app || !app->submenu) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "Weather (%u)", app->weather_count);
    submenu_change_item_label(app->submenu, MenuItemWeather, buf);
}

static void sort_nodes_by_last_heard(MeshtasticApp* app) {
    if(!app || app->node_count < 2) return;
    for(uint8_t i = 0; i < app->node_count - 1; i++) {
        for(uint8_t j = 0; j < app->node_count - 1 - i; j++) {
            NodeInfo* a = &app->nodes[j];
            NodeInfo* b = &app->nodes[j + 1];
            if(a->last_heard < b->last_heard) {
                NodeInfo tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
    }
}

static void nodes_menu_rebuild(MeshtasticApp* app) {
    if(!app || !app->nodes_menu) return;
    sort_nodes_by_last_heard(app);
    submenu_reset(app->nodes_menu);
    submenu_set_header(app->nodes_menu, "Nodes");
    for(uint8_t i = 0; i < app->node_count; i++) {
        NodeInfo* node = &app->nodes[i];
        char label[64];
        char name[48];
        char icon = node->has_public_key ? '*' : '-';
        get_node_display_name(app, node->node_id, name, sizeof(name));
        snprintf(label, sizeof(label), "%c %s", icon, name);
        submenu_add_item(app->nodes_menu, label, i, meshtastic_nodes_menu_callback, app);
    }
}

static void contacts_menu_rebuild(MeshtasticApp* app) {
    if(!app || !app->contacts_menu) return;
    sort_nodes_by_last_heard(app);
    submenu_reset(app->contacts_menu);
    submenu_set_header(app->contacts_menu, "Contacts");
    submenu_add_item(
        app->contacts_menu,
        app->dm_inbox_unread ? "DM Inbox *" : "DM Inbox",
        0,
        meshtastic_contacts_menu_callback,
        app);
    for(uint8_t i = 0; i < app->node_count; i++) {
        NodeInfo* node = &app->nodes[i];
        char label[64];
        char name[48];
        char icon = node->has_public_key ? '*' : '-';
        get_node_display_name(app, node->node_id, name, sizeof(name));
        snprintf(label, sizeof(label), "%c %s", icon, name);
        submenu_add_item(app->contacts_menu, label, (uint32_t)(i + 1), meshtastic_contacts_menu_callback, app);
    }
}

static bool log_name_is_log_file(const char* name) {
    if(!name || name[0] == '\0') return false;
    size_t len = strlen(name);
    return len > 4 && strcmp(name + len - 4, ".log") == 0;
}

static void logs_menu_rebuild(MeshtasticApp* app) {
    if(!app || !app->logs_menu) return;
    submenu_reset(app->logs_menu);
    submenu_set_header(app->logs_menu, "Logs");
    for(uint8_t i = 0; i < LogCategoryCount; i++) {
        submenu_add_item(app->logs_menu, log_category_labels[i], i, meshtastic_logs_menu_callback, app);
    }
}

static void log_files_menu_rebuild(MeshtasticApp* app) {
    if(!app || !app->log_files_menu || app->selected_log_category >= LogCategoryCount) return;
    submenu_reset(app->log_files_menu);
    submenu_set_header(app->log_files_menu, log_category_labels[app->selected_log_category]);
    app->log_file_count = 0;

    char dir[128];
    if(!log_category_path((MeshtasticLogCategory)app->selected_log_category, NULL, dir, sizeof(dir))) {
        return;
    }

    char latest[128];
    log_category_path((MeshtasticLogCategory)app->selected_log_category, "latest.log", latest, sizeof(latest));
    if(storage_file_exists(app->storage, latest)) {
        snprintf(app->log_file_names[app->log_file_count], sizeof(app->log_file_names[0]), "latest.log");
        submenu_add_item(
            app->log_files_menu,
            app->log_file_names[app->log_file_count],
            app->log_file_count,
            meshtastic_log_files_callback,
            app);
        app->log_file_count++;
    }

    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    if(storage_dir_open(file, dir)) {
        FileInfo info = {0};
        char name[40];
        while(app->log_file_count < LOG_BROWSER_MAX_FILES &&
              storage_dir_read(file, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info)) continue;
            if(strcmp(name, "latest.log") == 0) continue;
            if(!log_name_is_log_file(name)) continue;
            snprintf(
                app->log_file_names[app->log_file_count],
                sizeof(app->log_file_names[0]),
                "%s",
                name);
            submenu_add_item(
                app->log_files_menu,
                app->log_file_names[app->log_file_count],
                app->log_file_count,
                meshtastic_log_files_callback,
                app);
            app->log_file_count++;
        }
        storage_dir_close(file);
    }
    storage_file_free(file);

    if(app->log_file_count == 0) {
        submenu_add_item(app->log_files_menu, "No logs", 0, meshtastic_log_files_callback, app);
    }
}

static void log_text_open(MeshtasticApp* app, const char* filename) {
    if(!app || !filename || app->selected_log_category >= LogCategoryCount) return;
    memset(app->log_text_buffer, 0, sizeof(app->log_text_buffer));
    char path[128];
    if(!log_category_path((MeshtasticLogCategory)app->selected_log_category, filename, path, sizeof(path))) {
        return;
    }

    File* file = storage_file_alloc(app->storage);
    if(file && storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t read = storage_file_read(file, app->log_text_buffer, sizeof(app->log_text_buffer) - 1);
        app->log_text_buffer[read] = '\0';
        storage_file_close(file);
    } else {
        snprintf(app->log_text_buffer, sizeof(app->log_text_buffer), "Unable to open log.");
    }
    if(file) storage_file_free(file);

    text_box_reset(app->log_text_box);
    text_box_set_font(app->log_text_box, TextBoxFontText);
    text_box_set_text(app->log_text_box, app->log_text_buffer);
    text_box_set_focus(app->log_text_box, TextBoxFocusStart);
    meshtastic_switch_view(app, ViewIdLogText);
}

void meshtastic_update_node_info(
    MeshtasticApp* app,
    uint32_t node_id,
    const char* user_id,
    const char* long_name,
    const char* short_name,
    uint8_t role_index,
    const uint8_t* public_key,
    size_t public_key_len) {
    if(!app || node_id == 0) return;
    for(uint8_t i = 0; i < app->node_count; i++) {
        NodeInfo* node = &app->nodes[i];
        if(node->node_id == node_id) {
            if(user_id && user_id[0] != '\0') {
                meshtastic_sanitize_text(node->user_id, sizeof(node->user_id), user_id);
            }
            if(long_name && long_name[0] != '\0') {
                meshtastic_sanitize_text(node->long_name, sizeof(node->long_name), long_name);
            }
            if(short_name && short_name[0] != '\0') {
                meshtastic_sanitize_text(node->short_name, sizeof(node->short_name), short_name);
            }
            node->role_index = role_index;
            if(public_key && public_key_len == sizeof(node->public_key)) {
                memcpy(node->public_key, public_key, sizeof(node->public_key));
                node->has_public_key = true;
                node->pubkey_mode = 0;
                FURI_LOG_I("Proto", "Stored public key for node 0x%08lX", (unsigned long)node_id);
            }
            node->last_heard = furi_hal_rtc_get_timestamp();
            nodes_save(app);
            nodes_menu_rebuild(app);
            contacts_menu_rebuild(app);
            meshtastic_update_nodes_labels(app);
            return;
        }
    }
    if(app->node_count < MAX_NODES) {
        NodeInfo* node = &app->nodes[app->node_count++];
        memset(node, 0, sizeof(*node));
        node->node_id = node_id;
        if(user_id && user_id[0] != '\0') {
            meshtastic_sanitize_text(node->user_id, sizeof(node->user_id), user_id);
        }
        if(long_name && long_name[0] != '\0') {
            meshtastic_sanitize_text(node->long_name, sizeof(node->long_name), long_name);
        }
        if(short_name && short_name[0] != '\0') {
            meshtastic_sanitize_text(node->short_name, sizeof(node->short_name), short_name);
        }
        node->role_index = role_index;
        if(public_key && public_key_len == sizeof(node->public_key)) {
            memcpy(node->public_key, public_key, sizeof(node->public_key));
            node->has_public_key = true;
            node->pubkey_mode = 0;
            FURI_LOG_I("Proto", "Stored public key for node 0x%08lX", (unsigned long)node_id);
        }
        node->last_heard = furi_hal_rtc_get_timestamp();
        node->last_hops = 0xFF;
        node->last_rssi = 127;
        node->last_snr = 127;
        nodes_save(app);
        nodes_menu_rebuild(app);
        contacts_menu_rebuild(app);
        meshtastic_update_nodes_labels(app);
    }
}

void meshtastic_touch_node(MeshtasticApp* app, uint32_t node_id, uint8_t hops_used) {
    if(!app || node_id == 0) return;
    for(uint8_t i = 0; i < app->node_count; i++) {
        if(app->nodes[i].node_id == node_id) {
            app->nodes[i].last_heard = furi_hal_rtc_get_timestamp();
            if(hops_used != 0xFF) {
                if(app->nodes[i].last_hops == 0xFF || hops_used < app->nodes[i].last_hops) {
                    app->nodes[i].last_hops = hops_used;
                }
                if(hops_used == 0) {
                    app->nodes[i].last_rssi = app->last_rssi;
                    app->nodes[i].last_snr = app->last_snr;
                }
            }
            nodes_save(app);
            nodes_menu_rebuild(app);
            contacts_menu_rebuild(app);
            meshtastic_update_nodes_labels(app);
            return;
        }
    }
    if(app->node_count < MAX_NODES) {
        NodeInfo* node = &app->nodes[app->node_count++];
        memset(node, 0, sizeof(*node));
        node->node_id = node_id;
        snprintf(node->user_id, sizeof(node->user_id), "!%08lX", (unsigned long)node_id);
        node->last_heard = furi_hal_rtc_get_timestamp();
        node->last_hops = (hops_used != 0xFF) ? hops_used : 0xFF;
        if(hops_used == 0) {
            node->last_rssi = app->last_rssi;
            node->last_snr = app->last_snr;
        } else {
            node->last_rssi = 127;
            node->last_snr = 127;
        }
        nodes_save(app);
        nodes_menu_rebuild(app);
        contacts_menu_rebuild(app);
        meshtastic_update_nodes_labels(app);
    }
}

void meshtastic_store_weather(MeshtasticApp* app, uint32_t node_id, const WeatherInfo* weather) {
    if(!app || !weather || node_id == 0 || node_id == app->self_node_id) return;

    WeatherInfo* dst = NULL;
    for(uint8_t i = 0; i < app->weather_count; i++) {
        if(app->weather[i].node_id == node_id) {
            dst = &app->weather[i];
            break;
        }
    }

    if(!dst) {
        if(app->weather_count < MAX_WEATHER_NODES) {
            dst = &app->weather[app->weather_count++];
        } else {
            uint8_t oldest = 0;
            for(uint8_t i = 1; i < app->weather_count; i++) {
                if(app->weather[i].last_heard < app->weather[oldest].last_heard) oldest = i;
            }
            dst = &app->weather[oldest];
        }
    }

    WeatherInfo saved = *weather;
    saved.node_id = node_id;
    saved.last_heard = furi_hal_rtc_get_timestamp();
    saved.valid = 1;
    *dst = saved;

    weather_sort(app);
    weather_save(app);
    meshtastic_update_weather_label(app);
    if(app->current_view == ViewIdWeather) weather_redraw(app);
}

bool meshtastic_get_node_public_key(MeshtasticApp* app, uint32_t node_id, uint8_t* out, size_t out_len) {
    if(!app || !out || out_len < 32 || node_id == 0) return false;
    for(uint8_t i = 0; i < app->node_count; i++) {
        NodeInfo* node = &app->nodes[i];
        if(node->node_id == node_id && node->has_public_key) {
            memcpy(out, node->public_key, 32);
            return true;
        }
    }
    return false;
}

static NodeInfo* meshtastic_get_node_info(MeshtasticApp* app, uint32_t node_id) {
    if(!app || node_id == 0) return NULL;
    for(uint8_t i = 0; i < app->node_count; i++) {
        if(app->nodes[i].node_id == node_id) return &app->nodes[i];
    }
    return NULL;
}

static bool meshtastic_get_node_public_key_for_encrypt(
    MeshtasticApp* app,
    uint32_t node_id,
    uint8_t* out,
    size_t out_len) {
    if(!app || !out || out_len < 32 || node_id == 0) return false;
    NodeInfo* node = meshtastic_get_node_info(app, node_id);
    if(!node || !node->has_public_key) return false;
    memcpy(out, node->public_key, 32);
    if(node->pubkey_mode == 1) {
        for(size_t i = 0; i < 16; i++) {
            uint8_t tmp = out[i];
            out[i] = out[31 - i];
            out[31 - i] = tmp;
        }
    } else if(node->pubkey_mode == 2) {
        for(size_t i = 0; i < 32; i += 4) {
            uint8_t b0 = out[i + 0];
            uint8_t b1 = out[i + 1];
            uint8_t b2 = out[i + 2];
            uint8_t b3 = out[i + 3];
            out[i + 0] = b3;
            out[i + 1] = b2;
            out[i + 2] = b1;
            out[i + 3] = b0;
        }
    } else if(node->pubkey_mode == 3) {
        for(size_t i = 0; i < 4; i++) {
            size_t a = i * 4;
            size_t b = (7 - i) * 4;
            uint8_t t0 = out[a + 0];
            uint8_t t1 = out[a + 1];
            uint8_t t2 = out[a + 2];
            uint8_t t3 = out[a + 3];
            out[a + 0] = out[b + 0];
            out[a + 1] = out[b + 1];
            out[a + 2] = out[b + 2];
            out[a + 3] = out[b + 3];
            out[b + 0] = t0;
            out[b + 1] = t1;
            out[b + 2] = t2;
            out[b + 3] = t3;
        }
    }
    return true;
}

void meshtastic_set_node_pubkey_mode(MeshtasticApp* app, uint32_t node_id, uint8_t mode) {
    NodeInfo* node = meshtastic_get_node_info(app, node_id);
    if(!node) return;
    if(node->pubkey_mode != mode) {
        node->pubkey_mode = mode;
        nodes_save(app);
    }
}

static bool packet_cache_entry_expired(uint32_t now, uint32_t seen_at) {
    if(now == 0 || seen_at == 0) return false;
    return now > seen_at && (now - seen_at) >= PACKET_CACHE_TTL_SEC;
}

static bool packet_cache_prune(MeshtasticApp* app) {
    if(!app) return false;
    uint32_t now = furi_hal_rtc_get_timestamp();
    bool changed = false;
    for(uint8_t i = 0; i < app->packet_cache_count;) {
        if(packet_cache_entry_expired(now, app->packet_cache_seen_at[i])) {
            if(i + 1 < app->packet_cache_count) {
                memmove(
                    &app->packet_cache_from[i],
                    &app->packet_cache_from[i + 1],
                    sizeof(app->packet_cache_from[0]) * (app->packet_cache_count - i - 1));
                memmove(
                    &app->packet_cache_id[i],
                    &app->packet_cache_id[i + 1],
                    sizeof(app->packet_cache_id[0]) * (app->packet_cache_count - i - 1));
                memmove(
                    &app->packet_cache_seen_at[i],
                    &app->packet_cache_seen_at[i + 1],
                    sizeof(app->packet_cache_seen_at[0]) * (app->packet_cache_count - i - 1));
            }
            app->packet_cache_count--;
            changed = true;
        } else {
            i++;
        }
    }
    app->packet_cache_index = app->packet_cache_count % MAX_PACKET_CACHE;
    return changed;
}

static void packet_cache_save(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    bool saved = false;
    if(storage_file_open(file, PACKET_CACHE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        PacketCacheFileHeader hdr = {
            .magic = PACKET_CACHE_MAGIC,
            .version = PACKET_CACHE_VERSION,
            .count = app->packet_cache_count,
        };
        storage_file_write(file, &hdr, sizeof(hdr));
        for(uint8_t i = 0; i < app->packet_cache_count; i++) {
            PacketCacheRecord rec = {
                .from = app->packet_cache_from[i],
                .id = app->packet_cache_id[i],
                .seen_at = app->packet_cache_seen_at[i],
            };
            storage_file_write(file, &rec, sizeof(rec));
        }
        storage_file_close(file);
        saved = true;
    }
    storage_file_free(file);
    if(saved) {
        app->packet_cache_dirty = false;
        app->last_packet_cache_save_ms = furi_get_tick();
    }
}

static void packet_cache_load(MeshtasticApp* app) {
    if(!app || !app->storage) return;
    memset(app->packet_cache_from, 0, sizeof(app->packet_cache_from));
    memset(app->packet_cache_id, 0, sizeof(app->packet_cache_id));
    memset(app->packet_cache_seen_at, 0, sizeof(app->packet_cache_seen_at));
    app->packet_cache_count = 0;
    app->packet_cache_index = 0;
    app->packet_cache_dirty = false;
    app->last_packet_cache_save_ms = 0;
    if(!storage_file_exists(app->storage, PACKET_CACHE_PATH)) return;

    File* file = storage_file_alloc(app->storage);
    if(!file) return;
    bool changed = false;
    if(storage_file_open(file, PACKET_CACHE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        PacketCacheFileHeader hdr = {0};
        if(storage_file_read(file, &hdr, sizeof(hdr)) == sizeof(hdr) &&
           hdr.magic == PACKET_CACHE_MAGIC &&
           hdr.version == PACKET_CACHE_VERSION) {
            uint8_t count = hdr.count;
            if(count > MAX_PACKET_CACHE) {
                count = MAX_PACKET_CACHE;
                changed = true;
            }
            for(uint8_t i = 0; i < count; i++) {
                PacketCacheRecord rec = {0};
                if(storage_file_read(file, &rec, sizeof(rec)) != sizeof(rec)) break;
                app->packet_cache_from[app->packet_cache_count] = rec.from;
                app->packet_cache_id[app->packet_cache_count] = rec.id;
                app->packet_cache_seen_at[app->packet_cache_count] = rec.seen_at;
                app->packet_cache_count++;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);

    if(packet_cache_prune(app)) changed = true;
    if(changed) {
        app->packet_cache_dirty = true;
        packet_cache_save(app);
    }
}

bool meshtastic_packet_seen(MeshtasticApp* app, uint32_t from, uint32_t id) {
    if(!app) return false;
    bool pruned = packet_cache_prune(app);
    for(uint8_t i = 0; i < app->packet_cache_count; i++) {
        if(app->packet_cache_from[i] == from && app->packet_cache_id[i] == id) {
            if(pruned) packet_cache_save(app);
            return true;
        }
    }
    if(pruned) packet_cache_save(app);
    return false;
}

void meshtastic_packet_mark(MeshtasticApp* app, uint32_t from, uint32_t id) {
    if(!app) return;
    if(meshtastic_packet_seen(app, from, id)) return;

    uint8_t idx = 0;
    if(app->packet_cache_count < MAX_PACKET_CACHE) {
        idx = app->packet_cache_count++;
    } else {
        idx = app->packet_cache_index % MAX_PACKET_CACHE;
    }
    app->packet_cache_from[idx] = from;
    app->packet_cache_id[idx] = id;
    app->packet_cache_seen_at[idx] = furi_hal_rtc_get_timestamp();
    app->packet_cache_index = (uint8_t)((idx + 1) % MAX_PACKET_CACHE);
    app->packet_cache_dirty = true;
    uint32_t now = furi_get_tick();
    if(app->last_packet_cache_save_ms == 0 || now - app->last_packet_cache_save_ms >= 5000) {
        packet_cache_save(app);
    }
}

static void packet_cache_clear(MeshtasticApp* app) {
    if(!app) return;
    memset(app->packet_cache_from, 0, sizeof(app->packet_cache_from));
    memset(app->packet_cache_id, 0, sizeof(app->packet_cache_id));
    memset(app->packet_cache_seen_at, 0, sizeof(app->packet_cache_seen_at));
    app->packet_cache_count = 0;
    app->packet_cache_index = 0;
    app->packet_cache_dirty = true;
    app->packet_cache_loaded = true;
    packet_cache_save(app);
}

static uint32_t app_random_between(uint32_t min, uint32_t max) {
    if(max <= min) return min;
    return min + (furi_hal_random_get() % (max - min + 1));
}

static uint32_t relay_payload_hash(const uint8_t* packet, size_t len) {
    if(!packet || len == 0) return fnv1a32_bytes(NULL, 0);
    if(len > 16) return fnv1a32_bytes(packet + 16, len - 16);
    return fnv1a32_bytes(packet, len);
}

static bool relay_entry_matches(
    const PendingRelayPacket* relay,
    uint32_t from,
    uint32_t id,
    uint8_t channel,
    uint32_t payload_hash) {
    return relay && relay->active && relay->from == from && relay->id == id &&
           relay->channel == channel && relay->payload_hash == payload_hash;
}

void meshtastic_queue_relay_packet(
    MeshtasticApp* app,
    const uint8_t* packet,
    size_t len,
    uint32_t from,
    uint32_t id,
    uint8_t channel) {
    if(!app || !packet || len < 16 || len > 255) return;
    if(app->role_index != RoleClient) return;

    uint32_t hash = relay_payload_hash(packet, len);
    for(uint8_t i = 0; i < MAX_PENDING_RELAYS; i++) {
        if(relay_entry_matches(&app->pending_relays[i], from, id, channel, hash)) return;
    }

    PendingRelayPacket* slot = NULL;
    for(uint8_t i = 0; i < MAX_PENDING_RELAYS; i++) {
        if(!app->pending_relays[i].active) {
            slot = &app->pending_relays[i];
            break;
        }
    }
    if(!slot) {
        FURI_LOG_W(
            "Proto",
            "Rebroadcast dropped: queue full from=0x%08lX id=0x%08lX",
            (unsigned long)from,
            (unsigned long)id);
        meshtastic_log_event(
            app,
            LogCategoryRebroadcast,
            "drop queue_full from=0x%08lX id=0x%08lX",
            (unsigned long)from,
            (unsigned long)id);
        return;
    }

    uint32_t min_delay = app->rebroadcast_min_delay_ms;
    uint32_t max_delay = app->rebroadcast_max_delay_ms;
    if(max_delay < min_delay) max_delay = min_delay;
    uint32_t delay = app_random_between(min_delay, max_delay);
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->data, packet, len);
    slot->len = (uint8_t)len;
    slot->from = from;
    slot->id = id;
    slot->channel = channel;
    slot->payload_hash = hash;
    slot->queued_ms = furi_get_tick();
    slot->due_ms = slot->queued_ms + delay;
    slot->active = true;

    FURI_LOG_I(
        "Proto",
        "Rebroadcast queued from=0x%08lX id=0x%08lX delay=%lums",
        (unsigned long)from,
        (unsigned long)id,
        (unsigned long)delay);
    meshtastic_log_event(
        app,
        LogCategoryRebroadcast,
        "queued from=0x%08lX id=0x%08lX delay_ms=%lu",
        (unsigned long)from,
        (unsigned long)id,
        (unsigned long)delay);
}

void meshtastic_cancel_relay_packet(
    MeshtasticApp* app,
    const uint8_t* packet,
    size_t len,
    uint32_t from,
    uint32_t id,
    uint8_t channel) {
    if(!app || !packet || len == 0) return;
    uint32_t hash = relay_payload_hash(packet, len);
    for(uint8_t i = 0; i < MAX_PENDING_RELAYS; i++) {
        PendingRelayPacket* relay = &app->pending_relays[i];
        if(relay_entry_matches(relay, from, id, channel, hash)) {
            relay->active = false;
            FURI_LOG_I(
                "Proto",
                "Rebroadcast canceled due to duplicate heard from=0x%08lX id=0x%08lX",
                (unsigned long)from,
                (unsigned long)id);
            meshtastic_log_event(
                app,
                LogCategoryRebroadcast,
                "canceled duplicate from=0x%08lX id=0x%08lX",
                (unsigned long)from,
                (unsigned long)id);
        }
    }
}

void meshtastic_process_relay_queue(MeshtasticApp* app) {
    if(!app || app->state == AppStateError) return;
    if(app->role_index != RoleClient) {
        for(uint8_t i = 0; i < MAX_PENDING_RELAYS; i++) app->pending_relays[i].active = false;
        return;
    }

    uint32_t now = furi_get_tick();
    for(uint8_t i = 0; i < MAX_PENDING_RELAYS; i++) {
        PendingRelayPacket* relay = &app->pending_relays[i];
        if(!relay->active) continue;
        if((int32_t)(now - relay->due_ms) < 0) continue;

        uint8_t packet[256];
        uint8_t len = relay->len;
        memcpy(packet, relay->data, len);
        uint32_t from = relay->from;
        uint32_t id = relay->id;
        relay->active = false;

        if(rfm95w_send_packet(app, packet, len)) {
            app->packets_sent++;
            FURI_LOG_I(
                "Proto",
                "Rebroadcast TX done from=0x%08lX id=0x%08lX len=%u",
                (unsigned long)from,
                (unsigned long)id,
                (unsigned)len);
            meshtastic_log_event(
                app,
                LogCategoryRebroadcast,
                "tx_ok from=0x%08lX id=0x%08lX len=%u",
                (unsigned long)from,
                (unsigned long)id,
                (unsigned)len);
        } else {
            FURI_LOG_W(
                "Proto",
                "Rebroadcast dropped after polite TX wait from=0x%08lX id=0x%08lX",
                (unsigned long)from,
                (unsigned long)id);
            meshtastic_log_event(
                app,
                LogCategoryRebroadcast,
                "tx_drop from=0x%08lX id=0x%08lX",
                (unsigned long)from,
                (unsigned long)id);
        }
        return;
    }
}

static void meshtastic_switch_view(MeshtasticApp* app, uint32_t view_id) {
    if(!app || !app->view_dispatcher) return;
    if(view_id != ViewIdSettings) app->settings_hold_active = false;
    if(view_id != ViewIdSend) app->settings_keyboard_active = false;
    app->current_view = (uint8_t)view_id;
    if(view_id == ViewIdInbox) {
        app->inbox_unread = false;
        meshtastic_update_inbox_label(app);
    } else if(view_id == ViewIdDmInbox) {
        app->dm_inbox_unread = false;
        meshtastic_update_dm_inbox_label(app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, view_id);
}

static int message_list_find_index(Message* list, uint8_t count, uint32_t from, uint32_t id) {
    for(int i = (int)count - 1; i >= 0; i--) {
        if(list[i].from == from && list[i].id == id) return i;
    }
    return -1;
}

void meshtastic_mark_ack(MeshtasticApp* app, uint32_t request_id, bool ok, uint32_t ack_from) {
    UNUSED(ack_from);
    if(!app || request_id == 0) return;
    int idx = message_list_find_index(app->messages, app->msg_count, app->self_node_id, request_id);
    if(idx >= 0) {
        Message* msg = &app->messages[idx];
        if(msg->outgoing) {
            if(!ok) {
                msg->ack_state = AckStateFailed;
            } else {
                msg->ack_state = AckStateOk;
            }
        }
        messages_save(app);
        if(app->current_view == ViewIdInbox) inbox_redraw(app);
        return;
    }
    idx = message_list_find_index(app->dm_messages, app->dm_count, app->self_node_id, request_id);
    if(idx >= 0) {
        Message* msg = &app->dm_messages[idx];
        if(msg->outgoing) {
            if(!ok) {
                msg->ack_state = AckStateFailed;
            } else {
                msg->ack_state = AckStateOk;
            }
        }
        dm_messages_save(app);
        if(app->current_view == ViewIdDmInbox) dm_inbox_redraw(app);
    }
}

static void format_short_name(char* out, size_t cap, uint32_t from) {
    if(!out || cap < 5) return;
    snprintf(out, cap, "%04lX", (unsigned long)(from & 0xFFFF));
}

static void format_node_fallback_name(const NodeInfo* node, char* out, size_t cap) {
    if(!out || cap < 2) return;
    if(node && node->user_id[0] != '\0') {
        size_t len = strlen(node->user_id);
        const char* tail = node->user_id + (len > 4 ? len - 4 : 0);
        snprintf(out, cap, "Meshtastic %s", tail);
    } else if(node) {
        snprintf(out, cap, "Meshtastic %04lX", (unsigned long)(node->node_id & 0xFFFF));
    } else {
        snprintf(out, cap, "Meshtastic");
    }
}

static void get_node_short_name(MeshtasticApp* app, uint32_t node_id, char* out, size_t cap) {
    if(!out || cap < 2) return;
    if(app) {
        for(uint8_t i = 0; i < app->node_count; i++) {
            if(app->nodes[i].node_id == node_id && app->nodes[i].short_name[0] != '\0') {
                snprintf(out, cap, "%s", app->nodes[i].short_name);
                return;
            }
        }
    }
    format_short_name(out, cap, node_id);
}

static void get_node_display_name(MeshtasticApp* app, uint32_t node_id, char* out, size_t cap) {
    if(!out || cap < 2) return;
    if(app) {
        for(uint8_t i = 0; i < app->node_count; i++) {
            if(app->nodes[i].node_id == node_id) {
                if(app->nodes[i].long_name[0] != '\0') {
                    snprintf(out, cap, "%s", app->nodes[i].long_name);
                } else {
                    format_node_fallback_name(&app->nodes[i], out, cap);
                }
                return;
            }
        }
    }
    snprintf(out, cap, "Meshtastic %04lX", (unsigned long)(node_id & 0xFFFF));
}

static int wrap_next_len(Canvas* canvas, const char* text, int max_width) {
    if(!text || !canvas || max_width <= 0) return 0;
    const char* line_start = text;
    const char* last_space = NULL;
    const char* scan = text;
    int last_good = 0;
    bool overflowed = false;

    while(*scan) {
        if(*scan == ' ') last_space = scan;
        char tmp[128];
        int len = (int)(scan - line_start + 1);
        if(len >= (int)sizeof(tmp)) len = (int)sizeof(tmp) - 1;
        memcpy(tmp, line_start, len);
        tmp[len] = '\0';
        int width = canvas_string_width(canvas, tmp);
        if(width > max_width) {
            overflowed = true;
            break;
        }
        last_good = (int)(scan - line_start + 1);
        scan++;
    }

    if(last_good == 0) return 1;
    if(overflowed && last_space) {
        int space_len = (int)(last_space - line_start + 1);
        if(space_len > 0) return space_len;
    }
    return last_good;
}

static int wrap_count_lines(Canvas* canvas, const char* text, int max_width) {
    if(!text || !canvas || max_width <= 0) return 0;
    int lines = 0;
    const char* p = text;
    while(*p) {
        int len = wrap_next_len(canvas, p, max_width);
        p += len;
        while(*p == ' ') p++;
        lines++;
    }
    return lines;
}

static void draw_wrapped_lines(
    Canvas* canvas,
    const char* text,
    int max_width,
    int x,
    int y,
    int line_height) {
    if(!text || !canvas) return;
    const char* p = text;
    while(*p) {
        int len = wrap_next_len(canvas, p, max_width);
        char line[128];
        int copy_len = len;
        if(copy_len >= (int)sizeof(line)) copy_len = (int)sizeof(line) - 1;
        memcpy(line, p, copy_len);
        line[copy_len] = '\0';
        canvas_draw_str(canvas, x, y, line);
        y += line_height;
        p += len;
        while(*p == ' ') p++;
    }
}

static bool message_list_is_duplicate(
    Message* list,
    uint8_t count,
    uint32_t from,
    uint32_t id) {
    for(int i = (int)count - 1; i >= 0; i--) {
        Message* msg = &list[i];
        if(msg->from == from && msg->id == id) {
            return true;
        }
        if(count - i > 8) break;
    }
    return false;
}

static void store_message_internal(
    MeshtasticApp* app,
    Message* list,
    uint8_t* count,
    uint8_t* scroll,
    uint32_t from,
    uint32_t to,
    uint32_t id,
    const char* text,
    bool outgoing,
    bool is_dm) {
    if(!app || !text || text[0] == '\0') return;
    if(!outgoing && from == app->self_node_id) return;
    if(message_list_is_duplicate(list, *count, from, id)) return;
    uint8_t capacity = message_limit_for_app(app);
    trim_message_list_to_limit(list, count, scroll, capacity);

    bool at_bottom = false;
    if(*count <= INBOX_VISIBLE_LINES) {
        at_bottom = true;
    } else {
        at_bottom = (*scroll + INBOX_VISIBLE_LINES >= *count);
    }

    Message* msg = NULL;
    if(*count < capacity) {
        msg = &list[(*count)++];
    } else {
        memmove(&list[0], &list[1], sizeof(Message) * (capacity - 1));
        msg = &list[capacity - 1];
        *count = capacity;
    }

    size_t text_len = strlen(text);
    text_len = meshtastic_sanitize_text((char*)msg->payload, sizeof(msg->payload), text);
    msg->len = (uint8_t)text_len;
    msg->from = from;
    msg->to = to;
    msg->id = id;
    msg->rssi = app->last_rssi;
    msg->snr = app->last_snr;
    msg->timestamp = furi_get_tick();
    msg->outgoing = outgoing;
    msg->is_dm = is_dm;
    msg->ack_state = outgoing ? AckStatePending : AckStateNone;
    msg->retries = 0;
    msg->last_tx_ms = 0;

    if(!outgoing) {
        meshtastic_log_event(
            app,
            LogCategoryRx,
            "%s from=0x%08lX to=0x%08lX id=0x%08lX text=\"%s\"",
            is_dm ? "dm" : "broadcast",
            (unsigned long)from,
            (unsigned long)to,
            (unsigned long)id,
            (char*)msg->payload);
    }

    if(!is_dm) {
        msg->unread = (app->current_view != ViewIdInbox);
        if(msg->unread) {
            app->inbox_unread = true;
            meshtastic_update_inbox_label(app);
        }
    } else {
        msg->unread = (app->current_view != ViewIdDmInbox);
        if(msg->unread) {
            app->dm_inbox_unread = true;
            meshtastic_update_dm_inbox_label(app);
        }
    }

    if(at_bottom) {
        if(*count > INBOX_VISIBLE_LINES) {
            *scroll = *count - INBOX_VISIBLE_LINES;
        } else {
            *scroll = 0;
        }
    }
}

void meshtastic_store_message(
    MeshtasticApp* app,
    uint32_t from,
    uint32_t to,
    uint32_t id,
    const char* text,
    bool outgoing,
    bool is_dm) {
    if(is_dm) {
        store_message_internal(
            app,
            app->dm_messages,
            &app->dm_count,
            &app->dm_inbox_scroll,
            from,
            to,
            id,
            text,
            outgoing,
            true);
        dm_messages_save(app);
        meshtastic_update_dm_inbox_label(app);
        if(app->current_view == ViewIdDmInbox) dm_inbox_redraw(app);
    } else {
        store_message_internal(
            app,
            app->messages,
            &app->msg_count,
            &app->inbox_scroll,
            from,
            to,
            id,
            text,
            outgoing,
            false);
        messages_save(app);
        meshtastic_update_inbox_label(app);
        if(app->current_view == ViewIdInbox) inbox_redraw(app);
    }
}

static bool build_meshtastic_text_packet(
    MeshtasticApp* app,
    const char* text,
    uint32_t to,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len,
    uint32_t* out_id,
    uint32_t forced_id) {
    if(!out || !out_len || out_cap < 32) return false;

    uint32_t from = app ? app->self_node_id : meshtastic_get_node_id();
    bool is_dm = (to != NODENUM_BROADCAST);
    uint8_t data[256];
    size_t data_len = build_text_data_message(text, to, from, is_dm, data, sizeof(data));
    if(data_len == 0) return false;
    uint32_t id = forced_id ? forced_id : meshtastic_generate_packet_id_local();
    bool use_pki = false;
    bool can_pki = is_dm && app && app->self_keys_valid;
    uint8_t enc[256];
    size_t enc_len = 0;
    uint8_t chash = app ? app->channel_hash : MESHTASTIC_DEFAULT_CHASH;
    if(can_pki) {
        uint8_t remote_pub[32];
        if(meshtastic_get_node_public_key_for_encrypt(app, to, remote_pub, sizeof(remote_pub))) {
            use_pki = true;
            if(!crypto_pki_encrypt(
                   to,
                   from,
                   id,
                   remote_pub,
                   app->self_private_key,
                   data,
                   data_len,
                   enc,
                   sizeof(enc),
                   &enc_len)) {
                return false;
            }
            chash = 0x00;
            FURI_LOG_I("Proto", "DM PKI encrypt to 0x%08lX", (unsigned long)to);
        }
    }

    if(!use_pki) {
        if(is_dm && can_pki) {
            FURI_LOG_W("Proto", "No public key for node 0x%08lX, sending legacy DM", (unsigned long)to);
        }
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
        if(key.length <= 0) return false;
        memcpy(enc, data, data_len);
        enc_len = data_len;
        crypto_encrypt_packet(key, from, id, enc_len, enc);
        if(is_dm) {
            FURI_LOG_I("Proto", "DM legacy encrypt to 0x%08lX", (unsigned long)to);
        }
    }

    size_t total = 16 + enc_len;
    if(total > out_cap) return false;

    bool want_ack = is_dm;
    uint8_t hop = (app ? app->max_hops : DEFAULT_MAX_HOPS) & PACKET_FLAGS_HOP_LIMIT_MASK;
    uint8_t hop_start = hop;
    uint8_t flags = hop;
    flags |= (want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0);
    flags |= (uint8_t)((hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);
    write_u32_le(out + 0, to);
    write_u32_le(out + 4, from);
    write_u32_le(out + 8, id);
    out[12] = flags;
    out[13] = chash;
    out[14] = 0x00; // next_hop (no preference)
    out[15] = 0x00; // relay_node (unset)

    memcpy(out + 16, enc, enc_len);
    *out_len = total;
    if(out_id) *out_id = id;
    return true;
}

static bool build_meshtastic_ack_packet(
    MeshtasticApp* app,
    uint32_t to,
    uint32_t request_id,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!app || !out || !out_len || request_id == 0 || out_cap < 32) return false;

    uint8_t routing[32];
    size_t routing_len = build_routing_ack_payload(routing, sizeof(routing), 0);
    if(routing_len == 0) return false;

    uint8_t data[64];
    size_t data_len = build_data_message(
        PORTNUM_ROUTING_APP,
        routing,
        routing_len,
        request_id,
        0,
        0,
        data,
        sizeof(data));
    if(data_len == 0) return false;

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
    if(key.length <= 0) return false;

    uint32_t from = app->self_node_id;
    uint32_t id = meshtastic_generate_packet_id_local();

    crypto_encrypt_packet(key, from, id, data_len, data);

    size_t total = 16 + data_len;
    if(total > out_cap) return false;

    uint8_t hop = (app ? app->max_hops : DEFAULT_MAX_HOPS) & PACKET_FLAGS_HOP_LIMIT_MASK;
    uint8_t hop_start = hop;
    uint8_t flags = hop;
    flags |= (uint8_t)((hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);

    write_u32_le(out + 0, to);
    write_u32_le(out + 4, from);
    write_u32_le(out + 8, id);
    out[12] = flags;
    out[13] = app ? app->channel_hash : MESHTASTIC_DEFAULT_CHASH;
    out[14] = 0x00; // next_hop (no preference)
    out[15] = 0x00; // relay_node (unset)

    memcpy(out + 16, data, data_len);
    *out_len = total;
    return true;
}

static bool build_meshtastic_device_metrics_packet(
    MeshtasticApp* app,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!app || !out || !out_len || out_cap < 32) return false;

    uint8_t telemetry[64];
    size_t telemetry_len = build_device_metrics_telemetry(telemetry, sizeof(telemetry));
    if(telemetry_len == 0) return false;

    uint8_t data[128];
    size_t data_len = build_data_message(
        PORTNUM_TELEMETRY_APP,
        telemetry,
        telemetry_len,
        0,
        0,
        0,
        data,
        sizeof(data));
    if(data_len == 0) return false;

    uint8_t psk_bytes[32] = {0};
    size_t psk_len = 0;
    if(app->channel_psk_len > 0) {
        memcpy(psk_bytes, app->channel_psk, app->channel_psk_len);
        psk_len = app->channel_psk_len;
    } else {
        psk_bytes[0] = 0x01;
        psk_len = 1;
    }
    CryptoKey key = crypto_expand_psk(psk_bytes, psk_len);
    if(key.length <= 0) return false;

    uint32_t from = app->self_node_id;
    uint32_t to = NODENUM_BROADCAST;
    uint32_t id = meshtastic_generate_packet_id_local();
    crypto_encrypt_packet(key, from, id, data_len, data);

    size_t total = 16 + data_len;
    if(total > out_cap) return false;

    uint8_t hop = app->max_hops & PACKET_FLAGS_HOP_LIMIT_MASK;
    uint8_t flags = hop;
    flags |= (uint8_t)((hop << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);
    write_u32_le(out + 0, to);
    write_u32_le(out + 4, from);
    write_u32_le(out + 8, id);
    out[12] = flags;
    out[13] = app->channel_hash;
    out[14] = 0x00;
    out[15] = 0x00;
    memcpy(out + 16, data, data_len);
    *out_len = total;
    return true;
}

static bool meshtastic_send_device_metrics(MeshtasticApp* app) {
    if(!app) return false;
    uint8_t packet[160];
    size_t packet_len = 0;
    if(!build_meshtastic_device_metrics_packet(app, packet, sizeof(packet), &packet_len)) {
        FURI_LOG_W("Proto", "Failed to build DeviceMetrics packet");
        return false;
    }
    if(rfm95w_send_packet(app, packet, packet_len)) {
        app->packets_sent++;
        FURI_LOG_I("Proto", "DeviceMetrics TX done, len=%u", (unsigned)packet_len);
        meshtastic_log_event(app, LogCategoryTelemetry, "devicemetrics tx_ok len=%u", (unsigned)packet_len);
        return true;
    }
    FURI_LOG_W("Proto", "DeviceMetrics TX failed");
    meshtastic_log_event(
        app,
        LogCategoryTelemetry,
        "devicemetrics tx_failed busy_timeout=%u",
        app->last_tx_busy_timeout ? 1u : 0u);
    return false;
}

static bool build_meshtastic_position_packet_from_payload(
    MeshtasticApp* app,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!app || !payload || !out || !out_len || out_cap < 32) return false;

    uint32_t from = app->self_node_id;
    uint32_t to = NODENUM_BROADCAST;
    uint8_t data[256];
    size_t data_len = build_position_data_message(payload, payload_len, data, sizeof(data));
    if(data_len == 0) return false;
    uint32_t id = meshtastic_generate_packet_id_local();

    uint8_t psk_bytes[32] = {0};
    size_t psk_len = 0;
    if(app->channel_psk_len > 0) {
        memcpy(psk_bytes, app->channel_psk, app->channel_psk_len);
        psk_len = app->channel_psk_len;
    } else {
        psk_bytes[0] = 0x01;
        psk_len = 1;
    }
    CryptoKey key = crypto_expand_psk(psk_bytes, psk_len);
    if(key.length <= 0) return false;

    uint8_t enc[256];
    memcpy(enc, data, data_len);
    crypto_encrypt_packet(key, from, id, data_len, enc);

    size_t total = 16 + data_len;
    if(total > out_cap) return false;

    uint8_t hop = (app->max_hops) & PACKET_FLAGS_HOP_LIMIT_MASK;
    uint8_t hop_start = hop;
    uint8_t flags = hop;
    flags |= (uint8_t)((hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);
    out[0] = (uint8_t)(to & 0xFF);
    out[1] = (uint8_t)((to >> 8) & 0xFF);
    out[2] = (uint8_t)((to >> 16) & 0xFF);
    out[3] = (uint8_t)((to >> 24) & 0xFF);
    out[4] = (uint8_t)(from & 0xFF);
    out[5] = (uint8_t)((from >> 8) & 0xFF);
    out[6] = (uint8_t)((from >> 16) & 0xFF);
    out[7] = (uint8_t)((from >> 24) & 0xFF);
    out[8] = (uint8_t)(id & 0xFF);
    out[9] = (uint8_t)((id >> 8) & 0xFF);
    out[10] = (uint8_t)((id >> 16) & 0xFF);
    out[11] = (uint8_t)((id >> 24) & 0xFF);
    out[12] = flags;
    out[13] = app->channel_hash;
    out[14] = 0x00;
    out[15] = 0x00;

    memcpy(out + 16, enc, data_len);
    *out_len = total;
    return true;
}

bool meshtastic_send_ack(MeshtasticApp* app, uint32_t to, uint32_t request_id) {
    if(!app || request_id == 0) return false;
    uint8_t packet[128];
    size_t packet_len = 0;
    if(!build_meshtastic_ack_packet(app, to, request_id, packet, sizeof(packet), &packet_len))
        return false;
    bool ok = rfm95w_send_packet(app, packet, packet_len);
    meshtastic_log_event(
        app,
        LogCategoryTx,
        "ack %s to=0x%08lX request=0x%08lX len=%u",
        ok ? "tx_ok" : "tx_failed",
        (unsigned long)to,
        (unsigned long)request_id,
        (unsigned)packet_len);
    return ok;
}

static bool build_meshtastic_nodeinfo_packet_to(
    MeshtasticApp* app,
    uint32_t to,
    bool want_response,
    uint8_t* out,
    size_t out_len,
    size_t* out_used);

bool meshtastic_send_nodeinfo(MeshtasticApp* app, uint32_t to, bool want_response) {
    if(!app) return false;
    uint8_t packet[256];
    size_t packet_len = 0;
    if(!build_meshtastic_nodeinfo_packet_to(app, to, want_response, packet, sizeof(packet), &packet_len))
        return false;
    if(rfm95w_send_packet(app, packet, packet_len)) {
        FURI_LOG_I(
            "Proto",
            "NodeInfo TX to=0x%08lX len=%u",
            (unsigned long)to,
            (unsigned)packet_len);
        meshtastic_log_event(
            app,
            LogCategoryNodeInfo,
            "tx_ok to=0x%08lX want_response=%u len=%u",
            (unsigned long)to,
            want_response ? 1u : 0u,
            (unsigned)packet_len);
        meshtastic_send_device_metrics(app);
        return true;
    }
    meshtastic_log_event(
        app,
        LogCategoryNodeInfo,
        "tx_failed to=0x%08lX want_response=%u busy_timeout=%u",
        (unsigned long)to,
        want_response ? 1u : 0u,
        app->last_tx_busy_timeout ? 1u : 0u);
    return false;
}

static bool build_meshtastic_nodeinfo_packet_to(
    MeshtasticApp* app,
    uint32_t to,
    bool want_response,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!app || !out || !out_len || out_cap < 32) return false;

    uint8_t user[256];
    size_t user_len = build_user_message(app, user, sizeof(user));
    if(user_len == 0) return false;

    uint8_t data[256];
    size_t data_len = build_data_message_with_response(
        PORTNUM_NODEINFO_APP,
        user,
        user_len,
        want_response,
        0,
        0,
        0,
        data,
        sizeof(data));
    if(data_len == 0) return false;

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
    if(key.length <= 0) return false;

    uint32_t from = app->self_node_id;
    uint32_t id = meshtastic_generate_packet_id_local();

    crypto_encrypt_packet(key, from, id, data_len, data);

    size_t total = 16 + data_len;
    if(total > out_cap) return false;

    uint8_t hop = (app ? app->max_hops : DEFAULT_MAX_HOPS) & PACKET_FLAGS_HOP_LIMIT_MASK;
    uint8_t hop_start = hop;
    uint8_t flags = hop;
    flags |= (uint8_t)((hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);
    write_u32_le(out + 0, to);
    write_u32_le(out + 4, from);
    write_u32_le(out + 8, id);
    out[12] = flags;
    out[13] = app ? app->channel_hash : MESHTASTIC_DEFAULT_CHASH;
    out[14] = 0x00; // next_hop (no preference)
    out[15] = 0x00; // relay_node (unset)

    memcpy(out + 16, data, data_len);
    *out_len = total;
    return true;
}

static bool build_meshtastic_nodeinfo_packet(
    MeshtasticApp* app,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    return build_meshtastic_nodeinfo_packet_to(
        app,
        NODENUM_BROADCAST,
        false,
        out,
        out_cap,
        out_len);
}

static const char* wiring_guide[] = {
    "Hardware SPI wiring",
    "MOSI -> A7(2)",
    "MISO -> A6(3)",
    "CS -> A4(4)",
    "SCK -> B3(5)",
    "G0 -> B2(6)",
    "GND -> GND(8)",
    "VCC -> 3V3(9)",
    "RegVersion 0x00:",
    "check CS/MISO/power",
    "Back exits",
    NULL,
};

static void meshtastic_error_menu_callback(void* ctx, uint32_t index) {
    UNUSED(index);
    MeshtasticApp* app = ctx;
    if(app && app->view_dispatcher) {
        view_dispatcher_stop(app->view_dispatcher);
    }
}

static void radio_error_menu_rebuild(MeshtasticApp* app) {
    if(!app || !app->submenu) return;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "RFM95W not found");
    for(uint8_t i = 0; wiring_guide[i]; i++) {
        submenu_add_item(app->submenu, wiring_guide[i], i, meshtastic_error_menu_callback, app);
    }
}

static void meshtastic_inbox_draw(Canvas* canvas, void* model) {
    MeshtasticViewModel* vm = model;
    MeshtasticApp* app = vm ? vm->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Inbox");

    canvas_set_font(canvas, FontSecondary);
    uint8_t start = app->inbox_scroll;
    if(start >= app->msg_count) start = app->msg_count ? app->msg_count - 1 : 0;

    int y = INBOX_TOP_Y;
    for(uint8_t i = start; i < app->msg_count; i++) {
        Message* msg = &app->messages[i];
        char short_name[8];
        if(msg->outgoing) {
            char ack = '-';
            if(msg->ack_state == AckStateOk || msg->ack_state == AckStateRelay) ack = '+';
            else if(msg->ack_state == AckStateFailed) ack = '=';
            else if(msg->ack_state == AckStateBusy) ack = '/';
            snprintf(short_name, sizeof(short_name), "%c%s", ack, app->self_short_name);
        } else {
            get_node_short_name(app, msg->from, short_name, sizeof(short_name));
        }

        char line[128];
        snprintf(line, sizeof(line), "%s - %s", short_name, (char*)msg->payload);
        int max_width = (int)canvas_width(canvas) - 4;
        int lines = wrap_count_lines(canvas, line, max_width);
        if(lines <= 0) lines = 1;
        int bubble_height = lines * INBOX_LINE_HEIGHT;
        if(y + bubble_height > INBOX_BOTTOM_Y) break;

        if(msg->outgoing) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 8, 128, bubble_height + 1);
            canvas_set_color(canvas, ColorWhite);
            draw_wrapped_lines(canvas, line, max_width, 2, y, INBOX_LINE_HEIGHT);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_frame(canvas, 0, y - 8, 128, bubble_height + 1);
            draw_wrapped_lines(canvas, line, max_width, 2, y, INBOX_LINE_HEIGHT);
        }

        y += bubble_height + 2;
    }
}

static void meshtastic_dm_inbox_draw(Canvas* canvas, void* model) {
    MeshtasticViewModel* vm = model;
    MeshtasticApp* app = vm ? vm->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "DM Inbox");

    canvas_set_font(canvas, FontSecondary);
    uint8_t start = app->dm_inbox_scroll;
    if(start >= app->dm_count) start = app->dm_count ? app->dm_count - 1 : 0;

    int y = INBOX_TOP_Y;
    for(uint8_t i = start; i < app->dm_count; i++) {
        Message* msg = &app->dm_messages[i];
        char short_name[8];
        if(msg->outgoing) {
            char ack = '-';
            if(msg->ack_state == AckStateOk || msg->ack_state == AckStateRelay) ack = '+';
            else if(msg->ack_state == AckStateFailed) ack = '=';
            else if(msg->ack_state == AckStateBusy) ack = '/';
            snprintf(short_name, sizeof(short_name), "%c%s", ack, app->self_short_name);
        } else {
            get_node_short_name(app, msg->from, short_name, sizeof(short_name));
        }

        char target[8];
        if(msg->outgoing) {
            get_node_short_name(app, msg->to, target, sizeof(target));
        } else {
            target[0] = '\0';
        }

        char line[160];
        if(msg->outgoing) {
            snprintf(line, sizeof(line), "%s - %s - %s", short_name, target, (char*)msg->payload);
        } else {
            snprintf(line, sizeof(line), "%s - %s", short_name, (char*)msg->payload);
        }

        int max_width = (int)canvas_width(canvas) - 4;
        int lines = wrap_count_lines(canvas, line, max_width);
        if(lines <= 0) lines = 1;
        int bubble_height = lines * INBOX_LINE_HEIGHT;
        if(y + bubble_height > INBOX_BOTTOM_Y) break;

        if(msg->outgoing) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 8, 128, bubble_height + 1);
            canvas_set_color(canvas, ColorWhite);
            draw_wrapped_lines(canvas, line, max_width, 2, y, INBOX_LINE_HEIGHT);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_frame(canvas, 0, y - 8, 128, bubble_height + 1);
            draw_wrapped_lines(canvas, line, max_width, 2, y, INBOX_LINE_HEIGHT);
        }

        y += bubble_height + 2;
    }
}

static void dm_inbox_redraw(MeshtasticApp* app) {
    if(!app || !app->dm_inbox_view) return;
    with_view_model(
        app->dm_inbox_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        true);
}

static bool meshtastic_dm_inbox_input(InputEvent* event, void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyUp) {
        if(app->dm_inbox_scroll > 0) {
            app->dm_inbox_scroll--;
            dm_inbox_redraw(app);
        }
        return true;
    }
    if(event->key == InputKeyDown) {
        if(app->dm_inbox_scroll + 1 < app->dm_count) {
            app->dm_inbox_scroll++;
            dm_inbox_redraw(app);
        }
        return true;
    }
    if(event->key == InputKeyBack) {
        meshtastic_switch_view(app, ViewIdContacts);
        return true;
    }
    return false;
}

static const char* signal_quality_label(int8_t rssi, int8_t snr) {
    if(rssi >= -80 && snr >= 5) return "good";
    if(rssi >= -100 && snr >= 0) return "medium";
    return "bad";
}

static void meshtastic_node_detail_draw(Canvas* canvas, void* model) {
    MeshtasticViewModel* vm = model;
    MeshtasticApp* app = vm ? vm->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Node");
    canvas_set_font(canvas, FontSecondary);

    if(app->selected_node_index >= app->node_count) return;
    NodeInfo* node = &app->nodes[app->selected_node_index];
    char line[64];
    if(node->last_hops == 0) {
        if(node->last_rssi == 127) {
            snprintf(line, sizeof(line), "Signal: ? -- rssi=?");
        } else {
            const char* label = signal_quality_label(node->last_rssi, node->last_snr);
            snprintf(line, sizeof(line), "Signal: %s -- rssi=%d dBm", label, (int)node->last_rssi);
        }
    } else if(node->last_hops == 0xFF) {
        snprintf(line, sizeof(line), "Hops: ?");
    } else {
        snprintf(line, sizeof(line), "Hops: %u", (unsigned)node->last_hops);
    }
    canvas_draw_str(canvas, 2, 24, line);
    snprintf(line, sizeof(line), "ID: %s", node->user_id[0] ? node->user_id : "unknown");
    canvas_draw_str(canvas, 2, 36, line);
    snprintf(line, sizeof(line), "Role: %s", role_label_from_index(node->role_index));
    canvas_draw_str(canvas, 2, 48, line);
    uint32_t now = furi_hal_rtc_get_timestamp();
    uint32_t age = (node->last_heard > 0 && now > node->last_heard) ? (now - node->last_heard) : 0;
    snprintf(line, sizeof(line), "Last: %us", (unsigned)age);
    canvas_draw_str(canvas, 2, 60, line);
}

static bool meshtastic_node_detail_input(InputEvent* event, void* ctx) {
    MeshtasticApp* app = ctx;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat &&
       event->type != InputTypePress)
        return false;
    if(event->key == InputKeyBack) {
        meshtastic_switch_view(app, ViewIdNodes);
        return true;
    }
    return false;
}

static float weather_dew_point_c(float temperature_c, float humidity) {
    if(humidity < 0.0f) humidity = 0.0f;
    if(humidity > 100.0f) humidity = 100.0f;
    return temperature_c - ((100.0f - humidity) / 5.0f);
}

static float weather_display_temp(MeshtasticApp* app, float temperature_c) {
    return (app && app->weather_fahrenheit) ? ((temperature_c * 9.0f / 5.0f) + 32.0f) : temperature_c;
}

static char weather_temp_unit(MeshtasticApp* app) {
    return (app && app->weather_fahrenheit) ? 'F' : 'C';
}

static const char* weather_direction_label(uint16_t degrees) {
    static const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return dirs[((degrees + 22) % 360) / 45];
}

static const char* weather_condition_label(const WeatherInfo* weather) {
    if(!weather) return "?";
    float humidity = weather->has_humidity ? weather->humidity : -1.0f;
    float pressure = weather->has_pressure ? weather->pressure : 1013.25f;
    float wind = weather->has_wind_speed ? weather->wind_speed : 0.0f;
    float gust = weather->has_wind_gust ? weather->wind_gust : wind;

    if(gust >= 17.0f || wind >= 12.0f) return "Windy";
    if(weather->has_pressure && pressure < 1000.0f) return "Stormy";
    if(weather->has_humidity && humidity >= 90.0f) return "Rainy";
    if((weather->has_humidity && humidity >= 70.0f) || (weather->has_pressure && pressure < 1013.0f)) return "Cloudy";
    if(weather->has_pressure && pressure >= 1018.0f && (!weather->has_humidity || humidity < 65.0f)) return "Sunny";
    return "Clear";
}

static void weather_redraw(MeshtasticApp* app) {
    if(!app || !app->weather_view) return;
    with_view_model(
        app->weather_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        true);
}

static void meshtastic_weather_draw(Canvas* canvas, void* model) {
    MeshtasticViewModel* vm = model;
    MeshtasticApp* app = vm ? vm->app : NULL;
    if(!app) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Weather");
    canvas_set_font(canvas, FontSecondary);

    if(app->weather_count == 0) {
        canvas_draw_str(canvas, 2, 28, "No telemetry yet");
        canvas_draw_str(canvas, 2, 40, "Waiting for port 67");
        return;
    }

    if(app->weather_scroll >= app->weather_count) app->weather_scroll = app->weather_count - 1;
    WeatherInfo* weather = &app->weather[app->weather_scroll];

    char name[48];
    get_node_display_name(app, weather->node_id, name, sizeof(name));
    char line[96];
    snprintf(
        line,
        sizeof(line),
        "%u/%u %.32s",
        (unsigned)(app->weather_scroll + 1),
        (unsigned)app->weather_count,
        name);
    canvas_draw_str(canvas, 2, 20, line);

    if(weather->has_temperature) {
        snprintf(
            line,
            sizeof(line),
            "%s %.1f%c",
            weather_condition_label(weather),
            (double)weather_display_temp(app, weather->temperature),
            weather_temp_unit(app));
    } else {
        snprintf(line, sizeof(line), "%s", weather_condition_label(weather));
    }
    canvas_draw_str(canvas, 2, 30, line);

    if(weather->has_humidity && weather->has_temperature) {
        snprintf(
            line,
            sizeof(line),
            "Hum %.0f%% Dew %.1f%c",
            (double)weather->humidity,
            (double)weather_display_temp(app, weather_dew_point_c(weather->temperature, weather->humidity)),
            weather_temp_unit(app));
    } else if(weather->has_humidity) {
        snprintf(line, sizeof(line), "Hum %.0f%% Dew ?", (double)weather->humidity);
    } else {
        snprintf(line, sizeof(line), "Hum ? Dew ?");
    }
    canvas_draw_str(canvas, 2, 40, line);

    if(weather->has_pressure) {
        snprintf(line, sizeof(line), "Pressure %.1f hPa", (double)weather->pressure);
    } else {
        snprintf(line, sizeof(line), "Pressure ?");
    }
    canvas_draw_str(canvas, 2, 50, line);

    if(weather->has_wind_speed || weather->has_wind_gust || weather->has_wind_direction) {
        char dir[8] = "?";
        if(weather->has_wind_direction) {
            snprintf(
                dir,
                sizeof(dir),
                "%s",
                weather_direction_label((uint16_t)(weather->wind_direction % 360)));
        }
        snprintf(
            line,
            sizeof(line),
            "Wind %.1f G%.1f %s",
            (double)(weather->has_wind_speed ? weather->wind_speed : 0.0f),
            (double)(weather->has_wind_gust ? weather->wind_gust : 0.0f),
            dir);
    } else {
        snprintf(line, sizeof(line), "Wind ?");
    }
    canvas_draw_str(canvas, 2, 60, line);
}

static bool meshtastic_weather_input(InputEvent* event, void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyUp) {
        if(app->weather_scroll > 0) {
            app->weather_scroll--;
            weather_redraw(app);
        }
        return true;
    }
    if(event->key == InputKeyDown) {
        if(app->weather_scroll + 1 < app->weather_count) {
            app->weather_scroll++;
            weather_redraw(app);
        }
        return true;
    }
    if(event->key == InputKeyBack) {
        meshtastic_switch_view(app, ViewIdMenu);
        return true;
    }
    return false;
}

static void inbox_redraw(MeshtasticApp* app) {
    if(!app || !app->inbox_view) return;
    with_view_model(
        app->inbox_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        true);
}

static bool meshtastic_inbox_input(InputEvent* event, void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyUp) {
        if(app->inbox_scroll > 0) {
            app->inbox_scroll--;
            inbox_redraw(app);
        }
        return true;
    }
    if(event->key == InputKeyDown) {
        if(app->inbox_scroll + 1 < app->msg_count) {
            app->inbox_scroll++;
            inbox_redraw(app);
        }
        return true;
    }
    if(event->key == InputKeyBack) {
        meshtastic_switch_view(app, ViewIdMenu);
        return true;
    }
    return false;
}

static uint8_t settings_active_count(MeshtasticApp* app) {
    if(app && app->settings_log_clear) return LogCategoryCount + 2;
    return (app && app->settings_advanced) ? PoliteItemCount : SettingItemCount;
}

static bool settings_is_cycleable(MeshtasticApp* app, uint32_t item) {
    if(app && app->settings_log_clear) {
        UNUSED(item);
        return false;
    }
    if(app && app->settings_advanced) {
        return item != PoliteItemBack;
    }

    switch(item) {
    case SettingItemMaxHops:
    case SettingItemFreqSlot:
    case SettingItemPreset:
    case SettingItemTxPower:
    case SettingItemRole:
    case SettingItemNodeinfoInterval:
    case SettingItemMessageLimit:
    case SettingItemTemperatureUnit:
    case SettingItemChannelPreset:
        return true;
    default:
        return false;
    }
}

static const char* settings_bool_label(bool value) {
    return value ? "On" : "Off";
}

static void settings_get_label(MeshtasticApp* app, uint32_t item, char* out, size_t out_size) {
    if(!out || out_size == 0) return;
    out[0] = '\0';
    if(app && app->settings_log_clear) {
        if(item < LogCategoryCount) {
            snprintf(out, out_size, "Clear %.48s", log_category_labels[item]);
        } else if(item == LogCategoryCount) {
            snprintf(out, out_size, "Clear all logs");
        } else {
            snprintf(out, out_size, "Back");
        }
        return;
    }
    if(app && app->settings_advanced) {
        switch(item) {
        case PoliteItemEnabled:
            snprintf(out, out_size, "Enabled: %s", settings_bool_label(app->politeness_enabled));
            break;
        case PoliteItemCadEnabled:
            snprintf(out, out_size, "CAD: %s", settings_bool_label(app->cad_enabled));
            break;
        case PoliteItemRssiFallback:
            snprintf(out, out_size, "RSSI fallback: %s", settings_bool_label(app->rssi_fallback_enabled));
            break;
        case PoliteItemRssiThreshold:
            snprintf(out, out_size, "RSSI busy: +%u dB", (unsigned)app->rssi_busy_margin_db);
            break;
        case PoliteItemMinBackoff:
            snprintf(out, out_size, "Min backoff: %u ms", (unsigned)app->min_backoff_ms);
            break;
        case PoliteItemMaxBackoff:
            snprintf(out, out_size, "Max backoff: %u ms", (unsigned)app->max_backoff_ms);
            break;
        case PoliteItemMaxWait:
            snprintf(out, out_size, "Max wait: %u ms", (unsigned)app->max_tx_wait_ms);
            break;
        case PoliteItemRelayMinDelay:
            snprintf(out, out_size, "Relay min: %u ms", (unsigned)app->rebroadcast_min_delay_ms);
            break;
        case PoliteItemRelayMaxDelay:
            snprintf(out, out_size, "Relay max: %u ms", (unsigned)app->rebroadcast_max_delay_ms);
            break;
        case PoliteItemBack:
            snprintf(out, out_size, "Back");
            break;
        default:
            snprintf(out, out_size, "Unknown");
            break;
        }
        return;
    }

    switch(item) {
    case SettingItemMaxHops:
        snprintf(out, out_size, "Max hops: %u", app->max_hops);
        break;
    case SettingItemFreqSlot:
        if(app->freq_slot == 0) {
            snprintf(out, out_size, "Freq: Auto %.3f", (double)app->current_freq_mhz);
        } else {
            snprintf(out, out_size, "Freq: %u %.3f", app->freq_slot, (double)app->current_freq_mhz);
        }
        break;
    case SettingItemPreset:
        snprintf(out, out_size, "Preset: %s", preset_labels[app->preset_index]);
        break;
    case SettingItemTxPower:
        snprintf(out, out_size, "TX power: %u dBm", app->tx_power_dbm);
        break;
    case SettingItemRole:
        snprintf(out, out_size, "Role: %s", role_labels[app->role_index]);
        break;
    case SettingItemNodeinfoInterval:
        snprintf(out, out_size, "NodeInfo: %s", nodeinfo_interval_labels[app->nodeinfo_interval_index]);
        break;
    case SettingItemMessageLimit:
        snprintf(out, out_size, "Inbox limit: %s", message_limit_labels[app->message_limit_index]);
        break;
    case SettingItemTemperatureUnit:
        snprintf(out, out_size, "Temp unit: %c", app->weather_fahrenheit ? 'F' : 'C');
        break;
    case SettingItemRegion:
        snprintf(out, out_size, "Region: US");
        break;
    case SettingItemShortName:
        snprintf(out, out_size, "Short: %s", app->self_short_name);
        break;
    case SettingItemLongName:
        snprintf(out, out_size, "Long: %.36s", app->self_long_name);
        break;
    case SettingItemChannelName:
        snprintf(out, out_size, "Channel: %s", get_channel_name_display(app));
        break;
    case SettingItemChannelPreset:
        snprintf(out, out_size, "Key: %s", channel_preset_labels[app->channel_preset_index]);
        break;
    case SettingItemChannelCustom:
        snprintf(out, out_size, "Custom key");
        break;
    case SettingItemPolitenessAdvanced:
        snprintf(out, out_size, "Advanced politeness");
        break;
    case SettingItemClearLogs:
        snprintf(out, out_size, "Clear logs");
        break;
    case SettingItemClear:
        snprintf(out, out_size, "Clear inbox");
        break;
    case SettingItemClearNodes:
        snprintf(out, out_size, "Clear nodes");
        break;
    case SettingItemClearWeather:
        snprintf(out, out_size, "Clear weather info");
        break;
    case SettingItemResetKeys:
        snprintf(out, out_size, "Reset PKI keys");
        break;
    case SettingItemReset:
        snprintf(out, out_size, "Reset settings");
        break;
    default:
        snprintf(out, out_size, "Unknown");
        break;
    }
}

static void settings_adjust_scroll(MeshtasticApp* app) {
    if(!app) return;
    const uint8_t visible_rows = 5;
    uint8_t count = settings_active_count(app);
    if(app->settings_selected >= count) app->settings_selected = 0;
    if(app->settings_selected < app->settings_scroll) app->settings_scroll = app->settings_selected;
    if(app->settings_selected >= app->settings_scroll + visible_rows) {
        app->settings_scroll = (uint8_t)(app->settings_selected - visible_rows + 1);
    }
}

static void settings_redraw(MeshtasticApp* app) {
    if(!app || !app->settings_view) return;
    with_view_model(
        app->settings_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        true);
}

static void meshtastic_settings_draw(Canvas* canvas, void* model) {
    MeshtasticViewModel* vm = model;
    MeshtasticApp* app = vm ? vm->app : NULL;
    if(!app) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(
        canvas,
        2,
        9,
        app->settings_log_clear ? "Clear logs" : (app->settings_advanced ? "Politeness" : "Settings"));
    canvas_set_font(canvas, FontSecondary);

    const uint8_t visible_rows = 5;
    const uint8_t row_h = 10;
    const uint8_t top = 12;
    char label[64];

    settings_adjust_scroll(app);
    uint8_t count = settings_active_count(app);
    for(uint8_t row = 0; row < visible_rows; row++) {
        uint32_t item = app->settings_scroll + row;
        if(item >= count) break;

        uint8_t y = top + row * row_h;
        bool selected = (item == app->settings_selected);
        if(selected) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y, 128, row_h);
            canvas_set_color(canvas, ColorWhite);
        }

        settings_get_label(app, item, label, sizeof(label));
        canvas_draw_str(canvas, 2, y + 8, label);
        if(settings_is_cycleable(app, item)) {
            canvas_draw_str_aligned(canvas, 126, y + 8, AlignRight, AlignBottom, "<>");
        } else if(!app->settings_advanced &&
                  (item == SettingItemPolitenessAdvanced || item == SettingItemClearLogs)) {
            canvas_draw_str_aligned(canvas, 126, y + 8, AlignRight, AlignBottom, "menu");
        } else if(app->settings_advanced || item != SettingItemRegion) {
            canvas_draw_str_aligned(canvas, 126, y + 8, AlignRight, AlignBottom, "OK");
        }

        if(selected) canvas_set_color(canvas, ColorBlack);
    }

    if(app->settings_scroll > 0) canvas_draw_str_aligned(canvas, 124, 9, AlignRight, AlignBottom, "^");
    if(app->settings_scroll + visible_rows < count) {
        canvas_draw_str_aligned(canvas, 124, 64, AlignRight, AlignBottom, "v");
    }
}

static void settings_refresh(MeshtasticApp* app, bool apply_radio) {
    if(!app) return;
    if(app->settings_refreshing) return;
    app->settings_refreshing = true;
    if(app->max_hops < 1 || app->max_hops > 7) app->max_hops = DEFAULT_MAX_HOPS;
    if(app->preset_index >= PresetCount) app->preset_index = PresetLongFast;
    if(app->tx_power_dbm < 2 || app->tx_power_dbm > 20) app->tx_power_dbm = 20;
    if(app->role_index >= RoleCount) app->role_index = RoleClient;
    if(app->nodeinfo_interval_index >=
       (sizeof(nodeinfo_intervals_ms) / sizeof(nodeinfo_intervals_ms[0]))) {
        app->nodeinfo_interval_index = 8;
    }
    if(app->message_limit_index >=
       (sizeof(message_limit_values) / sizeof(message_limit_values[0]))) {
        app->message_limit_index = 0;
    }
    app->message_limit = message_limit_values[app->message_limit_index];
    if(app->channel_preset_index >=
       (sizeof(channel_preset_labels) / sizeof(channel_preset_labels[0]))) {
        app->channel_preset_index = 0;
    }
    settings_clamp_politeness(app);
    update_radio_settings(app, apply_radio);
    settings_adjust_scroll(app);
    app->settings_refreshing = false;
    settings_redraw(app);
}

static uint16_t settings_cycle_u16(uint16_t value, uint16_t min, uint16_t max, uint16_t step, int8_t dir) {
    if(step == 0) step = 1;
    if(value < min || value > max) value = min;
    if(dir > 0) {
        return (value >= max || (uint16_t)(max - value) < step) ? min : (uint16_t)(value + step);
    }
    return (value <= min || (uint16_t)(value - min) < step) ? max : (uint16_t)(value - step);
}

static void settings_cycle_politeness(MeshtasticApp* app, uint32_t item, int8_t dir) {
    if(!app || item == PoliteItemBack) return;
    if(dir == 0) dir = 1;

    switch(item) {
    case PoliteItemEnabled:
        app->politeness_enabled = !app->politeness_enabled;
        break;
    case PoliteItemCadEnabled:
        app->cad_enabled = !app->cad_enabled;
        break;
    case PoliteItemRssiFallback:
        app->rssi_fallback_enabled = !app->rssi_fallback_enabled;
        break;
    case PoliteItemRssiThreshold:
        if(dir > 0) {
            app->rssi_busy_margin_db =
                (app->rssi_busy_margin_db >= 10) ? 6 : (uint8_t)(app->rssi_busy_margin_db + 1);
        } else {
            app->rssi_busy_margin_db =
                (app->rssi_busy_margin_db <= 6) ? 10 : (uint8_t)(app->rssi_busy_margin_db - 1);
        }
        break;
    case PoliteItemMinBackoff:
        app->min_backoff_ms = settings_cycle_u16(app->min_backoff_ms, 50, 5000, 50, dir);
        break;
    case PoliteItemMaxBackoff:
        app->max_backoff_ms = settings_cycle_u16(app->max_backoff_ms, 50, 10000, 50, dir);
        break;
    case PoliteItemMaxWait:
        app->max_tx_wait_ms = settings_cycle_u16(app->max_tx_wait_ms, 250, 30000, 250, dir);
        break;
    case PoliteItemRelayMinDelay:
        app->rebroadcast_min_delay_ms =
            settings_cycle_u16(app->rebroadcast_min_delay_ms, 0, 10000, 100, dir);
        break;
    case PoliteItemRelayMaxDelay:
        app->rebroadcast_max_delay_ms =
            settings_cycle_u16(app->rebroadcast_max_delay_ms, 0, 30000, 100, dir);
        break;
    default:
        break;
    }

    settings_clamp_politeness(app);
    settings_save(app);
    settings_refresh(app, false);
}

static void settings_cycle(MeshtasticApp* app, uint32_t item, int8_t dir) {
    if(!app || !settings_is_cycleable(app, item)) return;
    if(dir == 0) dir = 1;

    if(app->settings_advanced) {
        settings_cycle_politeness(app, item, dir);
        return;
    }

    if(item == SettingItemMaxHops) {
        if(dir > 0) {
            app->max_hops = (app->max_hops >= 7) ? 1 : (uint8_t)(app->max_hops + 1);
        } else {
            app->max_hops = (app->max_hops <= 1) ? 7 : (uint8_t)(app->max_hops - 1);
        }
        settings_save(app);
        settings_refresh(app, false);
    } else if(item == SettingItemFreqSlot) {
        uint32_t num_channels = compute_num_channels(app->current_bw_khz);
        uint8_t max_slot = (num_channels < (SETTINGS_FREQ_SLOT_UI_COUNT - 1)) ?
                               (uint8_t)num_channels :
                               (SETTINGS_FREQ_SLOT_UI_COUNT - 1);
        if(dir > 0) {
            app->freq_slot = (app->freq_slot >= max_slot) ? 0 : (uint8_t)(app->freq_slot + 1);
        } else {
            app->freq_slot = (app->freq_slot == 0) ? max_slot : (uint8_t)(app->freq_slot - 1);
        }
        settings_save(app);
        settings_refresh(app, true);
    } else if(item == SettingItemPreset) {
        app->preset_index = (dir > 0) ?
                                (uint8_t)((app->preset_index + 1) % PresetCount) :
                                (uint8_t)((app->preset_index + PresetCount - 1) % PresetCount);
        settings_save(app);
        settings_refresh(app, true);
    } else if(item == SettingItemTxPower) {
        if(dir > 0) {
            app->tx_power_dbm = (app->tx_power_dbm >= 20) ? 2 : (uint8_t)(app->tx_power_dbm + 1);
        } else {
            app->tx_power_dbm = (app->tx_power_dbm <= 2) ? 20 : (uint8_t)(app->tx_power_dbm - 1);
        }
        settings_save(app);
        settings_refresh(app, true);
    } else if(item == SettingItemRole) {
        app->role_index = (dir > 0) ?
                              (uint8_t)((app->role_index + 1) % RoleCount) :
                              (uint8_t)((app->role_index + RoleCount - 1) % RoleCount);
        settings_save(app);
        settings_refresh(app, false);
    } else if(item == SettingItemNodeinfoInterval) {
        size_t count = sizeof(nodeinfo_intervals_ms) / sizeof(nodeinfo_intervals_ms[0]);
        app->nodeinfo_interval_index = (dir > 0) ?
                                           (uint8_t)((app->nodeinfo_interval_index + 1) % count) :
                                           (uint8_t)((app->nodeinfo_interval_index + count - 1) % count);
        app->nodeinfo_interval_ms = nodeinfo_intervals_ms[app->nodeinfo_interval_index];
        app->last_nodeinfo_sent = 0;
        settings_save(app);
        settings_refresh(app, false);
    } else if(item == SettingItemMessageLimit) {
        size_t count = sizeof(message_limit_values) / sizeof(message_limit_values[0]);
        app->message_limit_index = (dir > 0) ?
                                       (uint8_t)((app->message_limit_index + 1) % count) :
                                       (uint8_t)((app->message_limit_index + count - 1) % count);
        app->message_limit = message_limit_values[app->message_limit_index];
        trim_messages_to_limit(app);
        messages_save(app);
        dm_messages_save(app);
        settings_save(app);
        if(app->current_view == ViewIdInbox) inbox_redraw(app);
        if(app->current_view == ViewIdDmInbox) dm_inbox_redraw(app);
        settings_refresh(app, false);
    } else if(item == SettingItemTemperatureUnit) {
        UNUSED(dir);
        app->weather_fahrenheit = !app->weather_fahrenheit;
        settings_save(app);
        if(app->current_view == ViewIdWeather) weather_redraw(app);
        settings_refresh(app, false);
    } else if(item == SettingItemChannelPreset) {
        size_t count = sizeof(channel_preset_labels) / sizeof(channel_preset_labels[0]);
        app->channel_preset_index = (dir > 0) ?
                                        (uint8_t)((app->channel_preset_index + 1) % count) :
                                        (uint8_t)((app->channel_preset_index + count - 1) % count);
        if(app->channel_preset_index <
           (sizeof(channel_preset_psk_bytes) / sizeof(channel_preset_psk_bytes[0]))) {
            app->channel_psk_len = 1;
            app->channel_psk[0] = channel_preset_psk_bytes[app->channel_preset_index];
            app->channel_hash = compute_channel_hash(app);
            app->last_nodeinfo_sent = 0;
            packet_cache_clear(app);
        }
        settings_save(app);
        settings_refresh(app, true);
    }
}

static void settings_enter(void* ctx, uint32_t index) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    if(app->settings_log_clear) {
        if(index < LogCategoryCount) {
            logs_clear_category(app, (MeshtasticLogCategory)index);
        } else if(index == LogCategoryCount) {
            logs_clear_all(app);
        } else {
            app->settings_log_clear = false;
            app->settings_advanced = false;
            app->settings_selected = SettingItemClearLogs;
            app->settings_scroll = 0;
        }
        settings_redraw(app);
        return;
    }
    if(app->settings_advanced) {
        if(index == PoliteItemBack) {
            app->settings_advanced = false;
            app->settings_log_clear = false;
            app->settings_selected = SettingItemPolitenessAdvanced;
            app->settings_scroll = 0;
        }
        settings_redraw(app);
        return;
    }
    if(settings_is_cycleable(app, index) || (!app->settings_advanced && index == SettingItemRegion)) {
        settings_redraw(app);
        return;
    }
    if(index == SettingItemShortName) {
        text_input_reset(app->text_input);
        memset(app->short_name_input, 0, sizeof(app->short_name_input));
        snprintf(app->short_name_input, sizeof(app->short_name_input), "%s", app->self_short_name);
        text_input_set_header_text(app->text_input, "Short name");
        text_input_set_result_callback(
            app->text_input,
            meshtastic_short_name_done,
            app,
            app->short_name_input,
            sizeof(app->short_name_input),
            true);
        app->settings_keyboard_active = true;
        meshtastic_switch_view(app, ViewIdSend);
    } else if(index == SettingItemLongName) {
        text_input_reset(app->text_input);
        memset(app->long_name_input, 0, sizeof(app->long_name_input));
        snprintf(app->long_name_input, sizeof(app->long_name_input), "%s", app->self_long_name);
        text_input_set_header_text(app->text_input, "Long name");
        text_input_set_result_callback(
            app->text_input,
            meshtastic_long_name_done,
            app,
            app->long_name_input,
            sizeof(app->long_name_input),
            true);
        app->settings_keyboard_active = true;
        meshtastic_switch_view(app, ViewIdSend);
    } else if(index == SettingItemChannelName) {
        text_input_reset(app->text_input);
        memset(app->channel_name_input, 0, sizeof(app->channel_name_input));
        snprintf(
            app->channel_name_input,
            sizeof(app->channel_name_input),
            "%s",
            get_channel_name_display(app));
        text_input_set_header_text(app->text_input, "Channel name");
        text_input_set_result_callback(
            app->text_input,
            meshtastic_channel_name_done,
            app,
            app->channel_name_input,
            sizeof(app->channel_name_input),
            true);
        app->settings_keyboard_active = true;
        meshtastic_switch_view(app, ViewIdSend);
    } else if(index == SettingItemChannelCustom) {
        text_input_reset(app->text_input);
        memset(app->channel_key_input, 0, sizeof(app->channel_key_input));
        text_input_set_header_text(app->text_input, "Channel key (base64)");
        text_input_set_result_callback(
            app->text_input,
            meshtastic_channel_key_done,
            app,
            app->channel_key_input,
            sizeof(app->channel_key_input),
            true);
        app->settings_keyboard_active = true;
        meshtastic_switch_view(app, ViewIdSend);
    } else if(index == SettingItemPolitenessAdvanced) {
        app->settings_advanced = true;
        app->settings_log_clear = false;
        app->settings_selected = 0;
        app->settings_scroll = 0;
        settings_redraw(app);
    } else if(index == SettingItemClearLogs) {
        app->settings_advanced = false;
        app->settings_log_clear = true;
        app->settings_selected = 0;
        app->settings_scroll = 0;
        settings_redraw(app);
    } else if(index == SettingItemClear) {
        messages_clear(app);
        dm_messages_clear(app);
        settings_refresh(app, false);
    } else if(index == SettingItemClearNodes) {
        nodes_clear(app);
        nodes_menu_rebuild(app);
        contacts_menu_rebuild(app);
        meshtastic_update_nodes_labels(app);
        settings_refresh(app, false);
    } else if(index == SettingItemClearWeather) {
        weather_clear(app);
        settings_refresh(app, false);
    } else if(index == SettingItemResetKeys) {
        if(crypto_curve25519_generate_keypair(app->self_public_key, app->self_private_key)) {
            app->self_keys_valid = true;
            settings_save(app);
            app->last_nodeinfo_sent = 0;
            app->nodeinfo_burst_left = NODEINFO_BURST_COUNT;
            FURI_LOG_I("Proto", "PKI keys reset");
        }
        settings_refresh(app, false);
    } else if(index == SettingItemReset) {
        settings_defaults(app);
        app->settings_advanced = false;
        app->settings_log_clear = false;
        packet_cache_clear(app);
        settings_save(app);
        meshtastic_update_inbox_label(app);
        meshtastic_update_dm_inbox_label(app);
        settings_refresh(app, true);
    }
}

static bool settings_handle_key(MeshtasticApp* app, InputKey key) {
    if(!app) return false;
    if(key == InputKeyUp) {
        if(app->settings_selected > 0) app->settings_selected--;
        settings_adjust_scroll(app);
        settings_redraw(app);
        return true;
    }
    if(key == InputKeyDown) {
        if(app->settings_selected + 1 < settings_active_count(app)) app->settings_selected++;
        settings_adjust_scroll(app);
        settings_redraw(app);
        return true;
    }
    if(key == InputKeyLeft) {
        settings_cycle(app, app->settings_selected, -1);
        return true;
    }
    if(key == InputKeyRight) {
        settings_cycle(app, app->settings_selected, 1);
        return true;
    }
    return false;
}

static bool settings_key_can_hold(InputKey key) {
    return key == InputKeyUp || key == InputKeyDown || key == InputKeyLeft || key == InputKeyRight;
}

static void settings_hold_step(MeshtasticApp* app) {
    if(!app || app->current_view != ViewIdSettings || !app->settings_hold_active) return;
    uint32_t now = furi_get_tick();
    if(now - app->settings_hold_start_ms < SETTINGS_HOLD_DELAY_MS) return;
    if(app->settings_hold_last_step_ms != 0 &&
       now - app->settings_hold_last_step_ms < SETTINGS_HOLD_REPEAT_MS) {
        return;
    }
    if(settings_handle_key(app, (InputKey)app->settings_hold_key)) {
        app->settings_hold_last_step_ms = now;
    }
}

static bool meshtastic_settings_input(InputEvent* event, void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return false;

    if(event->type == InputTypePress && settings_key_can_hold(event->key)) {
        app->settings_hold_active = true;
        app->settings_hold_key = (uint8_t)event->key;
        app->settings_hold_start_ms = furi_get_tick();
        app->settings_hold_last_step_ms = 0;
        return true;
    }
    if(event->type == InputTypeRelease) {
        if(app->settings_hold_active && app->settings_hold_key == (uint8_t)event->key) {
            app->settings_hold_active = false;
        }
        return settings_key_can_hold(event->key);
    }
    if(event->type == InputTypeLong || event->type == InputTypeRepeat) {
        return settings_key_can_hold(event->key);
    }
    // Short is the one-shot click. Held repeats are generated from the app tick above.
    if(event->type != InputTypeShort) return false;

    app->settings_hold_active = false;
    if(settings_handle_key(app, event->key)) return true;
    if(event->key == InputKeyOk) {
        settings_enter(app, app->settings_selected);
        return true;
    }
    if(event->key == InputKeyBack) {
        if(app->settings_log_clear) {
            app->settings_log_clear = false;
            app->settings_advanced = false;
            app->settings_selected = SettingItemClearLogs;
            app->settings_scroll = 0;
            settings_redraw(app);
            return true;
        }
        if(app->settings_advanced) {
            app->settings_advanced = false;
            app->settings_log_clear = false;
            app->settings_selected = SettingItemPolitenessAdvanced;
            app->settings_scroll = 0;
            settings_redraw(app);
            return true;
        }
        meshtastic_switch_view(app, ViewIdMenu);
        return true;
    }
    return false;
}

static void meshtastic_send_done(void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    if(app->text_buffer[0] != '\0') {
        uint32_t id = meshtastic_generate_packet_id_local();
        uint32_t to = app->sending_dm ? app->dm_target_id : NODENUM_BROADCAST;
        if(app->sending_dm) {
            meshtastic_send_nodeinfo(app, to, true);
        }
        meshtastic_store_message(
            app,
            app->self_node_id,
            to,
            id,
            app->text_buffer,
            true,
            app->sending_dm);
        app->pending_tx = true;
        app->pending_tx_is_dm = app->sending_dm;
        app->pending_tx_id = id;
        app->pending_tx_to = to;
        if(app->sending_dm) {
            app->pending_tx_msg_index =
                message_list_find_index(app->dm_messages, app->dm_count, app->self_node_id, id);
        } else {
            app->pending_tx_msg_index =
                message_list_find_index(app->messages, app->msg_count, app->self_node_id, id);
        }
        snprintf(app->pending_tx_text, sizeof(app->pending_tx_text), "%s", app->text_buffer);
    }
    if(app->sending_dm) {
        meshtastic_switch_view(app, ViewIdDmInbox);
        app->sending_dm = false;
    } else {
        meshtastic_switch_view(app, ViewIdInbox);
    }
}

static void meshtastic_channel_key_done(void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    if(app->channel_key_input[0] != '\0') {
        uint8_t decoded[32] = {0};
        size_t decoded_len = 0;
        if(base64_decode(app->channel_key_input, decoded, sizeof(decoded), &decoded_len) &&
           decoded_len > 0 && decoded_len <= sizeof(decoded)) {
            memcpy(app->channel_psk, decoded, decoded_len);
            app->channel_psk_len = (uint8_t)decoded_len;
            app->channel_preset_index =
                (uint8_t)(sizeof(channel_preset_labels) / sizeof(channel_preset_labels[0]) - 1);
            app->channel_hash = compute_channel_hash(app);
            app->last_nodeinfo_sent = 0;
            packet_cache_clear(app);
            settings_save(app);
        } else {
            FURI_LOG_W("Proto", "Invalid channel key base64");
        }
    }
    settings_refresh(app, true);
    meshtastic_switch_view(app, ViewIdSettings);
}

static void meshtastic_short_name_done(void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    meshtastic_sanitize_text(app->self_short_name, sizeof(app->self_short_name), app->short_name_input);
    settings_save(app);
    app->last_nodeinfo_sent = 0;
    app->nodeinfo_burst_left = NODEINFO_BURST_COUNT;
    settings_refresh(app, false);
    meshtastic_switch_view(app, ViewIdSettings);
}

static void meshtastic_long_name_done(void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    meshtastic_sanitize_text(app->self_long_name, sizeof(app->self_long_name), app->long_name_input);
    settings_save(app);
    app->last_nodeinfo_sent = 0;
    app->nodeinfo_burst_left = NODEINFO_BURST_COUNT;
    settings_refresh(app, false);
    meshtastic_switch_view(app, ViewIdSettings);
}

static void meshtastic_channel_name_done(void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    size_t len = safe_strnlen(app->channel_name_input, sizeof(app->channel_name_input));
    while(len > 0 && app->channel_name_input[len - 1] == ' ') {
        app->channel_name_input[len - 1] = '\0';
        len--;
    }
    size_t start = 0;
    while(start < len && app->channel_name_input[start] == ' ') start++;
    memset(app->channel_name, 0, sizeof(app->channel_name));
    if(start < len) {
        strncpy(app->channel_name, app->channel_name_input + start, sizeof(app->channel_name) - 1);
    } else {
        strncpy(app->channel_name, MESHTASTIC_CHANNEL_NAME, sizeof(app->channel_name) - 1);
    }
    app->channel_hash = compute_channel_hash(app);
    app->last_nodeinfo_sent = 0;
    packet_cache_clear(app);
    settings_save(app);
    settings_refresh(app, true);
    meshtastic_switch_view(app, ViewIdSettings);
}

static void meshtastic_busy_dialog_done(DialogExResult result, void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    if(app->contacts_warning_dialog) {
        app->contacts_warning_dialog = false;
        meshtastic_switch_view(app, result == DialogExResultCenter ? ViewIdContacts : ViewIdMenu);
        return;
    }
    if(result == DialogExResultCenter) {
        meshtastic_switch_view(app, app->busy_dialog_is_dm ? ViewIdDmInbox : ViewIdInbox);
    }
}

static void meshtastic_show_channel_busy_dialog(MeshtasticApp* app, bool is_dm) {
    if(!app || !app->busy_dialog) return;
    app->busy_dialog_is_dm = is_dm;
    app->contacts_warning_dialog = false;
    dialog_ex_reset(app->busy_dialog);
    dialog_ex_set_header(app->busy_dialog, "Channel busy", 64, 10, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->busy_dialog,
        "channel is busy.\nplease wait and try again.",
        64,
        32,
        AlignCenter,
        AlignCenter);
    dialog_ex_set_center_button_text(app->busy_dialog, "OK");
    dialog_ex_set_context(app->busy_dialog, app);
    dialog_ex_set_result_callback(app->busy_dialog, meshtastic_busy_dialog_done);
    meshtastic_switch_view(app, ViewIdPopup);
}

static void meshtastic_show_contacts_warning(MeshtasticApp* app) {
    if(!app || !app->busy_dialog) return;
    app->contacts_warning_dialog = true;
    dialog_ex_reset(app->busy_dialog);
    dialog_ex_set_text(
        app->busy_dialog,
        "Direct Messaging\nis currently not\nfunctioning.\nPlaceholder only.",
        64,
        30,
        AlignCenter,
        AlignCenter);
    dialog_ex_set_left_button_text(app->busy_dialog, "Back");
    dialog_ex_set_center_button_text(app->busy_dialog, "OK");
    dialog_ex_set_context(app->busy_dialog, app);
    dialog_ex_set_result_callback(app->busy_dialog, meshtastic_busy_dialog_done);
    meshtastic_switch_view(app, ViewIdPopup);
}

static bool meshtastic_navigation(void* ctx) {
    MeshtasticApp* app = ctx;
    if(!app) return false;
    if(app->current_view == ViewIdMenu) {
        view_dispatcher_stop(app->view_dispatcher);
    } else if(app->current_view == ViewIdSend && app->settings_keyboard_active) {
        meshtastic_switch_view(app, ViewIdSettings);
    } else if(app->current_view == ViewIdPopup) {
        if(app->contacts_warning_dialog) {
            app->contacts_warning_dialog = false;
            meshtastic_switch_view(app, ViewIdMenu);
        } else {
            meshtastic_switch_view(app, app->busy_dialog_is_dm ? ViewIdDmInbox : ViewIdInbox);
        }
    } else if(app->current_view == ViewIdLogs) {
        meshtastic_switch_view(app, ViewIdMenu);
    } else if(app->current_view == ViewIdLogFiles) {
        meshtastic_switch_view(app, ViewIdLogs);
    } else if(app->current_view == ViewIdLogText) {
        meshtastic_switch_view(app, ViewIdLogFiles);
    } else {
        meshtastic_switch_view(app, ViewIdMenu);
    }
    return true;
}

static void meshtastic_menu_callback(void* ctx, uint32_t index) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    if(index == MenuItemInbox) {
        meshtastic_switch_view(app, ViewIdInbox);
    } else if(index == MenuItemSend) {
        app->settings_keyboard_active = false;
        app->sending_dm = false;
        text_input_reset(app->text_input);
        memset(app->text_buffer, 0, sizeof(app->text_buffer));
        text_input_set_header_text(app->text_input, "Send message");
        text_input_set_result_callback(
            app->text_input,
            meshtastic_send_done,
            app,
            app->text_buffer,
            sizeof(app->text_buffer),
            true);
        meshtastic_switch_view(app, ViewIdSend);
    } else if(index == MenuItemBroadcastNodeinfo) {
        if(meshtastic_send_nodeinfo(app, NODENUM_BROADCAST, false)) {
            app->last_nodeinfo_sent = furi_get_tick();
        }
    } else if(index == MenuItemNodes) {
        meshtastic_switch_view(app, ViewIdNodes);
    } else if(index == MenuItemContacts) {
        meshtastic_show_contacts_warning(app);
    } else if(index == MenuItemWeather) {
        meshtastic_switch_view(app, ViewIdWeather);
    } else if(index == MenuItemSettings) {
        FURI_LOG_I("Main", "Settings refresh start");
        app->settings_advanced = false;
        app->settings_log_clear = false;
        settings_refresh(app, false);
        FURI_LOG_I("Main", "Settings refresh done");
        meshtastic_switch_view(app, ViewIdSettings);
        FURI_LOG_I("Main", "Settings view shown");
    } else if(index == MenuItemLogs) {
        logs_menu_rebuild(app);
        meshtastic_switch_view(app, ViewIdLogs);
    }
}

static void meshtastic_logs_menu_callback(void* ctx, uint32_t index) {
    MeshtasticApp* app = ctx;
    if(!app || index >= LogCategoryCount) return;
    app->selected_log_category = (uint8_t)index;
    log_files_menu_rebuild(app);
    meshtastic_switch_view(app, ViewIdLogFiles);
}

static void meshtastic_log_files_callback(void* ctx, uint32_t index) {
    MeshtasticApp* app = ctx;
    if(!app || index >= app->log_file_count) return;
    log_text_open(app, app->log_file_names[index]);
}

static void meshtastic_nodes_menu_callback(void* ctx, uint32_t index) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    if(index < app->node_count) {
        app->selected_node_index = (uint8_t)index;
        meshtastic_switch_view(app, ViewIdNodeDetail);
    }
}

static void meshtastic_contacts_menu_callback(void* ctx, uint32_t index) {
    MeshtasticApp* app = ctx;
    if(!app) return;
    if(index == 0) {
        meshtastic_switch_view(app, ViewIdDmInbox);
        return;
    }
    uint32_t node_index = index - 1;
    if(node_index < app->node_count) {
        NodeInfo* node = &app->nodes[node_index];
        app->sending_dm = true;
        app->dm_target_id = node->node_id;
        if(node->short_name[0] != '\0') {
            snprintf(app->dm_target_short, sizeof(app->dm_target_short), "%s", node->short_name);
        } else {
            format_short_name(app->dm_target_short, sizeof(app->dm_target_short), node->node_id);
        }
        text_input_reset(app->text_input);
        memset(app->text_buffer, 0, sizeof(app->text_buffer));
        text_input_set_header_text(app->text_input, "Send DM");
        text_input_set_result_callback(
            app->text_input,
            meshtastic_send_done,
            app,
            app->text_buffer,
            sizeof(app->text_buffer),
            true);
        meshtastic_switch_view(app, ViewIdSend);
    }
}

static void meshtastic_radio_step(MeshtasticApp* app) {
    if(!app || app->state == AppStateError) return;

    uint32_t now = furi_get_tick();
    if(app->radio_ready_ms == 0 || now - app->radio_ready_ms < RADIO_STARTUP_DELAY_MS) {
        return;
    }

    if(!app->packet_cache_loaded) {
        FURI_LOG_I("Main", "Packet cache load start");
        packet_cache_load(app);
        app->packet_cache_loaded = true;
        FURI_LOG_I("Main", "Packet cache loaded");
    }

    static uint32_t last_poll = 0;
    static uint8_t last_irq = 0;

    if(now - last_poll >= 50) {
        last_poll = now;
        uint8_t irq = read_reg(REG_IRQ_FLAGS);

        if(irq != last_irq) {
            int16_t rssi = rfm95w_get_current_rssi();
            app->last_rssi = (int8_t)rssi;
            FURI_LOG_I("Radio", "IRQ change: 0x%02X -> 0x%02X | RSSI %d dBm", last_irq, irq, rssi);
            last_irq = irq;
        }

        if(irq & IRQ_RX_DONE) {
            app->packet_available = true;
        }
        if(irq & IRQ_PAYLOAD_CRC_ERROR) {
            rfm95w_dio0_clear();
        }
    }

    if(app->packet_available) {
        uint8_t rx_buffer[256];
        int len = rfm95w_read_packet(app, rx_buffer);
        if(len > 0) {
            app->packets_received++;
            process_received_packet(app, rx_buffer, len);
        }
        app->packet_available = false;
    }

    meshtastic_process_relay_queue(app);

    if(!app->pending_tx) {
        uint32_t now = furi_get_tick();
        uint32_t ack_interval_ms = ack_retry_interval_ms(app);
        for(uint8_t pass = 0; pass < 2 && !app->pending_tx; pass++) {
            Message* list = (pass == 0) ? app->messages : app->dm_messages;
            uint8_t count = (pass == 0) ? app->msg_count : app->dm_count;
            for(uint8_t i = 0; i < count; i++) {
                Message* msg = &list[i];
                if(!msg->outgoing || msg->ack_state != AckStatePending) continue;
                if(msg->retries >= ACK_MAX_ATTEMPTS) {
                    if(msg->last_tx_ms > 0 && now - msg->last_tx_ms >= ack_interval_ms) {
                        msg->ack_state = AckStateFailed;
                        if(pass == 0) {
                            messages_save(app);
                            if(app->current_view == ViewIdInbox) inbox_redraw(app);
                        } else {
                            dm_messages_save(app);
                            if(app->current_view == ViewIdDmInbox) dm_inbox_redraw(app);
                        }
                    }
                    continue;
                }
                if(msg->last_tx_ms == 0 || now - msg->last_tx_ms >= ack_interval_ms) {
                    app->pending_tx = true;
                    app->pending_tx_is_dm = (pass == 1);
                    app->pending_tx_id = msg->id;
                    app->pending_tx_to = msg->to;
                    app->pending_tx_msg_index = (int8_t)i;
                    snprintf(
                        app->pending_tx_text,
                        sizeof(app->pending_tx_text),
                        "%.*s",
                        (int)sizeof(app->pending_tx_text) - 1,
                        (char*)msg->payload);
                    break;
                }
            }
        }
    }

    if(app->nodeinfo_burst_left > 0 &&
       (app->last_nodeinfo_sent == 0 ||
        now - app->last_nodeinfo_sent >= NODEINFO_BURST_INTERVAL_MS)) {
        uint8_t packet[256];
        size_t packet_len = 0;
        if(build_meshtastic_nodeinfo_packet(app, packet, sizeof(packet), &packet_len)) {
            if(rfm95w_send_packet(app, packet, packet_len)) {
                app->last_nodeinfo_sent = now;
                app->nodeinfo_burst_left--;
                FURI_LOG_I("Radio", "NodeInfo TX done, len=%u", (unsigned)packet_len);
                meshtastic_log_event(
                    app,
                    LogCategoryNodeInfo,
                    "periodic tx_ok to=broadcast len=%u",
                    (unsigned)packet_len);
                meshtastic_send_device_metrics(app);
            } else {
                meshtastic_log_event(
                    app,
                    LogCategoryNodeInfo,
                    "periodic tx_failed to=broadcast busy_timeout=%u",
                    app->last_tx_busy_timeout ? 1u : 0u);
            }
        }
    } else if(app->last_nodeinfo_sent == 0 ||
              now - app->last_nodeinfo_sent >= app->nodeinfo_interval_ms) {
        uint8_t packet[256];
        size_t packet_len = 0;
        if(build_meshtastic_nodeinfo_packet(app, packet, sizeof(packet), &packet_len)) {
            if(rfm95w_send_packet(app, packet, packet_len)) {
                app->last_nodeinfo_sent = now;
                FURI_LOG_I("Radio", "NodeInfo TX done, len=%u", (unsigned)packet_len);
                meshtastic_log_event(
                    app,
                    LogCategoryNodeInfo,
                    "periodic tx_ok to=broadcast len=%u",
                    (unsigned)packet_len);
                meshtastic_send_device_metrics(app);
            } else {
                meshtastic_log_event(
                    app,
                    LogCategoryNodeInfo,
                    "periodic tx_failed to=broadcast busy_timeout=%u",
                    app->last_tx_busy_timeout ? 1u : 0u);
            }
        }
    }

    if(app->pending_pos) {
        app->pending_pos = false;
        uint8_t packet[256];
        size_t packet_len = 0;
        if(build_meshtastic_position_packet_from_payload(
               app,
               app->pending_pos_payload,
               app->pending_pos_len,
               packet,
               sizeof(packet),
               &packet_len)) {
            if(rfm95w_send_packet(app, packet, packet_len)) {
                app->packets_sent++;
                FURI_LOG_I("Radio", "Position TX done, len=%u", (unsigned)packet_len);
                meshtastic_log_event(
                    app,
                    LogCategoryTelemetry,
                    "position_copy tx_ok from=0x%08lX len=%u",
                    (unsigned long)app->pending_pos_from,
                    (unsigned)packet_len);
            } else {
                FURI_LOG_W("Radio", "Position TX failed");
                meshtastic_log_event(
                    app,
                    LogCategoryTelemetry,
                    "position_copy tx_failed from=0x%08lX busy_timeout=%u",
                    (unsigned long)app->pending_pos_from,
                    app->last_tx_busy_timeout ? 1u : 0u);
            }
        } else {
            FURI_LOG_W("Radio", "Failed to build position packet");
        }
    }

    if(app->pending_tx) {
        app->pending_tx = false;
        uint8_t packet[256];
        size_t packet_len = 0;
        uint32_t pkt_id = 0;
        if(build_meshtastic_text_packet(
               app,
               app->pending_tx_text,
               app->pending_tx_to,
               packet,
               sizeof(packet),
               &packet_len,
               &pkt_id,
               app->pending_tx_id)) {
            bool sent = rfm95w_send_packet(app, packet, packet_len);
            if(sent) {
                app->packets_sent++;
                FURI_LOG_I("Radio", "TX done, len=%u", (unsigned)packet_len);
                meshtastic_log_event(
                    app,
                    LogCategoryTx,
                    "%s tx_ok to=0x%08lX id=0x%08lX len=%u text=\"%s\"",
                    app->pending_tx_is_dm ? "dm" : "broadcast",
                    (unsigned long)app->pending_tx_to,
                    (unsigned long)app->pending_tx_id,
                    (unsigned)packet_len,
                    app->pending_tx_text);
            } else {
                FURI_LOG_W("Radio", "TX failed");
                meshtastic_log_event(
                    app,
                    LogCategoryTx,
                    "%s tx_failed to=0x%08lX id=0x%08lX busy_timeout=%u text=\"%s\"",
                    app->pending_tx_is_dm ? "dm" : "broadcast",
                    (unsigned long)app->pending_tx_to,
                    (unsigned long)app->pending_tx_id,
                    app->last_tx_busy_timeout ? 1u : 0u,
                    app->pending_tx_text);
            }
            Message* list = app->pending_tx_is_dm ? app->dm_messages : app->messages;
            uint8_t count = app->pending_tx_is_dm ? app->dm_count : app->msg_count;
            if(app->pending_tx_msg_index >= 0 &&
               app->pending_tx_msg_index < (int8_t)count) {
                Message* msg = &list[app->pending_tx_msg_index];
                if(sent) {
                    msg->last_tx_ms = furi_get_tick();
                    msg->retries++;
                } else if(app->last_tx_busy_timeout) {
                    msg->ack_state = AckStateBusy;
                    meshtastic_show_channel_busy_dialog(app, app->pending_tx_is_dm);
                } else {
                    msg->last_tx_ms = furi_get_tick();
                    msg->retries++;
                }
                if(app->pending_tx_is_dm) {
                    dm_messages_save(app);
                    if(app->current_view == ViewIdDmInbox) dm_inbox_redraw(app);
                } else {
                    messages_save(app);
                    if(app->current_view == ViewIdInbox) inbox_redraw(app);
                }
            }
        } else {
            FURI_LOG_W("Radio", "Failed to build Meshtastic packet");
            Message* list = app->pending_tx_is_dm ? app->dm_messages : app->messages;
            uint8_t count = app->pending_tx_is_dm ? app->dm_count : app->msg_count;
            if(app->pending_tx_msg_index >= 0 &&
               app->pending_tx_msg_index < (int8_t)count) {
                Message* msg = &list[app->pending_tx_msg_index];
                msg->ack_state = AckStateFailed;
                if(app->pending_tx_is_dm) {
                    dm_messages_save(app);
                    if(app->current_view == ViewIdDmInbox) dm_inbox_redraw(app);
                } else {
                    messages_save(app);
                    if(app->current_view == ViewIdInbox) inbox_redraw(app);
                }
            }
        }
    }

    rfm95w_update_noise_floor_ema(app);
}

static void meshtastic_tick(void* ctx) {
    MeshtasticApp* app = ctx;
    settings_hold_step(app);
    meshtastic_radio_step(app);
}

int32_t meshtastic_app_entry(void* p) {
    UNUSED(p);
    FURI_LOG_I("Main", "App start");
    FURI_LOG_I("Main", "App ctx size %u", (unsigned)sizeof(MeshtasticApp));

    MeshtasticApp* app = malloc(sizeof(MeshtasticApp));
    FURI_LOG_I("Main", "App alloc %p", app);
    if(!app) {
        FURI_LOG_E("Main", "App alloc failed");
        return -1;
    }
    memset(app, 0, sizeof(MeshtasticApp));
    app->pending_tx_msg_index = -1;

    FURI_LOG_I("Main", "Open GUI record");
    app->gui = furi_record_open(RECORD_GUI);
    FURI_LOG_I("Main", "Open Storage record");
    app->storage = furi_record_open(RECORD_STORAGE);
    FURI_LOG_I("Main", "Records open");
    storage_common_mkdir(app->storage, MESSAGES_DIR);
    logs_init(app);
    app->view_dispatcher = view_dispatcher_alloc();
    FURI_LOG_I("Main", "View dispatcher alloc");
    FURI_LOG_I("Main", "Set event ctx");
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    FURI_LOG_I("Main", "Set nav cb");
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, meshtastic_navigation);
    FURI_LOG_I("Main", "Dispatcher callbacks set");

    FURI_LOG_I("Main", "Set names");
    meshtastic_sanitize_text(app->self_long_name, sizeof(app->self_long_name), MESHTASTIC_SELF_LONG_NAME);
    meshtastic_sanitize_text(app->self_short_name, sizeof(app->self_short_name), MESHTASTIC_SELF_SHORT_NAME);
    app->self_node_id = meshtastic_get_node_id();
    snprintf(
        app->self_id_str,
        sizeof(app->self_id_str),
        "!%08lX",
        (unsigned long)app->self_node_id);
    app->nodeinfo_burst_left = NODEINFO_BURST_COUNT;
    FURI_LOG_I("Main", "Names set");
    settings_defaults(app);
    FURI_LOG_I("Main", "Defaults set");
    settings_load(app);
    FURI_LOG_I("Main", "Settings loaded");
    update_radio_settings(app, false);
    FURI_LOG_I("Main", "Radio settings computed");
    FURI_LOG_I("Main", "Messages load start");
    messages_load(app);
    FURI_LOG_I("Main", "DM messages load start");
    dm_messages_load(app);
    FURI_LOG_I("Main", "Nodes load start");
    nodes_load(app);
    FURI_LOG_I("Main", "Weather load start");
    weather_load(app);
    FURI_LOG_I("Main", "Storage loaded");

    app->submenu = submenu_alloc();
    app->nodes_menu = submenu_alloc();
    app->contacts_menu = submenu_alloc();
    app->logs_menu = submenu_alloc();
    app->log_files_menu = submenu_alloc();
    submenu_set_header(app->submenu, "Fliptastic");
    submenu_add_item(app->submenu, "Inbox", MenuItemInbox, meshtastic_menu_callback, app);
    submenu_add_item(app->submenu, "Send message", MenuItemSend, meshtastic_menu_callback, app);
    submenu_add_item(app->submenu, "Broadcast NodeInfo", MenuItemBroadcastNodeinfo, meshtastic_menu_callback, app);
    submenu_add_item(app->submenu, "Nodes (0)", MenuItemNodes, meshtastic_menu_callback, app);
    submenu_add_item(app->submenu, "Contacts (0)", MenuItemContacts, meshtastic_menu_callback, app);
    submenu_add_item(app->submenu, "Weather (0)", MenuItemWeather, meshtastic_menu_callback, app);
    submenu_add_item(app->submenu, "Settings", MenuItemSettings, meshtastic_menu_callback, app);
    submenu_add_item(app->submenu, "Logs", MenuItemLogs, meshtastic_menu_callback, app);
    logs_menu_rebuild(app);
    nodes_menu_rebuild(app);
    contacts_menu_rebuild(app);
    meshtastic_update_nodes_labels(app);
    meshtastic_update_weather_label(app);

    app->inbox_view = view_alloc();
    view_set_context(app->inbox_view, app);
    view_set_draw_callback(app->inbox_view, meshtastic_inbox_draw);
    view_set_input_callback(app->inbox_view, meshtastic_inbox_input);
    view_allocate_model(app->inbox_view, ViewModelTypeLockFree, sizeof(MeshtasticViewModel));
    with_view_model(
        app->inbox_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        false);

    app->dm_inbox_view = view_alloc();
    view_set_context(app->dm_inbox_view, app);
    view_set_draw_callback(app->dm_inbox_view, meshtastic_dm_inbox_draw);
    view_set_input_callback(app->dm_inbox_view, meshtastic_dm_inbox_input);
    view_allocate_model(app->dm_inbox_view, ViewModelTypeLockFree, sizeof(MeshtasticViewModel));
    with_view_model(
        app->dm_inbox_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        false);

    app->node_detail_view = view_alloc();
    view_set_context(app->node_detail_view, app);
    view_set_draw_callback(app->node_detail_view, meshtastic_node_detail_draw);
    view_set_input_callback(app->node_detail_view, meshtastic_node_detail_input);
    view_allocate_model(app->node_detail_view, ViewModelTypeLockFree, sizeof(MeshtasticViewModel));
    with_view_model(
        app->node_detail_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        false);

    app->weather_view = view_alloc();
    view_set_context(app->weather_view, app);
    view_set_draw_callback(app->weather_view, meshtastic_weather_draw);
    view_set_input_callback(app->weather_view, meshtastic_weather_input);
    view_allocate_model(app->weather_view, ViewModelTypeLockFree, sizeof(MeshtasticViewModel));
    with_view_model(
        app->weather_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        false);

    app->settings_view = view_alloc();
    view_set_context(app->settings_view, app);
    view_set_draw_callback(app->settings_view, meshtastic_settings_draw);
    view_set_input_callback(app->settings_view, meshtastic_settings_input);
    view_allocate_model(app->settings_view, ViewModelTypeLockFree, sizeof(MeshtasticViewModel));
    with_view_model(
        app->settings_view,
        MeshtasticViewModel* model,
        { model->app = app; },
        false);
    settings_refresh(app, false);

    app->text_input = text_input_alloc();
    app->log_text_box = text_box_alloc();
    app->busy_dialog = dialog_ex_alloc();
    app->popup = popup_alloc();

    view_dispatcher_add_view(app->view_dispatcher, ViewIdMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdInbox, app->inbox_view);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdDmInbox, app->dm_inbox_view);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdSend, text_input_get_view(app->text_input));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdSettings, app->settings_view);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdPopup, dialog_ex_get_view(app->busy_dialog));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdNodes, submenu_get_view(app->nodes_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdContacts, submenu_get_view(app->contacts_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdNodeDetail, app->node_detail_view);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdWeather, app->weather_view);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdLogs, submenu_get_view(app->logs_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdLogFiles, submenu_get_view(app->log_files_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdLogText, text_box_get_view(app->log_text_box));

    FURI_LOG_I("Main", "Attach view dispatcher");
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    view_dispatcher_set_tick_event_callback(app->view_dispatcher, meshtastic_tick, 50);

    FURI_LOG_I("Main", "Radio init start");
    if(!rfm95w_init(app)) {
        FURI_LOG_E("Main", "Radio init failed - check wiring");
        app->state = AppStateError;
    } else {
        FURI_LOG_I("Main", "Radio Ready");
        app->state = AppStateIdle;
        rfm95w_update_noise_floor_ema(app);
    }

    FURI_LOG_I("Main", "Radio init stage done");
    FURI_LOG_I("Main", "Views ready");

    app->current_view = ViewIdMenu;
    FURI_LOG_I("Main", "Update labels");
    meshtastic_update_inbox_label(app);
    FURI_LOG_I("Main", "Switch initial view");
    if(app->state == AppStateError) {
        FURI_LOG_I("Main", "Build radio error menu");
        radio_error_menu_rebuild(app);
    }
    app->current_view = ViewIdMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdMenu);
    FURI_LOG_I("Main", "Switch initial view done");
    if(app->state != AppStateError) {
        app->radio_ready_ms = furi_get_tick();
        if(app->last_nodeinfo_sent == 0) {
            app->last_nodeinfo_sent = app->radio_ready_ms;
        }
        FURI_LOG_I("Main", "Enable DIO0 interrupt");
        rfm95w_set_dio0_interrupt(app, true);
    } else {
        FURI_LOG_I("Main", "Skip settings refresh in radio error mode");
    }
    FURI_LOG_I("Main", "Start dispatcher");

    view_dispatcher_run(app->view_dispatcher);

    if(app->packet_cache_loaded && app->packet_cache_dirty) {
        packet_cache_save(app);
    }
    nodes_save(app);
    weather_save(app);

    rfm95w_set_dio0_interrupt(app, false);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdLogText);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdLogFiles);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdLogs);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdPopup);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdSettings);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdSend);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdWeather);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdNodeDetail);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdContacts);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdNodes);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdDmInbox);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdInbox);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdMenu);
    view_free(app->settings_view);
    view_free(app->inbox_view);
    view_free(app->dm_inbox_view);
    view_free(app->node_detail_view);
    view_free(app->weather_view);
    text_input_free(app->text_input);
    text_box_free(app->log_text_box);
    dialog_ex_free(app->busy_dialog);
    popup_free(app->popup);
    submenu_free(app->submenu);
    submenu_free(app->nodes_menu);
    submenu_free(app->contacts_menu);
    submenu_free(app->logs_menu);
    submenu_free(app->log_files_menu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
