cmake_minimum_required(VERSION 3.10)

project("asio")

add_library(asio INTERFACE)
target_include_directories(asio INTERFACE include)
set(ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" PARENT_SCOPE)