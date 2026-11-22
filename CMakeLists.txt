# tofu-plugin-framework-cpp: the high-level C++ framework for OpenTofu / Terraform provider plugins.
# The C++ counterpart of github.com/hashicorp/terraform-plugin-framework. It sits on tofu-plugin-cpp (the counterpart of
# terraform-plugin-go): find_package(TofuPluginCpp), or the sibling folder ../tofu-plugin-cpp if that is not installed.
#
#   tofu::framework   (target tofu_plugin_framework)
cmake_minimum_required(VERSION 3.24)
project(tofu-plugin-framework-cpp VERSION 0.1.0 LANGUAGES C CXX)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Write compile_commands.json" FORCE)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()
option(TOFU_PLUGIN_FRAMEWORK_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})

# --- the low-level library -------------------------------------------------------------------------------------------
# An installed one (-DCMAKE_PREFIX_PATH=<its prefix>), else its source in the sibling folder, else TOFU_PLUGIN_CPP_SOURCE_DIR.
if(NOT TARGET tofu::plugin)
  find_package(TofuPluginCpp QUIET CONFIG)
  if(NOT TofuPluginCpp_FOUND)
    set(TOFU_PLUGIN_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tofu-plugin-cpp" CACHE PATH "tofu-plugin-cpp source folder")
    if(NOT EXISTS "${TOFU_PLUGIN_CPP_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR "tofu-plugin-cpp not found: install it and pass -DCMAKE_PREFIX_PATH=<prefix>, or put its source at "
              "${TOFU_PLUGIN_CPP_SOURCE_DIR} (or set TOFU_PLUGIN_CPP_SOURCE_DIR).")
    endif()
    message(STATUS "Using the tofu-plugin-cpp source at ${TOFU_PLUGIN_CPP_SOURCE_DIR}")
    add_subdirectory("${TOFU_PLUGIN_CPP_SOURCE_DIR}" tofu-plugin-cpp EXCLUDE_FROM_ALL)
  endif()
endif()

# --- the library -----------------------------------------------------------------------------------------------------
add_library(tofu_plugin_framework STATIC src/schema.cpp src/stock.cpp src/fwserver.cpp)
add_library(tofu::framework ALIAS tofu_plugin_framework)
target_include_directories(tofu_plugin_framework PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(tofu_plugin_framework PUBLIC tofu::plugin)
target_compile_features(tofu_plugin_framework PUBLIC cxx_std_23)
set_target_properties(tofu_plugin_framework PROPERTIES EXPORT_NAME framework)

if(TOFU_PLUGIN_FRAMEWORK_BUILD_TESTS)
  # tools/check-unit.sh runs this: the framework's behaviour, through tfprotov6 and without gRPC or OpenTofu.
  add_executable(unit-tests tests/unit-tests.cpp tests/types-tests.cpp tests/server-tests.cpp)
  target_link_libraries(unit-tests tofu::framework)
endif()

# --- install: find_package(TofuPluginFramework) -----------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS tofu_plugin_framework EXPORT TofuPluginFrameworkTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT TofuPluginFrameworkTargets NAMESPACE tofu:: FILE TofuPluginFrameworkTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TofuPluginFramework)
configure_package_config_file(cmake/TofuPluginFrameworkConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/TofuPluginFrameworkConfig.cmake"
                              INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TofuPluginFramework)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/TofuPluginFrameworkConfigVersion.cmake"
                                 VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/TofuPluginFrameworkConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/TofuPluginFrameworkConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TofuPluginFramework)
