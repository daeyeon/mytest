cmake_minimum_required(VERSION 2.8.12)

project(mytest)

add_library(mytest INTERFACE)
target_include_directories(mytest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(example.x example.cc)
target_link_libraries(example.x PRIVATE mytest)

file(GLOB TEST_SRCS test/*.cc)
add_executable(test.x ${TEST_SRCS})
target_link_libraries(test.x PRIVATE mytest)
