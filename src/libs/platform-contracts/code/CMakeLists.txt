cmake_minimum_required(VERSION 3.20)
project(
  platform_contracts
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(platform-contracts INTERFACE)
add_library(Raven::platform-contracts ALIAS platform-contracts)
target_compile_features(platform-contracts INTERFACE cxx_std_17)
target_include_directories(platform-contracts INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS platform-contracts EXPORT PlatformContractsTargets)
install(
  EXPORT PlatformContractsTargets
  FILE PlatformContractsConfig.cmake
  NAMESPACE Raven::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PlatformContracts)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/PlatformContractsConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PlatformContractsConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PlatformContracts)
