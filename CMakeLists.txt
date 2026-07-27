cmake_minimum_required(VERSION 3.15)
project(veto-daq VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)

find_path(ASIO_INCLUDE_DIR asio.hpp)
if(NOT ASIO_INCLUDE_DIR)
    message(FATAL_ERROR "asio not found (install libasio-dev)")
endif()

add_executable(veto-daq
    src/main.cc
    src/daq_controller.cc
    src/board_manager.cc
    src/daq_log.cc
    src/disk_writer.cc
    src/veto_board_client.cc
)

target_include_directories(veto-daq PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/external
    ${ASIO_INCLUDE_DIR}
)

target_link_libraries(veto-daq PRIVATE
    yaml-cpp
    spdlog::spdlog
    pthread
)

add_executable(veto_data_test
    tests/veto_data_test.cc
)

target_include_directories(veto_data_test PRIVATE
    ${ASIO_INCLUDE_DIR}
)

target_link_libraries(veto_data_test PRIVATE
    pthread
)
