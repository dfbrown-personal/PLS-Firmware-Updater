cmake_minimum_required(VERSION 3.20)

project(pls_firmware_updater LANGUAGES C)

option(PLS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(flash_sim src/flash_sim.c)
target_include_directories(flash_sim PUBLIC include)
target_compile_features(flash_sim PUBLIC c_std_11)
target_compile_options(flash_sim PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(PLS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(flash_sim PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(flash_sim PUBLIC -fsanitize=address,undefined)
endif()

enable_testing()

add_executable(test_flash_sim tests/test_flash_sim.c)
target_link_libraries(test_flash_sim PRIVATE flash_sim)
target_compile_options(test_flash_sim PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME flash_sim COMMAND test_flash_sim)
