#include "meshtastic.h"

static bool spi_is_initialized = false;

void spi_init(void) {
    if(spi_is_initialized) {
        return;
    }

    // CS and EN are application-controlled GPIO.
    furi_hal_gpio_init_simple(PIN_CS, GpioModeOutputPushPull);
    furi_hal_gpio_write(PIN_CS, true);

    furi_hal_gpio_init_simple(PIN_EN, GpioModeOutputPushPull);
    furi_hal_gpio_write(PIN_EN, true);

    furi_hal_gpio_init(PIN_DIO0, GpioModeInput, GpioPullDown, GpioSpeedVeryHigh);

    // Initialize Flipper external SPI handle (MOSI=A7, MISO=A6, SCK=B3).
    furi_hal_spi_bus_handle_deinit(&furi_hal_spi_bus_handle_external);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_external);

    spi_is_initialized = true;
    furi_delay_ms(10);
}

void spi_begin(void) {
    if(!spi_is_initialized) {
        spi_init();
    }

    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_external);
    furi_hal_gpio_write(PIN_CS, false);
}

void spi_end(void) {
    furi_hal_gpio_write(PIN_CS, true);
    furi_hal_spi_release(&furi_hal_spi_bus_handle_external);
}

uint8_t spi_transfer(uint8_t tx) {
    uint8_t rx = 0;
    furi_hal_spi_bus_trx(&furi_hal_spi_bus_handle_external, &tx, &rx, 1, 10);
    return rx;
}
