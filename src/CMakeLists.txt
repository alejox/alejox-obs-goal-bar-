# Targets shared by the plugin build and the standalone test build. Nothing here
# may depend on OBS or Qt.

# buildspec.json is the single source of the version, for both builds.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../buildspec.json" goal_bar_buildspec)
string(JSON goal_bar_version GET "${goal_bar_buildspec}" version)

add_library(
  goal-bar-core STATIC
  core/action_sequence.cpp
  core/cancel_token.cpp
  core/celebration.cpp
  core/compact_number.cpp
  core/goal.cpp
  core/goal_reading.cpp
  core/goal_settings.cpp
  core/goal_view.cpp
  core/manual_provider.cpp
  core/metric.cpp
  core/poll_schedule.cpp
  core/style_preset.cpp
  core/subscriber_rounding.cpp
  core/version.cpp
  core/youtube_page.cpp)
target_include_directories(goal-bar-core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(goal-bar-core PRIVATE GOAL_BAR_VERSION="${goal_bar_version}")
target_compile_features(goal-bar-core PUBLIC cxx_std_17)
set_target_properties(goal-bar-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Metric provider adapters. Plain C++ and threads: HTTP and logging are injected
# by the OBS adapter, so the tests can drive them with fakes.
find_package(Threads REQUIRED)
add_library(goal-bar-providers STATIC providers/youtube_public_provider.cpp)
target_link_libraries(goal-bar-providers PUBLIC goal-bar-core Threads::Threads)
set_target_properties(goal-bar-providers PROPERTIES POSITION_INDEPENDENT_CODE ON)
