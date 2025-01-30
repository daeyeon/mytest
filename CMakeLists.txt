cmake_minimum_required(VERSION 2.8.12)

project(mytest)

add_library(mytest INTERFACE)
target_include_directories(mytest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(sample.x sample.cc)
target_link_libraries(sample.x PRIVATE mytest)
