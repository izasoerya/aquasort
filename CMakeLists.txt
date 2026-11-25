cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aquasort)

target_sources(app PRIVATE
    src/main.cpp
    src/components/sensor_service/digital_sensor_component.cpp
    src/components/sensor_service/pass_counter_component.cpp
    src/components/wifi_service/wifi_service.cpp
    src/components/wifi_service/internet_ugm.cpp
    src/components/supabase_service/supabase_service.cpp
    src/components/ota_service/ota_service.cpp
)

# Add subdirectories as include paths if you use relative header paths
target_include_directories(app PRIVATE
    src/
    src/components
    src/services
    src/contexts
)

zephyr_linker_sources(SECTIONS src/components/ota_service/sections-rom.ld)
zephyr_linker_section(
    NAME http_resource_desc_ota_http_service
    KVMA RAM_REGION GROUP RODATA_REGION
)