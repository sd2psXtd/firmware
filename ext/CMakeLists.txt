
add_library(sd_fat STATIC    
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/common/FmtNumber.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/common/FsCache.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/common/FsDateTime.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/common/FsName.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/common/FsStructs.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/common/FsUtf.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/common/upcase.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatDbg.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatFile.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatFilePrint.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatFileWrite.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatFormatter.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatName.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatPartition.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/ExFatLib/ExFatVolume.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatDbg.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatFile.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatFileLFN.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatFilePrint.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatFileSFN.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatFormatter.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatName.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatPartition.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FatLib/FatVolume.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FreeStack.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FsLib/FsFile.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FsLib/FsNew.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/FsLib/FsVolume.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/iostream/istream.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/iostream/ostream.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/iostream/StdioStream.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/iostream/StreamBaseClass.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/MinimumSerial.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/SdCard/SdCardInfo.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src/SdCard/SdSpiCard.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFatWrapper/src/sd.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFatWrapper/src/SPI.cpp
)

target_include_directories(sd_fat 
                        INTERFACE
                            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFatWrapper/include
                        PRIVATE
                            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFatWrapper/src
                        PUBLIC 
                            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/src
                            ${CMAKE_CURRENT_SOURCE_DIR}/ESP8266SdFat/extras/attic)

target_link_libraries(sd_fat 
        PUBLIC 
            pico_stdlib
            hardware_spi)

target_link_libraries(sd_fat PRIVATE sd2psx_common)


add_library(ssd1306 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/ssd1306/ssd1306.c)
target_link_libraries(ssd1306 pico_stdlib hardware_i2c)
target_include_directories(ssd1306 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ssd1306)

set(LV_CONF_PATH ${PROJECT_SOURCE_DIR}/src/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lvgl EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/mcfat)

target_compile_definitions(mcfat PRIVATE MAX_CACHEENTRY=0x1)
target_compile_definitions(mcfat PRIVATE MAX_FDHANDLES=0x1)

add_library(inih STATIC ${CMAKE_CURRENT_SOURCE_DIR}/inih/ini.c)
target_include_directories(inih PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inih)
