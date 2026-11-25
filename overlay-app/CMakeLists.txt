# AC Goal Bar Overlay: the Goal Bar as a desktop app for Meld Studio (or any app with a
# browser source). Built on its own, apart from the OBS plugin; it shares the plugin's
# core, providers, system adapters and painter from ../src.
#
#   cmake -S overlay-app -B build_overlay -DCMAKE_PREFIX_PATH=<Qt 6>
#   cmake --build build_overlay
cmake_minimum_required(VERSION 3.22)

file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" overlay_version)
string(STRIP "${overlay_version}" overlay_version)
project(ac-goal-bar-overlay VERSION ${overlay_version} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)
if(MSVC)
  add_compile_options(/W3 /utf-8)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(GOAL_BAR_SYSTEM ON)
add_subdirectory(../src goal-bar-src)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(app_sources
  src/main.cpp
  src/main_window.cpp
  src/translator.cpp
  ../src/render/card_painter.cpp
  ../src/render/goal_bar_painter.cpp
  ../src/ui/support_dialog.cpp
  ../src/ui/twitch_connect_dialog.cpp)

add_executable(ac-goal-bar-overlay WIN32 MACOSX_BUNDLE ${app_sources})
target_include_directories(ac-goal-bar-overlay PRIVATE src ../src)
target_link_libraries(ac-goal-bar-overlay PRIVATE goal-bar-app-core goal-bar-system Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(ac-goal-bar-overlay PRIVATE GOAL_BAR_OVERLAY_VERSION="${PROJECT_VERSION}")
set_target_properties(ac-goal-bar-overlay PROPERTIES AUTOMOC ON)

# data/: the plugin's locale files and fonts plus the app's own texts.
set(data_files)
foreach(file IN ITEMS locale/en-US.ini locale/es-ES.ini fonts/VT323-Regular.ttf fonts/MrDafoe-Regular.ttf
                      fonts/Orbitron-VariableFont_wght.ttf fonts/Anton-Regular.ttf fonts/OFL-Anton.txt
                      fonts/OFL-MrDafoe.txt fonts/OFL-Orbitron.txt fonts/OFL-VT323.txt)
  list(APPEND data_files "${CMAKE_CURRENT_LIST_DIR}/../data/${file}|${file}")
endforeach()
foreach(file IN ITEMS en-US.ini es-ES.ini)
  list(APPEND data_files "${CMAKE_CURRENT_LIST_DIR}/locale/${file}|app-locale/${file}")
endforeach()

if(APPLE)
  set_target_properties(
    ac-goal-bar-overlay
    PROPERTIES OUTPUT_NAME "AC Goal Bar Overlay"
               MACOSX_BUNDLE_BUNDLE_NAME "AC Goal Bar Overlay"
               MACOSX_BUNDLE_GUI_IDENTIFIER "com.alejoxcode.goal-bar-overlay"
               MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
               MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
               INSTALL_RPATH "@executable_path/../Frameworks"
               BUILD_WITH_INSTALL_RPATH ON)
  set(data_root "$<TARGET_BUNDLE_CONTENT_DIR:ac-goal-bar-overlay>/Resources/data")
else()
  set(data_root "$<TARGET_FILE_DIR:ac-goal-bar-overlay>/data")
endif()

foreach(entry IN LISTS data_files)
  string(REPLACE "|" ";" pair "${entry}")
  list(GET pair 0 source)
  list(GET pair 1 destination)
  add_custom_command(
    TARGET ac-goal-bar-overlay
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "${data_root}/${destination}"
    VERBATIM)
endforeach()
