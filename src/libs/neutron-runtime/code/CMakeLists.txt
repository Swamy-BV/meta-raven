cmake_minimum_required(VERSION 3.20)
project(
  NeutronRuntime
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_path(TFLITE_INCLUDE_DIR tensorflow/lite/c/c_api.h REQUIRED)
add_library(neutron-runtime SHARED neutron_backend.cpp)
add_library(Neutron::Runtime ALIAS neutron-runtime)
set_target_properties(
  neutron-runtime
  PROPERTIES EXPORT_NAME Runtime
             VERSION ${PROJECT_VERSION}
             SOVERSION 1)
target_include_directories(neutron-runtime SYSTEM PUBLIC $<BUILD_INTERFACE:${TFLITE_INCLUDE_DIR}>)
target_include_directories(neutron-runtime PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                                  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/neutron-runtime>)
target_link_libraries(neutron-runtime PRIVATE ${CMAKE_DL_LIBS})
install(
  TARGETS neutron-runtime
  EXPORT NeutronRuntimeTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES neutron_backend.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/neutron-runtime)
set(NEUTRON_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/NeutronRuntime)
install(
  EXPORT NeutronRuntimeTargets
  NAMESPACE Neutron::
  DESTINATION ${NEUTRON_CMAKE_DIR})
configure_package_config_file(NeutronRuntimeConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/NeutronRuntimeConfig.cmake
                              INSTALL_DESTINATION ${NEUTRON_CMAKE_DIR})
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/NeutronRuntimeConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/NeutronRuntimeConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/NeutronRuntimeConfigVersion.cmake DESTINATION ${NEUTRON_CMAKE_DIR})
