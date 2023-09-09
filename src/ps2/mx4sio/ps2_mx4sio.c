#include "ps2_mx4sio.h"
#include "hardware/gpio.h"
#include "ps2_mc_spi.pio.h"
#include "ps2_mx4sio.pio.h"
#include "hardware/pio.h"

void ps2_mx4sio_init(void)
{
    PIO pio = pio0;

    hw_set_bits(&pio->input_sync_bypass, (1u << PIN_PSX_CLK) | (1u << PIN_PSX_DAT) | (1 << PIN_SD_MISO) | (1 << PIN_PSX_SEL));
    
    uint offset = pio_add_program(pio, &spi_gateway_program);
    uint sm = pio_claim_unused_sm(pio, true);

    spi_gateway_init(pio, sm, offset, PIN_PSX_CLK, PIN_SD_SCK);
    
    offset = pio_add_program(pio, &spi_gateway_program);
    sm = pio_claim_unused_sm(pio, true);
    spi_gateway_init(pio, sm, offset, PIN_PSX_CMD, PIN_SD_MOSI);


    offset = pio_add_program(pio, &spi_gateway_program);
    sm = pio_claim_unused_sm(pio, true);
    spi_gateway_init(pio, sm, offset, PIN_SD_MISO, PIN_PSX_DAT);


    offset = pio_add_program(pio, &spi_gateway_program);
    sm = pio_claim_unused_sm(pio, true);
    spi_gateway_init(pio, sm, offset, PIN_PSX_SEL, PIN_SD_CS);

    gpio_init(PIN_PSX_ACK);
    gpio_set_dir(PIN_PSX_ACK, GPIO_OUT);
    gpio_put(PIN_PSX_ACK, 0);
}
