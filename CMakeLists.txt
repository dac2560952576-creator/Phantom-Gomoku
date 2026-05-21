cmake_minimum_required(VERSION 3.20)

project(phantom_gomoku LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SFML 3 REQUIRED COMPONENTS System Window Graphics Network Audio)

add_executable(phantom_gomoku
    src/main.cpp
    src/Game.cpp
    src/Board.cpp
    src/NetworkServer.cpp
    src/NetworkClient.cpp
)

target_include_directories(phantom_gomoku PRIVATE src)

target_link_libraries(phantom_gomoku
    PRIVATE
        SFML::System
        SFML::Window
        SFML::Graphics
        SFML::Network
        SFML::Audio
)

if(MSVC)
    target_compile_options(phantom_gomoku PRIVATE /W4 /utf-8)
else()
    target_compile_options(phantom_gomoku PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
