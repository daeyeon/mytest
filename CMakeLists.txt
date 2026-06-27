cmake_minimum_required(VERSION 3.16)
project(mytest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(mytest INTERFACE)
target_include_directories(mytest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" BUILD_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
add_compile_definitions(BUILD_OUTPUT_DIR="${BUILD_OUTPUT_DIR}")

set(COMMON_TEST_LIBS pthread)
if(NOT APPLE)
  list(APPEND COMMON_TEST_LIBS rt)
endif()

add_executable(example.x test/00-example/example.cc)
target_link_libraries(example.x PRIVATE mytest ${COMMON_TEST_LIBS})

file(GLOB TEST_01_SRCS CONFIGURE_DEPENDS test/01-basics/*.cc)
add_executable(1-basics.x ${TEST_01_SRCS})
target_link_libraries(1-basics.x PRIVATE mytest ${COMMON_TEST_LIBS})

file(GLOB TEST_02_JOB_SRCS CONFIGURE_DEPENDS test/02-options/job-mode/*.cc)
add_executable(2-job.x ${TEST_02_JOB_SRCS})
target_link_libraries(2-job.x PRIVATE mytest ${COMMON_TEST_LIBS})

file(GLOB TEST_02_CLI_SRCS CONFIGURE_DEPENDS test/02-options/cli/*.cc)
add_executable(2-cli.x ${TEST_02_CLI_SRCS})
target_link_libraries(2-cli.x PRIVATE mytest ${COMMON_TEST_LIBS})

add_executable(3-hooks.x test/03-advanced/isolate-hooks-test.cc)
target_link_libraries(3-hooks.x PRIVATE mytest ${COMMON_TEST_LIBS})

add_executable(3-reporter.x test/03-advanced/report-writer-test.cc)
target_link_libraries(3-reporter.x PRIVATE mytest ${COMMON_TEST_LIBS})

add_executable(3-must-call.x test/03-advanced/must-call-test.cc)
target_link_libraries(3-must-call.x PRIVATE mytest ${COMMON_TEST_LIBS})

add_executable(3-match.x test/03-advanced/match-test.cc)
target_link_libraries(3-match.x PRIVATE mytest ${COMMON_TEST_LIBS})

add_executable(3-temp-path.x test/03-advanced/temp-path-test.cc)
target_link_libraries(3-temp-path.x PRIVATE mytest ${COMMON_TEST_LIBS})

enable_testing()
add_test(NAME example.x COMMAND example.x)
add_test(NAME 1-basics COMMAND 1-basics.x)
add_test(NAME 2-job COMMAND 2-job.x -j)
add_test(NAME 2-cli COMMAND 2-cli.x)
add_test(NAME 3-hooks COMMAND 3-hooks.x)
add_test(NAME 3-reporter COMMAND 3-reporter.x)
add_test(NAME 3-must-call COMMAND 3-must-call.x)
add_test(NAME 3-match COMMAND 3-match.x)
add_test(NAME 3-temp-path COMMAND 3-temp-path.x)

set_tests_properties(2-job PROPERTIES DEPENDS 1-basics)
set_tests_properties(2-cli PROPERTIES DEPENDS 2-job)
set_tests_properties(3-hooks PROPERTIES DEPENDS 2-cli)
set_tests_properties(3-reporter PROPERTIES DEPENDS 3-hooks)
set_tests_properties(3-must-call PROPERTIES DEPENDS 3-reporter)
set_tests_properties(3-match PROPERTIES DEPENDS 3-must-call)
set_tests_properties(3-temp-path PROPERTIES DEPENDS 3-match)
