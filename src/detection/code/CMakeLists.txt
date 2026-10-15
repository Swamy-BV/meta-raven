cmake_minimum_required(VERSION 3.20)
project(
  detection
  VERSION 1.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
find_package(nlohmann_json REQUIRED)
install(FILES detection.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
