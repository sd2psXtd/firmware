#include "ps1/ps1_memory_card.c"
#include <assert.h>
#include <string.h>

ps1_memory_card_backend_t ps1_card_backend_mca;
ps1_memory_card_backend_t ps1_card_backend_pio;
static const uint8_t *input;
static size_t input_length, input_pos, reply_length;
static uint8_t reply[160], card[128];
static bool dma_waited;
static unsigned writes, commits, setups;

static uint8_t receive(uint8_t *byte) {
    if (input_pos == input_length) return PS1_RECEIVE_RESET;
    *byte = input[input_pos++];
    return PS1_RECEIVE_OK;
}
static void respond(uint8_t byte) {
    assert(reply_length < sizeof(reply));
    reply[reply_length++] = byte;
}
void ps1_mc_data_interface_setup_read_page(uint32_t page) {
    assert(page < 1024);
    ++setups;
}
uint8_t *ps1_mc_data_interface_get_page(uint32_t page) {
    assert(page < 1024);
    return card;
}
void ps1_mc_data_interface_wait_for_byte(uint32_t offset) {
    if (offset == PS1_PAGE_SIZE - 1) dma_waited = true;
}
void ps1_mc_data_interface_write_byte(uint32_t address, uint8_t byte) {
    assert(dma_waited && address / PS1_PAGE_SIZE == 2);
    card[address % PS1_PAGE_SIZE] = byte;
    ++writes;
}
void ps1_mc_data_interface_write_mc(uint32_t page) {
    assert(page == 2 && writes == PS1_PAGE_SIZE);
    ++commits;
}
static void begin(const uint8_t *request, size_t length) {
    input = request + 2;
    input_length = length - 2;
    input_pos = reply_length = 0;
    ps1_card_active = true;
}
int main(void) {
    uint8_t write[138] = {0x81, 'W', 0, 0, 0, 2};
    uint8_t read[140] = {0x81, 'R', 0, 0, 3, 0xff};
    ps1_card_backend_mca.recv_mc = receive;
    ps1_card_backend_mca.respond = respond;
    ps1_card_backend = &ps1_card_backend_mca;
    memset(write + 6, 0x34, PS1_PAGE_SIZE);
    write[134] = 2;

    begin(write, sizeof(write));
    mc_cmd_write();
    assert(dma_waited && commits == 1 && writes == PS1_PAGE_SIZE);
    assert(reply_length == 136 && reply[135] == 0x47);
    assert(memcmp(card, write + 6, PS1_PAGE_SIZE) == 0);

    dma_waited = false;
    writes = 0;
    write[134] = 3;
    begin(write, sizeof(write));
    mc_cmd_write();
    assert(!dma_waited && writes == 0 && commits == 1);
    assert(reply_length == 136 && reply[135] == 0x4e);

    begin(write, 70);
    mc_cmd_write();
    assert(!dma_waited && writes == 0 && commits == 1);

    begin(read, sizeof(read));
    mc_cmd_read(false);
    assert(setups == 1 && reply_length == 138 && reply[137] == 0x47);
    read[1] = 'B';
    setups = 0;
    begin(read, sizeof(read));
    mc_cmd_read(true);
    assert(setups == 1 && reply_length < 138);
    read[1] = 'R'; read[4] = 4; read[5] = 0;
    setups = 0;
    begin(read, sizeof(read));
    mc_cmd_read(false);
    assert(setups == 0 && reply_length < 138);
    return 0;
}
