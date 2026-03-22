cmake_minimum_required(VERSION 3.21)

project(eightd_chess_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(eightd_engine
    src/engine.cpp
)

target_include_directories(eightd_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eightd_engine PUBLIC cxx_std_20)

add_executable(eightd_protocol
    src/protocol_main.cpp
)

target_link_libraries(eightd_protocol PRIVATE eightd_engine)

add_executable(eightd_evo
    src/evo_main.cpp
)

target_link_libraries(eightd_evo PRIVATE eightd_engine)

add_executable(eightd_bridge
    src/bridge_main.cpp
)

target_link_libraries(eightd_bridge PRIVATE eightd_engine ws2_32)

add_executable(eightd_engine_tests
    tests/engine_tests.cpp
)

target_link_libraries(eightd_engine_tests PRIVATE eightd_engine)

enable_testing()
add_test(NAME eightd_engine_tests COMMAND eightd_engine_tests)
