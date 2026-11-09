# Standalone core tests: configures with plain CMake and needs neither OBS nor Qt.
#
#   cmake -S tests -B build_tests
#   cmake --build build_tests
#   ctest --test-dir build_tests --output-on-failure
cmake_minimum_required(VERSION 3.28...3.30)

project(obs-goal-bar-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)
if(MSVC)
  add_compile_options(/W3 /utf-8)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(../src src)

add_library(test_harness STATIC support/test_harness.cpp)
target_include_directories(test_harness PUBLIC support)

enable_testing()

function(add_core_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE test_harness goal-bar-core)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

add_core_test(version_test core/version_test.cpp)
add_core_test(metric_test core/metric_test.cpp)
add_core_test(goal_test core/goal_test.cpp)
add_core_test(subscriber_rounding_test core/subscriber_rounding_test.cpp)
add_core_test(manual_provider_test core/manual_provider_test.cpp)
add_core_test(goal_settings_test core/goal_settings_test.cpp)
add_core_test(compact_number_test core/compact_number_test.cpp)
add_core_test(goal_view_test core/goal_view_test.cpp)
add_core_test(style_preset_test core/style_preset_test.cpp)
