add_library(mcfat STATIC
                src/mcio.c
                src/util.c
                src/mcfat.c)

target_include_directories(mcfat PUBLIC inc)
