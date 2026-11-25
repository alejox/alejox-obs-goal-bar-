cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "obs-goal-bar requires ENABLE_FRONTEND_API and ENABLE_QT.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_subdirectory(src)

# The system adapters (HTTPS, WebSocket, secret store, providers, Twitch account) shared by the
# plugin and the overlay app. Neither OBS nor Qt.
# System HTTPS stack: OBS does not ship Qt's TLS plugins on Windows or macOS, so
# QtNetwork cannot do HTTPS inside OBS (same approach as multi-chat).
# OS secret store for the Twitch refresh token (Keychain, Credential Manager,
# Secret Service), adapted from multi-stream.
add_library(goal-bar-system STATIC)
target_sources(
  goal-bar-system
  PRIVATE src/system/log.cpp src/system/providers.cpp src/system/twitch_services.cpp src/secrets/platform_secret_store.cpp
)
target_link_libraries(goal-bar-system PUBLIC goal-bar-providers)
target_include_directories(goal-bar-system PUBLIC src)
set_target_properties(goal-bar-system PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(OS_WINDOWS)
  target_sources(goal-bar-system PRIVATE src/net/net-win.cpp src/secrets/windows_credential_store.cpp)
  target_link_libraries(goal-bar-system PUBLIC winhttp Advapi32)
elseif(OS_MACOS)
  enable_language(OBJCXX)
  target_sources(goal-bar-system PRIVATE src/net/net-mac.mm src/secrets/keychain_secret_store.cpp)
  target_link_libraries(
    goal-bar-system
    PUBLIC
      "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>"
      "$<LINK_LIBRARY:FRAMEWORK,Security.framework>"
  )
else()
  find_package(CURL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
  target_sources(goal-bar-system PRIVATE src/net/net-linux.cpp src/secrets/linux_secret_store.cpp)
  target_link_libraries(goal-bar-system PUBLIC CURL::libcurl PkgConfig::LIBSECRET)
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE goal-bar-core goal-bar-providers goal-bar-system goal-bar-update OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets
)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin_main.cpp
    src/obs/goal_actions.cpp
    src/obs/goal_bar_source.cpp
    src/obs/obs_shutdown.cpp
    src/obs/ui_text.cpp
    src/render/card_painter.cpp
    src/render/goal_bar_painter.cpp
    src/ui/support_dialog.cpp
    src/ui/twitch_connect_dialog.cpp
    src/ui/update_controller.cpp
    src/update/update_service.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
