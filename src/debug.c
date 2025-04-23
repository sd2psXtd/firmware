#include "debug.h"

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#if WITH_LED
#include "led.h"
#endif

#if WITH_GUI
#include "oled.h"
#endif
#include "pico/platform.h"

const char *log_level_str[] = {
    " ",
    "[ERROR]",
    "[WARN] ",
    "[INFO] ",
    "[TRACE]"
};

static char debug_queue[1024];
static size_t debug_read_pos, debug_write_pos;

void debug_put(char c) {
    debug_queue[debug_write_pos] = c;
    debug_write_pos = (debug_write_pos + 1) % sizeof(debug_queue);
}

char debug_get(void) {
    char ret = debug_queue[debug_read_pos];
    if (ret) {
        debug_queue[debug_read_pos] = 0;
        debug_read_pos = (debug_read_pos + 1) % sizeof(debug_queue);
    }
    return ret;
}

void __time_critical_func(buffered_printf)(const char *format, ...) {
    char buf[128];

    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    for (char *c = buf; *c; ++c)
        debug_put(*c);
}

void fatal(const char *format, ...) {
    char buf[128];

    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    printf("%s\n", buf);
#if WITH_GUI
    static int fatal_reentry;
    if (!fatal_reentry) {
        fatal_reentry = 1;
        oled_init();
        oled_clear();
        oled_draw_text("FATAL ERROR\n\n");
        oled_draw_text(buf);
        oled_show();
    }
#endif
#if WITH_LED
    led_fatal();
#endif

    while (1) {
    }
}

void hexdump(const uint8_t *buf, size_t sz) {
    for (size_t i = 0; i < sz; ++i)
        printf("%02X ", buf[i]);
    printf("\n");
}
