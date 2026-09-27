cmake_minimum_required(VERSION 3.16)
project(dance_host_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DANCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(dance STATIC
    ${DANCE_ROOT}/dance.cpp
    ${DANCE_ROOT}/clap_tempo_estimator.cpp
)
target_include_directories(dance PUBLIC ${DANCE_ROOT}/include)
target_compile_options(dance PRIVATE -Wall -Wextra -Werror)

enable_testing()

add_executable(test_dance test_dance.cpp)
target_link_libraries(test_dance PRIVATE dance)
target_compile_options(test_dance PRIVATE -Wall -Wextra -Werror)
add_test(NAME dance_controller COMMAND test_dance)

add_executable(test_clap_tempo test_clap_tempo.cpp)
target_link_libraries(test_clap_tempo PRIVATE dance)
target_compile_options(test_clap_tempo PRIVATE -Wall -Wextra -Werror)
add_test(NAME clap_tempo_estimator COMMAND test_clap_tempo)
