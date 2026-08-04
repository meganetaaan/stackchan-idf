# SPDX-FileCopyrightText: 2026 Shinya Ishikawa
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.16)
project(clap_dance_host_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CLAP_DANCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(clap_dance_host STATIC
    ${CLAP_DANCE_ROOT}/clap_dance.cpp
)
target_include_directories(clap_dance_host PUBLIC ${CLAP_DANCE_ROOT}/include)
target_compile_options(clap_dance_host PRIVATE -Wall -Wextra -Werror)

enable_testing()

add_executable(clap_dance_test_controller test_controller.cpp)
target_link_libraries(clap_dance_test_controller PRIVATE clap_dance_host)
target_compile_options(clap_dance_test_controller PRIVATE -Wall -Wextra -Werror)
add_test(NAME controller COMMAND clap_dance_test_controller)
