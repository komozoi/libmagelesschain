cmake_minimum_required(VERSION 3.15)
project(LibMagelessChain LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D LIBUS_NO_SSL")

include(FetchContent)

FetchContent_Declare(
    excessive
    GIT_REPOSITORY https://gitea.com/komozoi/excessive.git
    GIT_TAG v0.3.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(excessive)

file(GLOB SOURCES
        "src/blockchain/*.cpp"
        "src/storage/*.cpp"
)

add_library(LibMagelessChain ${SOURCES})

target_compile_options(LibMagelessChain PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-format
    -Wno-unused-parameter
    -Wno-unused-result
    -fexec-charset=UTF-8
    -fextended-identifiers
    -finput-charset=UTF-8
)

target_link_options(LibMagelessChain PRIVATE -fsanitize=address)
target_compile_options(LibMagelessChain PRIVATE -fsanitize=address)

target_include_directories(LibMagelessChain PUBLIC src)
target_link_libraries(LibMagelessChain PUBLIC excessive)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    find_package(GTest)
    if(GTest_FOUND)
        file(GLOB_RECURSE TEST_SOURCES
            "src/blockchain/tests/*.cpp"
            "src/storage/tests/*.cpp"
            "src/testutil/*.cpp"
        )

        add_executable(mageless_tests src/testmain.cpp ${TEST_SOURCES} ${SOURCES})
        target_include_directories(mageless_tests PRIVATE src ${KTRANSFORMERS_INCLUDE_DIRS} ${excessive_SOURCE_DIR}/include)
        target_compile_options(mageless_tests PRIVATE
            -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter -Wno-unused-result
            -fexec-charset=UTF-8 -fextended-identifiers -finput-charset=UTF-8
            -fsanitize=address
        )
        target_link_options(mageless_tests PRIVATE -fsanitize=address)
        target_link_libraries(mageless_tests PRIVATE
            excessive
            GTest::gtest
            GTest::gtest_main
        )
    endif()
endif()

