#include <stddef.h>
#include <stdint.h>

void card_config_read_channel_name(const char* card_folder, const char* card_base, const char* channel_number, char* name, size_t name_max_len);
uint8_t card_config_get_max_channels(const char* card_folder, const char* card_base);
uint8_t card_config_get_ps2_cardsize(const char* card_folder, const char* card_base);
