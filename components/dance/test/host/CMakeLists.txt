cmake_minimum_required(VERSION 3.16)
project(dance_host_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DANCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(dance STATIC
    ${DANCE_ROOT}/dance.cpp
    ${DANCE_ROOT}/clap_tempo_estimator.cpp
    ${DANCE_ROOT}/head_stroke_detector.cpp
    ${DANCE_ROOT}/dance_mode_input.cpp
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

add_executable(test_head_stroke test_head_stroke.cpp)
target_link_libraries(test_head_stroke PRIVATE dance)
target_compile_options(test_head_stroke PRIVATE -Wall -Wextra -Werror)
add_test(NAME head_stroke_detector COMMAND test_head_stroke)

add_executable(test_dance_mode test_dance_mode.cpp)
target_link_libraries(test_dance_mode PRIVATE dance)
target_compile_options(test_dance_mode PRIVATE -Wall -Wextra -Werror)
add_test(NAME dance_mode_input COMMAND test_dance_mode)
