cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aquasort)

target_sources(app PRIVATE
    src/main.cpp
    src/components/sensor_service/digital_sensor_component.cpp
    src/components/wifi_service/wifi_service.cpp
    src/components/supabase_service/supabase_service.cpp
)

# Add subdirectories as include paths if you use relative header paths
target_include_directories(app PRIVATE
    src/
    src/components
    src/services
    src/contexts
)