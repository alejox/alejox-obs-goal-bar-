# Targets shared by the plugin build and the standalone test build. Nothing here
# may depend on OBS or Qt.

# buildspec.json is the single source of the version, for both builds.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../buildspec.json" goal_bar_buildspec)
string(JSON goal_bar_version GET "${goal_bar_buildspec}" version)

# JSON parsing for the Twitch API answers (header only, pinned like multi-stream).
include(FetchContent)
FetchContent_Declare(
  nlohmann_json_header
  # Same file at the same tag; CMake tries the URLs in order, so a GitHub release
  # download outage falls back to raw.githubusercontent.com.
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
      https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
  URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json_header)

add_library(
  goal-bar-core STATIC
  core/action_sequence.cpp
  core/cancel_token.cpp
  core/chat_events.cpp
  core/celebration.cpp
  core/compact_number.cpp
  core/goal.cpp
  core/goal_reading.cpp
  core/goal_settings.cpp
  core/goal_view.cpp
  core/kick_api.cpp
  core/manual_provider.cpp
  core/metric.cpp
  core/poll_schedule.cpp
  core/progress_tween.cpp
  core/sha256.cpp
  core/style_preset.cpp
  core/subscriber_rounding.cpp
  core/tiktok_live.cpp
  core/twitch_auth.cpp
  core/twitch_helix.cpp
  core/version.cpp
  core/youtube_page.cpp)
target_include_directories(goal-bar-core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(goal-bar-core SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
target_compile_definitions(goal-bar-core PRIVATE GOAL_BAR_VERSION="${goal_bar_version}")
target_compile_features(goal-bar-core PUBLIC cxx_std_17)
set_target_properties(goal-bar-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Metric provider adapters. Plain C++ and threads: HTTP and logging are injected
# by the OBS adapter, so the tests can drive them with fakes.
find_package(Threads REQUIRED)
add_library(
  goal-bar-providers STATIC
  providers/chat_count_provider.cpp
  providers/chat_sessions.cpp
  providers/kick_provider.cpp
  providers/tiktok_provider.cpp
  providers/twitch_account.cpp
  providers/twitch_provider.cpp
  providers/youtube_public_provider.cpp)
target_link_libraries(goal-bar-providers PUBLIC goal-bar-core Threads::Threads)
set_target_properties(goal-bar-providers PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Updater logic: release check, staging manifest, installer script and settings. Plain
# C++ and the standard library; the Qt service in the plugin does the network and UI.
add_library(
  goal-bar-update STATIC
  update/atomic_file.cpp
  update/install_plan.cpp
  update/staging.cpp
  update/update_check.cpp
  update/update_settings.cpp
  update/update_settings_file.cpp)
target_link_libraries(goal-bar-update PUBLIC goal-bar-core)
target_include_directories(goal-bar-update SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
set_target_properties(goal-bar-update PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Overlay app logic without Qt: settings, the goal engine and the local web server (HTTP
# parsing and loopback sockets), tested with the core.
add_library(goal-bar-app-core STATIC app/frame_service.cpp app/goal_engine.cpp app/http.cpp app/http_server.cpp app/overlay_settings.cpp)
target_link_libraries(goal-bar-app-core PUBLIC goal-bar-core goal-bar-providers Threads::Threads)
target_include_directories(goal-bar-app-core SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
if(WIN32)
  target_link_libraries(goal-bar-app-core PUBLIC ws2_32)
endif()

# The system adapters (HTTPS, WebSocket, secret store, provider factory, Twitch account),
# shared by the OBS plugin and the overlay app; neither OBS nor Qt. Off for the tests,
# which use fakes. The system HTTPS stack is used because OBS does not ship Qt's TLS
# plugins on Windows or macOS (same approach as multi-chat); the OS secret store keeps
# the Twitch refresh token (Keychain, Credential Manager, Secret Service).
option(GOAL_BAR_SYSTEM "Build the system adapters (network, secret store)" OFF)
if(GOAL_BAR_SYSTEM)
  add_library(
    goal-bar-system STATIC
    system/log.cpp
    system/providers.cpp
    system/twitch_services.cpp
    secrets/platform_secret_store.cpp)
  target_link_libraries(goal-bar-system PUBLIC goal-bar-providers)
  set_target_properties(goal-bar-system PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(WIN32)
    target_sources(goal-bar-system PRIVATE net/net-win.cpp secrets/windows_credential_store.cpp)
    target_link_libraries(goal-bar-system PUBLIC winhttp Advapi32)
  elseif(APPLE)
    enable_language(OBJCXX)
    target_sources(goal-bar-system PRIVATE net/net-mac.mm secrets/keychain_secret_store.cpp)
    target_link_libraries(
      goal-bar-system
      PUBLIC
        "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>"
        "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>"
        "$<LINK_LIBRARY:FRAMEWORK,Security.framework>")
  else()
    find_package(CURL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
    target_sources(goal-bar-system PRIVATE net/net-linux.cpp secrets/linux_secret_store.cpp)
    target_link_libraries(goal-bar-system PUBLIC CURL::libcurl PkgConfig::LIBSECRET)
  endif()
endif()
