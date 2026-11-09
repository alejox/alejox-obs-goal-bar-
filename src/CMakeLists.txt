# Targets shared by the plugin build and the standalone test build. Nothing here
# may depend on OBS or Qt.

# buildspec.json is the single source of the version, for both builds.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../buildspec.json" goal_bar_buildspec)
string(JSON goal_bar_version GET "${goal_bar_buildspec}" version)

add_library(
  goal-bar-core STATIC
  core/goal.cpp
  core/goal_settings.cpp
  core/manual_provider.cpp
  core/metric.cpp
  core/subscriber_rounding.cpp
  core/version.cpp)
target_include_directories(goal-bar-core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(goal-bar-core PRIVATE GOAL_BAR_VERSION="${goal_bar_version}")
target_compile_features(goal-bar-core PUBLIC cxx_std_17)
set_target_properties(goal-bar-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
