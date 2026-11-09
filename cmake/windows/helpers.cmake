# CMake Windows helper functions module

include_guard(GLOBAL)

include(helpers_common)

# set_target_properties_plugin: Set target properties for a Windows plugin DLL
function(set_target_properties_plugin target)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs PROPERTIES)
  cmake_parse_arguments(PARSE_ARGV 0 _STPO "${options}" "${oneValueArgs}" "${multiValueArgs}")

  message(DEBUG "Setting additional properties for target ${target}...")

  while(_STPO_PROPERTIES)
    list(POP_FRONT _STPO_PROPERTIES key value)
    set_property(TARGET ${target} PROPERTY ${key} "${value}")
  endwhile()

  if(TARGET plugin-support)
    target_link_libraries(${target} PRIVATE plugin-support)
  endif()

  target_install_resources(${target})

  # The plugin is a MODULE library, which CMake installs as a LIBRARY artifact on every platform.
  # OBS loads third-party plugins from C:\ProgramData\obs-studio\plugins\<name>\bin\64bit\<name>.dll
  # with their data in ...\<name>\data (https://obsproject.com/kb/plugins-guide).
  install(TARGETS ${target} LIBRARY DESTINATION bin/64bit)
endfunction()

# target_install_resources: Install the data directory beside the plugin DLL
function(target_install_resources target)
  message(DEBUG "Installing resources for target ${target}...")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION data)
  endif()
endfunction()
