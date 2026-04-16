#include <hardware/spi.h>
#include <hardware/gpio.h>

#include "globals.h"
#include "mcp4251.h"

#define SPI_CHANNEL spi0
#define SPI_BAUDRATE 10000000

static const uint8_t CMD_WRITE = 0x00;

void mcp4251_init() {
    spi_init(SPI_CHANNEL, SPI_BAUDRATE);
    spi_set_format(SPI_CHANNEL, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, true);
}

void mcp4251_write() {
    for (int pot = 0; pot < NDIGIPOTS; pot++) {
        // MCP4x51 16-bit SPI frame: [addr(4) | cmd(2) | data(10)]
        // Pot 0 addr = 0x00, Pot 1 addr = 0x10, write cmd = 0x00
        uint16_t address = pot << 4;
        uint16_t spi_data = ((address | CMD_WRITE) << 8) | (digipot_state[pot] & 0xFF);

        gpio_put(SPI_CS_PIN, false);
        spi_write16_blocking(SPI_CHANNEL, &spi_data, 1);
        gpio_put(SPI_CS_PIN, true);
    }
}