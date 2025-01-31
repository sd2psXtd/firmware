#include <stddef.h>
#include <stdint.h>

#define MAX_FOLDER_NAME_LENGTH      ( 32 )

void card_config_read_channel_name(const char* card_folder, const char* card_base, const char* channel_number, char* name, size_t name_max_len);
uint8_t card_config_get_max_channels(const char* card_folder, const char* card_base);
uint8_t card_config_get_ps2_cardsize(const char* card_folder, const char* card_base);
void card_config_get_card_folder(const char* game_id, char* card_folder, size_t card_folder_max_len);
