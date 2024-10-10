#include "util.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "sd.h"
#include "game_db/game_db.h"

bool str_is_integer(const char *str) {
    int i = 0;

    while (true) {
        if (!str[i])
            break;
        if (isdigit((unsigned char)str[i]) == 0)
            return false;
        i++;
    }

    return true;
}

bool try_set_named_card_folder(const char *cards_dir, int it_idx, char *folder_name, size_t folder_name_size) {
    bool ret = false;
    int dir_fd, it_fd = -1;
    char filename[MAX_GAME_ID_LENGTH + 1] = {}; // +1 byte to be able to tell whether the name was truncated or not

    dir_fd = sd_open(cards_dir, O_RDONLY);
    if (dir_fd < 0)
        return false;

    it_fd = sd_iterate_dir(dir_fd, it_fd);
    while (it_fd != -1) {
        if (!sd_is_dir(it_fd) || !sd_get_name(it_fd, filename, sizeof(filename))) {
            it_fd = sd_iterate_dir(dir_fd, it_fd);
            continue;
        }

        // Skip boot card, normal cards, and cards with names longer than 15 characters
        if (strcmp(filename, "BOOT") == 0 ||
            (strncmp(filename, "Card", 4) == 0 && str_is_integer(filename + 4)) ||
            (strlen(filename) >= MAX_GAME_ID_LENGTH)) {
            it_fd = sd_iterate_dir(dir_fd, it_fd);
            continue;
        }

        if (it_idx > 0) {
            it_idx--;
            it_fd = sd_iterate_dir(dir_fd, it_fd);
            continue;
        }

        // This is the valid folder name
        snprintf(folder_name, folder_name_size, "%s", filename);
        ret = true;
        break;
    }


    if (it_fd != -1)
        sd_close(it_fd);

    sd_close(dir_fd);

    return ret;
}