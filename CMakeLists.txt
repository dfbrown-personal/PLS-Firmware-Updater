cmake_minimum_required(VERSION 3.20)

project(pls_firmware_updater LANGUAGES C)

option(PLS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(pls_configure_target target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)

    if(PLS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endif()
endfunction()

add_library(flash_sim src/flash_sim.c)
target_include_directories(flash_sim PUBLIC include)
target_compile_features(flash_sim PUBLIC c_std_11)
pls_configure_target(flash_sim)

add_library(image_validation
    src/boot_selector.c
    src/crc32.c
    src/image.c
    src/sha256.c)
target_include_directories(image_validation PUBLIC include)
target_compile_features(image_validation PUBLIC c_std_11)
target_link_libraries(image_validation PUBLIC flash_sim)
pls_configure_target(image_validation)

enable_testing()

add_executable(test_flash_sim tests/test_flash_sim.c)
target_link_libraries(test_flash_sim PRIVATE flash_sim)
pls_configure_target(test_flash_sim)

add_executable(test_image tests/test_image.c)
target_link_libraries(test_image PRIVATE image_validation)
pls_configure_target(test_image)

add_test(NAME flash_sim COMMAND test_flash_sim)
add_test(NAME image_validation COMMAND test_image)
