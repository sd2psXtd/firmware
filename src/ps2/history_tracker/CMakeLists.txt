add_library(ps2_history_tracker STATIC
            ps2_history_tracker.c)

target_link_libraries(ps2_history_tracker PRIVATE mcFat)