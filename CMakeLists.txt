cmake_minimum_required(VERSION 3.16)
project(scyrox-v6 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -fvisibility=hidden")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(scyrox-v6
    src/main.cpp
    src/scyroxdevice.cpp
    src/trayicon.cpp
    src/batteryicon.cpp
    src/settingsdialog.cpp
)

target_include_directories(scyrox-v6 PRIVATE src)

target_link_libraries(scyrox-v6 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

install(TARGETS scyrox-v6 DESTINATION bin)
install(FILES scyrox-v6.desktop DESTINATION share/applications)
# /dev/hidraw is 0600 root:root by default; without this rule the app gets
# EACCES. Note /etc/udev/rules.d wins over this path if it holds a copy of
# the same file.
install(FILES config/99-scyrox.rules DESTINATION lib/udev/rules.d)
