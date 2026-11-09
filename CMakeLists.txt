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

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE goal-bar-core goal-bar-providers OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets
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
    src/obs/provider_factory.cpp
    src/render/goal_bar_painter.cpp
)

# System HTTPS stack: OBS does not ship Qt's TLS plugins on Windows or macOS, so
# QtNetwork cannot do HTTPS inside OBS (same approach as multi-chat).
if(OS_WINDOWS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net/net-win.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winhttp)
elseif(OS_MACOS)
  enable_language(OBJCXX)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net/net-mac.mm)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>")
else()
  find_package(CURL REQUIRED)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net/net-linux.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
