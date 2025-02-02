cmake_minimum_required(VERSION 2.8.12)

project(mytest)

set(CMAKE_CXX_STANDARD 17)

add_library(mytest INTERFACE)
target_include_directories(mytest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(example.x example.cc)

file(GLOB TEST_SRCS test/*.cc)
add_executable(test.x ${TEST_SRCS})
target_link_libraries(test.x PRIVATE mytest)
