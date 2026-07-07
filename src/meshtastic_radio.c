#include "meshtastic.h"
#include <furi_hal_gpio.h>

static int8_t last_rssi = 0;
static int8_t last_snr = 0;

#define NOISE_FLOOR_SAMPLE_INTERVAL_MS 100
#define CAD_TIMEOUT_MIN_MS 5
#define CAD_TIMEOUT_MAX_MS 200

// ============ REGISTER ACCESS ============
uint8_t read_reg(uint8_t addr) {
    uint8_t val = 0;
    spi_begin();
    spi_transfer(addr & 0x7F);
    val = spi_transfer(0x00);
    spi_end();
    return val;
}

void write_reg(uint8_t reg, uint8_t val) {
    spi_begin();
    spi_transfer(reg | 0x80);
    spi_transfer(val);
    spi_end();
}

// ============ INTERRUPT HANDLER ============
static void rfm95w_dio0_isr(void* context) {
    MeshtasticApp* app = (MeshtasticApp*)context;
    if(app) {
        app->packet_available = true;
    }
}

void rfm95w_set_dio0_interrupt(MeshtasticApp* app, bool enabled) {
    if(enabled) {
        furi_hal_gpio_init(PIN_DIO0, GpioModeInterruptRise, GpioPullDown, GpioSpeedVeryHigh);
        furi_hal_gpio_add_int_callback(PIN_DIO0, rfm95w_dio0_isr, app);
        FURI_LOG_I("Radio", "DIO0 interrupt enabled on B2 (Pin 6)");
    } else {
        furi_hal_gpio_remove_int_callback(PIN_DIO0);
        furi_hal_gpio_init(PIN_DIO0, GpioModeInput, GpioPullDown, GpioSpeedVeryHigh);
    }
}

bool rfm95w_dio0_status(void) {
    return furi_hal_gpio_read(PIN_DIO0);
}

void rfm95w_dio0_clear(void) {
    write_reg(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_PAYLOAD_CRC_ERROR | IRQ_RX_TIMEOUT);
}

// ============ MODE CONTROL ============
void rfm95w_set_mode_standby(void) {
    write_reg(REG_OPMODE, MODE_LORA | MODE_STDBY);
    furi_delay_ms(5);
}

void rfm95w_set_mode_rx(void) {
    write_reg(REG_DIO_MAPPING_1, 0x00); // DIO0 = RxDone
    write_reg(REG_OPMODE, MODE_LORA | MODE_RXCONTINUOUS);
    furi_delay_ms(5);
}

static void rfm95w_set_mode_standby_fast(void) {
    write_reg(REG_OPMODE, MODE_LORA | MODE_STDBY);
    furi_delay_ms(1);
}

static void rfm95w_set_mode_rx_fast(void) {
    write_reg(REG_DIO_MAPPING_1, 0x00); // DIO0 = RxDone
    write_reg(REG_OPMODE, MODE_LORA | MODE_RXCONTINUOUS);
    furi_delay_ms(1);
}

static void rfm95w_power_cycle(void) {
    furi_hal_gpio_write(PIN_EN, false);
    furi_delay_ms(10);
    furi_hal_gpio_write(PIN_EN, true);
    furi_delay_ms(10);
}

// ============ INITIALIZATION ============
bool rfm95w_init(MeshtasticApp* app) {
    UNUSED(app);

    spi_init();
    rfm95w_power_cycle();

    uint8_t version = 0;
    for(int i = 0; i < 10; i++) {
        version = read_reg(REG_VERSION);
        if(version == 0x12) break;
        FURI_LOG_W("Radio", "Probe %d/10: RegVersion=0x%02X (expected 0x12)", i + 1, version);
        furi_delay_ms(100);
    }

    if(version != 0x12) {
        uint8_t opmode = read_reg(REG_OPMODE);
        FURI_LOG_E("Radio", "SPI FAIL! RegVersion=0x%02X RegOpMode=0x%02X", version, opmode);
        return false;
    }

    FURI_LOG_I("Radio", "Found RFM95W (RegVersion=0x%02X)", version);

    // Enter LoRa standby mode.
    write_reg(REG_OPMODE, MODE_SLEEP);
    furi_delay_ms(5);
    write_reg(REG_OPMODE, MODE_LORA | MODE_SLEEP);
    furi_delay_ms(5);
    write_reg(REG_OPMODE, MODE_LORA | MODE_STDBY);
    furi_delay_ms(5);

    // FIFO and IRQ setup.
    write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);
    write_reg(REG_FIFO_ADDR_PTR, 0x00);
    write_reg(REG_MAX_PAYLOAD_LENGTH, 0xFF);
    write_reg(REG_IRQ_FLAGS_MASK, 0x00);

    // Configure default radio settings from app.
    if(app) {
        rfm95w_apply_config(app, app->current_freq_mhz, app->current_bw_khz, app->current_sf, app->current_cr, app->tx_power_dbm);
    }

    // LNA gain + boost for RX sensitivity on SX127x.
    write_reg(REG_LNA, 0x23);

    // Preamble + Meshtastic sync.
    write_reg(REG_PREAMBLE_MSB, 0x00);
    write_reg(REG_PREAMBLE_LSB, 0x10);
    write_reg(REG_SYNC_WORD, 0x2B);

    write_reg(REG_IRQ_FLAGS, 0xFF);
    rfm95w_set_mode_rx();

    FURI_LOG_I("Radio", "Init complete. IRQ: 0x%02X", read_reg(REG_IRQ_FLAGS));
    return true;
}

static uint32_t freq_to_frf(float freq_mhz) {
    uint32_t frf = (uint32_t)((freq_mhz * 1000000.0f) / 61.03515625f);
    return frf;
}

static uint8_t bw_khz_to_reg(float bw_khz) {
    if(bw_khz > 400.0f) return 0x90; // 500 kHz
    if(bw_khz > 200.0f) return 0x80; // 250 kHz
    if(bw_khz > 100.0f) return 0x70; // 125 kHz
    if(bw_khz > 60.0f) return 0x60;  // 62.5 kHz
    return 0x50; // 41.7 kHz fallback
}

static uint8_t cr_to_reg(uint8_t cr) {
    if(cr < 5) cr = 5;
    if(cr > 8) cr = 8;
    return (uint8_t)((cr - 4) << 1);
}

bool rfm95w_apply_config(
    MeshtasticApp* app,
    float freq_mhz,
    float bw_khz,
    uint8_t sf,
    uint8_t cr,
    uint8_t tx_power_dbm) {
    UNUSED(app);

    rfm95w_set_mode_standby();

    uint32_t frf = freq_to_frf(freq_mhz);
    write_reg(REG_FR_MSB, (uint8_t)((frf >> 16) & 0xFF));
    write_reg(REG_FR_MID, (uint8_t)((frf >> 8) & 0xFF));
    write_reg(REG_FR_LSB, (uint8_t)(frf & 0xFF));

    uint8_t mc1 = bw_khz_to_reg(bw_khz) | cr_to_reg(cr);
    uint8_t mc2 = (uint8_t)((sf & 0x0F) << 4) | 0x04; // CRC on
    uint8_t mc3 = 0x04; // AGC auto on
    write_reg(REG_MODEM_CONFIG_1, mc1);
    write_reg(REG_MODEM_CONFIG_2, mc2);
    write_reg(REG_MODEM_CONFIG_3, mc3);

    // TX power via PA_BOOST
    if(tx_power_dbm < 2) tx_power_dbm = 2;
    if(tx_power_dbm > 20) tx_power_dbm = 20;
    if(tx_power_dbm > 17) {
        write_reg(REG_PA_DAC, 0x87);
        write_reg(REG_OCP, 0x3B); // 150 mA
        write_reg(REG_PA_CONFIG, 0x80 | (tx_power_dbm - 5));
    } else {
        write_reg(REG_PA_DAC, 0x84);
        write_reg(REG_OCP, 0x2B); // 100 mA
        write_reg(REG_PA_CONFIG, 0x80 | (tx_power_dbm - 2));
    }

    // Preamble + Meshtastic sync.
    write_reg(REG_PREAMBLE_MSB, 0x00);
    write_reg(REG_PREAMBLE_LSB, 0x10);
    write_reg(REG_SYNC_WORD, 0x2B);

    rfm95w_set_mode_rx();

    FURI_LOG_I(
        "Radio",
        "Cfg F=%.4f BW=%.1f SF=%u CR=%u PWR=%u",
        (double)freq_mhz,
        (double)bw_khz,
        sf,
        cr,
        tx_power_dbm);
    return true;
}

static uint32_t radio_random_between(uint32_t min, uint32_t max) {
    if(max <= min) return min;
    return min + (furi_hal_random_get() % (max - min + 1));
}

static uint8_t rfm95w_rssi_margin(MeshtasticApp* app) {
    if(!app || app->rssi_busy_margin_db < 6 || app->rssi_busy_margin_db > 10) return 6;
    return app->rssi_busy_margin_db;
}

static int16_t rfm95w_noise_floor(MeshtasticApp* app) {
    if(!app || !app->rssi_noise_floor_valid) return DEFAULT_RSSI_NOISE_FLOOR_DBM;
    return app->rssi_noise_floor_dbm;
}

static int16_t rfm95w_busy_rssi_threshold(MeshtasticApp* app) {
    return rfm95w_noise_floor(app) + (int16_t)rfm95w_rssi_margin(app);
}

static int16_t rfm95w_round_ema_x16(int16_t ema_x16) {
    if(ema_x16 >= 0) return (int16_t)((ema_x16 + 8) / 16);
    return (int16_t)((ema_x16 - 8) / 16);
}

static uint32_t rfm95w_cad_timeout_ms(MeshtasticApp* app) {
    uint8_t sf = app ? app->current_sf : 11;
    float bw_khz = app ? app->current_bw_khz : 250.0f;
    if(sf < 6 || sf > 12) sf = 11;
    if(bw_khz <= 0.0f) bw_khz = 250.0f;

    // SX127x CAD time scales with LoRa symbol time. Wait about four symbols
    // plus a small guard so slow presets do not false-timeout.
    float symbol_ms = (float)(1UL << sf) / bw_khz;
    uint32_t timeout_ms = (uint32_t)((symbol_ms * 4.0f) + 5.0f);
    if(timeout_ms < CAD_TIMEOUT_MIN_MS) timeout_ms = CAD_TIMEOUT_MIN_MS;
    if(timeout_ms > CAD_TIMEOUT_MAX_MS) timeout_ms = CAD_TIMEOUT_MAX_MS;
    return timeout_ms;
}

void rfm95w_update_noise_floor_ema(MeshtasticApp* app) {
    if(!app) return;
    if(app->state != AppStateIdle || app->packet_available || app->pending_tx || app->pending_pos) return;

    uint32_t now = furi_get_tick();
    if(app->last_noise_floor_sample_ms != 0 &&
       now - app->last_noise_floor_sample_ms < NOISE_FLOOR_SAMPLE_INTERVAL_MS) {
        return;
    }

    uint8_t opmode = read_reg(REG_OPMODE);
    if((opmode & 0x07) != MODE_RXCONTINUOUS) return;

    uint8_t irq = read_reg(REG_IRQ_FLAGS);
    if(irq & (IRQ_VALID_HEADER | IRQ_RX_DONE | IRQ_PAYLOAD_CRC_ERROR | IRQ_RX_TIMEOUT)) return;

    int16_t rssi = rfm95w_get_current_rssi();
    if(rssi < -127) rssi = -127;
    if(rssi > 0) rssi = 0;
    app->last_noise_floor_sample_ms = now;

    int16_t sample_x16 = (int16_t)(rssi * 16);
    if(!app->rssi_noise_floor_valid) {
        app->rssi_noise_floor_ema_x16 = sample_x16;
        app->rssi_noise_floor_valid = true;
        app->rssi_noise_floor_dbm = (int8_t)rssi;
        FURI_LOG_I(
            "Radio",
            "Noise floor EMA init: %d dBm, busy threshold %d dBm",
            app->rssi_noise_floor_dbm,
            rfm95w_busy_rssi_threshold(app));
        meshtastic_log_event(
            app,
            LogCategoryPoliteness,
            "noise floor EMA init=%d margin=+%u threshold=%d",
            app->rssi_noise_floor_dbm,
            (unsigned)rfm95w_rssi_margin(app),
            rfm95w_busy_rssi_threshold(app));
    } else {
        // EMA alpha = 1/8. This tracks slow floor changes but ignores short packets.
        app->rssi_noise_floor_ema_x16 =
            (int16_t)(app->rssi_noise_floor_ema_x16 + ((sample_x16 - app->rssi_noise_floor_ema_x16) / 8));
        int16_t rounded = rfm95w_round_ema_x16(app->rssi_noise_floor_ema_x16);
        if(rounded < -127) rounded = -127;
        if(rounded > 0) rounded = 0;
        app->rssi_noise_floor_dbm = (int8_t)rounded;
    }
}

static bool rfm95w_send_packet_now(uint8_t* data, size_t len) {
    if(!data || len == 0 || len > 255) return false;

    rfm95w_set_mode_standby_fast();
    write_reg(REG_IRQ_FLAGS, 0xFF);

    write_reg(REG_FIFO_ADDR_PTR, 0x00);
    write_reg(REG_PAYLOAD_LENGTH, (uint8_t)len);
    for(size_t i = 0; i < len; i++) {
        write_reg(REG_FIFO, data[i]);
    }

    write_reg(REG_OPMODE, MODE_LORA | MODE_TX);
    FURI_LOG_I("Radio", "TX start, len=%u", (unsigned)len);

    uint32_t timeout = furi_get_tick() + 1000;
    while(furi_get_tick() < timeout) {
        uint8_t irq = read_reg(REG_IRQ_FLAGS);
        if(irq & IRQ_TX_DONE) {
            FURI_LOG_I("Radio", "TX IRQ=0x%02X (TX_DONE set)", irq);
            write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE);
            rfm95w_set_mode_rx();
            return true;
        }
        furi_delay_ms(1);
    }

    FURI_LOG_W("Radio", "TX timeout");
    write_reg(REG_IRQ_FLAGS, 0xFF);
    rfm95w_set_mode_rx();
    return false;
}

static bool rfm95w_channel_busy_cad(MeshtasticApp* app, bool* valid) {
    if(valid) *valid = false;

    rfm95w_set_mode_standby_fast();
    write_reg(REG_IRQ_FLAGS, 0xFF);
    write_reg(REG_OPMODE, MODE_LORA | MODE_CAD);

    uint32_t cad_timeout_ms = rfm95w_cad_timeout_ms(app);
    uint32_t start = furi_get_tick();
    while(furi_get_tick() - start < cad_timeout_ms) {
        uint8_t irq = read_reg(REG_IRQ_FLAGS);
        if(irq & IRQ_CAD_DONE) {
            bool busy = (irq & IRQ_CAD_DETECTED) != 0;
            write_reg(REG_IRQ_FLAGS, IRQ_CAD_DONE | IRQ_CAD_DETECTED);
            rfm95w_set_mode_rx_fast();
            if(valid) *valid = true;
            if(busy) {
                FURI_LOG_I("Radio", "Channel busy: CAD detected LoRa activity");
            }
            return busy;
        }
        furi_delay_ms(1);
    }

    FURI_LOG_W("Radio", "CAD timeout after %lums, treating channel busy", (unsigned long)cad_timeout_ms);
    write_reg(REG_IRQ_FLAGS, IRQ_CAD_DONE | IRQ_CAD_DETECTED);
    rfm95w_set_mode_rx_fast();
    return true;
}

static bool rfm95w_channel_busy(MeshtasticApp* app) {
    if(!app) return false;

    if(app->cad_enabled) {
        bool valid = false;
        bool busy = rfm95w_channel_busy_cad(app, &valid);
        if(valid) {
            if(busy) {
                meshtastic_log_event(app, LogCategoryPoliteness, "channel busy CAD detected");
                return true;
            }
            meshtastic_log_event(
                app,
                LogCategoryPoliteness,
                "CAD clear timeout_ms=%lu; checking RSSI floor=%d margin=+%u threshold=%d",
                (unsigned long)rfm95w_cad_timeout_ms(app),
                rfm95w_noise_floor(app),
                (unsigned)rfm95w_rssi_margin(app),
                rfm95w_busy_rssi_threshold(app));
        } else {
            meshtastic_log_event(
                app,
                LogCategoryPoliteness,
                "channel busy CAD timeout_ms=%lu",
                (unsigned long)rfm95w_cad_timeout_ms(app));
            return true;
        }
    } else if(app->rssi_fallback_enabled) {
        meshtastic_log_event(
            app,
            LogCategoryPoliteness,
            "CAD disabled; checking RSSI floor=%d margin=+%u threshold=%d",
            rfm95w_noise_floor(app),
            (unsigned)rfm95w_rssi_margin(app),
            rfm95w_busy_rssi_threshold(app));
    }

    if(app->cad_enabled || app->rssi_fallback_enabled) {
        int16_t rssi = rfm95w_get_current_rssi();
        int16_t threshold = rfm95w_busy_rssi_threshold(app);
        bool busy = rssi >= threshold;
        if(busy) {
            FURI_LOG_I(
                "Radio",
                "Channel busy: RSSI %d dBm >= %d dBm",
                rssi,
                threshold);
            meshtastic_log_event(
                app,
                LogCategoryPoliteness,
                "channel busy RSSI=%d floor=%d margin=+%u threshold=%d",
                rssi,
                rfm95w_noise_floor(app),
                (unsigned)rfm95w_rssi_margin(app),
                threshold);
        } else {
            meshtastic_log_event(
                app,
                LogCategoryPoliteness,
                "RSSI clear RSSI=%d floor=%d margin=+%u threshold=%d",
                rssi,
                rfm95w_noise_floor(app),
                (unsigned)rfm95w_rssi_margin(app),
                threshold);
        }
        return busy;
    }

    return false;
}

// ============ SEND PACKET ============
bool rfm95w_send_packet(MeshtasticApp* app, uint8_t* data, size_t len) {
    if(!data || len == 0 || len > 255) return false;
    if(app) app->last_tx_busy_timeout = false;
    if(!app || !app->politeness_enabled) return rfm95w_send_packet_now(data, len);

    uint32_t min_backoff = app->min_backoff_ms;
    uint32_t max_backoff = app->max_backoff_ms;
    uint32_t max_wait = app->max_tx_wait_ms;
    if(min_backoff == 0) min_backoff = 250;
    if(max_backoff < min_backoff) max_backoff = min_backoff;
    if(max_wait == 0) max_wait = 5000;

    uint32_t start = furi_get_tick();
    while(true) {
        if(!rfm95w_channel_busy(app)) {
            FURI_LOG_I(
                "Radio",
                "TX allowed after %lums wait",
                (unsigned long)(furi_get_tick() - start));
            meshtastic_log_event(
                app,
                LogCategoryPoliteness,
                "tx allowed wait_ms=%lu len=%u",
                (unsigned long)(furi_get_tick() - start),
                (unsigned)len);
            return rfm95w_send_packet_now(data, len);
        }

        uint32_t waited = furi_get_tick() - start;
        if(waited >= max_wait) {
            FURI_LOG_W(
                "Radio",
                "TX dropped after max wait %lums, len=%u",
                (unsigned long)waited,
                (unsigned)len);
            meshtastic_log_event(
                app,
                LogCategoryPoliteness,
                "tx dropped max_wait_ms=%lu len=%u",
                (unsigned long)waited,
                (unsigned)len);
            app->last_tx_busy_timeout = true;
            rfm95w_set_mode_rx();
            return false;
        }

        uint32_t remaining = max_wait - waited;
        uint32_t backoff = radio_random_between(min_backoff, max_backoff);
        if(backoff > remaining) backoff = remaining;
        FURI_LOG_I("Radio", "Backoff chosen: %lums", (unsigned long)backoff);
        meshtastic_log_event(
            app,
            LogCategoryPoliteness,
            "backoff_ms=%lu remaining_ms=%lu",
            (unsigned long)backoff,
            (unsigned long)remaining);
        furi_delay_ms(backoff ? backoff : 1);
    }
}

// ============ READ PACKET ============
int rfm95w_read_packet(MeshtasticApp* app, uint8_t* buffer) {
    if(!buffer) return 0;

    uint8_t irq = read_reg(REG_IRQ_FLAGS);
    if(!(irq & IRQ_RX_DONE)) {
        return 0;
    }

    if(irq & IRQ_PAYLOAD_CRC_ERROR) {
        FURI_LOG_W("Radio", "RX CRC error (IRQ=0x%02X)", irq);
        write_reg(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_PAYLOAD_CRC_ERROR);
        return 0;
    }

    uint8_t len = read_reg(REG_RX_NB_BYTES);
    if(len == 0) {
        write_reg(REG_IRQ_FLAGS, IRQ_RX_DONE);
        return 0;
    }

    uint8_t fifo_addr = read_reg(REG_FIFO_RX_CURRENT_ADDR);
    write_reg(REG_FIFO_ADDR_PTR, fifo_addr);

    for(uint8_t i = 0; i < len; i++) {
        buffer[i] = read_reg(REG_FIFO);
    }

    last_rssi = -157 + (int16_t)read_reg(REG_PKT_RSSI_VALUE);
    last_snr = (int8_t)read_reg(REG_PKT_SNR_VALUE) / 4;

    if(app) {
        app->last_rssi = last_rssi;
        app->last_snr = last_snr;
    }

    write_reg(REG_IRQ_FLAGS, 0xFF);
    return len;
}

// ============ RSSI/SNR GETTERS ============
int8_t rfm95w_get_last_rssi(void) {
    return last_rssi;
}

int8_t rfm95w_get_last_snr(void) {
    return last_snr;
}

int16_t rfm95w_get_current_rssi(void) {
    return -157 + read_reg(REG_RSSI_VALUE);
}
