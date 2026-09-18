cmake_minimum_required(VERSION 3.20)

# When consumed as a Zephyr module, the zephyr/CMakeLists.txt takes over.
if(DEFINED ZEPHYR_BASE)
  return()
endif()

project(microconf VERSION 1.0.1 LANGUAGES C CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(MCONF_BUILD_TESTS "Build microconf test and consumer targets" ON)
option(MCONF_STRICT_WARNINGS "Enable strict warnings" ON)
option(MCONF_CONFIG_ENABLE_NAMES "Enable key lookup support" ON)
option(MCONF_CONFIG_ENABLE_FLOAT "Enable float32 support" ON)

set(MCONF_CONFIG_MAX_ENTRIES 64 CACHE STRING "Maximum schema entries")
set(MCONF_CONFIG_MAX_KEY_LEN 48 CACHE STRING "Maximum key length")

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mconf_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/mconf_config.h
  @ONLY
)

add_library(microconf src/mconf.c)
add_library(microconf::microconf ALIAS microconf)

target_compile_features(microconf PUBLIC c_std_99)
target_include_directories(microconf
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MCONF_STRICT_WARNINGS)
  if(MSVC)
    target_compile_options(microconf PRIVATE /W4 /WX)
  else()
    target_compile_options(microconf PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)
  endif()
endif()

install(TARGETS microconf EXPORT microconfTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  include/mconf.h
  ${CMAKE_CURRENT_BINARY_DIR}/generated/mconf_config.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/microconfConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/microconfConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/microconf
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/microconfConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(EXPORT microconfTargets
  FILE microconfTargets.cmake
  NAMESPACE microconf::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/microconf
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/microconfConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/microconfConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/microconf
)

if(MCONF_BUILD_TESTS)
  add_executable(microconf_test tests/test_all.c)
  target_link_libraries(microconf_test PRIVATE microconf::microconf)

  add_subdirectory(tests/consumers/add_subdirectory)
  add_subdirectory(tests/consumers/cpp)
endif()
