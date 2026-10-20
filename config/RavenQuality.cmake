include_guard(GLOBAL)
set(_raven_config "${CMAKE_CURRENT_LIST_DIR}")
foreach(check FORMAT TIDY IWYU WARNINGS WERROR)
  option(RAVEN_QUALITY_${check} "Enable Raven ${check} checks" ON)
endforeach()
option(RAVEN_QUALITY_CPPCHECK "Enable optional cppcheck checks" OFF)
set(RAVEN_SANITIZER
    "none"
    CACHE STRING "Native test instrumentation: none, address-undefined, thread")
set_property(CACHE RAVEN_SANITIZER PROPERTY STRINGS none address-undefined thread)
if(NOT RAVEN_SANITIZER STREQUAL "none")
  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "RAVEN_SANITIZER is for native tests; production cross builds must use none")
  endif()
  if(RAVEN_SANITIZER STREQUAL "address-undefined")
    set(_raven_sanitizer "-fsanitize=address,undefined")
  elseif(RAVEN_SANITIZER STREQUAL "thread")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      message(FATAL_ERROR "ThreadSanitizer tests require matching Yocto Clang/compiler-rt")
    endif()
    set(_raven_sanitizer "-fsanitize=thread")
  else()
    message(FATAL_ERROR "Unknown RAVEN_SANITIZER: ${RAVEN_SANITIZER}")
  endif()
  add_compile_options("${_raven_sanitizer}" -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options("${_raven_sanitizer}" -fno-sanitize-recover=all)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(RAVEN_QUALITY_WARNINGS)
  include(CheckCXXCompilerFlag)
  foreach(
    flag
    -Wall
    -Wextra
    -Wpedantic
    -Wformat=2
    -Wformat-security
    -Wshadow
    -Wundef
    -Wcast-align
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Woverloaded-virtual
    -Wnon-virtual-dtor
    -Wthread-safety)
    string(MAKE_C_IDENTIFIER "${flag}" flag_id)
    check_cxx_compiler_flag("${flag}" "RAVEN_HAS_${flag_id}")
    if(RAVEN_HAS_${flag_id})
      add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${flag}>")
    endif()
  endforeach()
endif()
if(RAVEN_QUALITY_WERROR)
  add_compile_options(-Werror)
endif()
# Policy edits must invalidate native objects as well as Yocto task signatures.
file(GLOB_RECURSE _raven_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/*.cpp" "${PROJECT_SOURCE_DIR}/*.h"
     "${PROJECT_SOURCE_DIR}/*.hpp")
if(_raven_sources)
  set_property(
    SOURCE ${_raven_sources}
    APPEND
    PROPERTY OBJECT_DEPENDS "${_raven_config}/.clang-tidy" "${_raven_config}/.clang-format"
             "${_raven_config}/RavenQuality.cmake" "${_raven_config}/iwyu.imp")
endif()
if(RAVEN_QUALITY_FORMAT)
  find_program(RAVEN_CLANG_FORMAT clang-format REQUIRED)
  add_custom_target(
    raven-format-check ALL
    COMMAND "${RAVEN_CLANG_FORMAT}" "--style=file:${_raven_config}/.clang-format" --dry-run --Werror ${_raven_sources}
    COMMENT "Checking C++ formatting"
    VERBATIM)
endif()
# Clang tools need their own intrinsic headers before GCC's target includes.
# libstdc++ still comes from the target sysroot; GCC ARM NEON builtins cannot
# be parsed by Clang when Eigen enables vectorization.
if(RAVEN_QUALITY_TIDY OR RAVEN_QUALITY_IWYU)
  find_program(RAVEN_CLANG clang REQUIRED)
  execute_process(
    COMMAND "${RAVEN_CLANG}" -print-resource-dir
    RESULT_VARIABLE _raven_resource_result
    OUTPUT_VARIABLE _raven_resource_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _raven_resource_result STREQUAL "0" OR NOT IS_DIRECTORY "${_raven_resource_dir}/include")
    message(FATAL_ERROR "Cannot locate the Yocto Clang intrinsic headers")
  endif()
endif()
if(RAVEN_QUALITY_TIDY)
  find_program(RAVEN_CLANG_TIDY clang-tidy REQUIRED)
  execute_process(
    COMMAND "${RAVEN_CLANG_TIDY}" --verify-config "--config-file=${_raven_config}/.clang-tidy"
    RESULT_VARIABLE _raven_tidy_result
    OUTPUT_VARIABLE _raven_tidy_output
    ERROR_VARIABLE _raven_tidy_error)
  if(NOT _raven_tidy_result STREQUAL "0")
    message(FATAL_ERROR "Invalid clang-tidy policy: ${_raven_tidy_output}${_raven_tidy_error}")
  endif()
  set(CMAKE_CXX_CLANG_TIDY "${RAVEN_CLANG_TIDY}" "--config-file=${_raven_config}/.clang-tidy"
                           "--header-filter=^${PROJECT_SOURCE_DIR}/|/raven/contracts/" --system-headers)
  list(APPEND CMAKE_CXX_CLANG_TIDY "--extra-arg=-Wthread-safety" "--extra-arg=-isystem${_raven_resource_dir}/include")
  # Clang must see the target GCC's standard library, not the host's headers.
  foreach(dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    list(APPEND CMAKE_CXX_CLANG_TIDY "--extra-arg=-isystem${dir}")
  endforeach()
  if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    list(APPEND CMAKE_CXX_CLANG_TIDY "--extra-arg=--target=aarch64-linux-gnu")
  endif()
  if(NOT RAVEN_QUALITY_WERROR)
    list(APPEND CMAKE_CXX_CLANG_TIDY "--warnings-as-errors=")
  endif()
endif()
if(RAVEN_QUALITY_CPPCHECK)
  find_program(RAVEN_CPPCHECK cppcheck REQUIRED)
  set(CMAKE_CXX_CPPCHECK
      "${RAVEN_CPPCHECK};--enable=warning,performance,portability;--inline-suppr;--quiet;--check-level=exhaustive")
  # Third-party sysroot headers retain their upstream coding policy.
  if(CMAKE_SYSROOT)
    list(APPEND CMAKE_CXX_CPPCHECK "--suppress=*:${CMAKE_SYSROOT}/*")
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND CMAKE_CXX_CPPCHECK --platform=unix64)
  else()
    list(APPEND CMAKE_CXX_CPPCHECK --platform=unix32)
  endif()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    list(APPEND CMAKE_CXX_CPPCHECK -D__aarch64__=1)
  endif()
  if(RAVEN_QUALITY_WERROR)
    list(APPEND CMAKE_CXX_CPPCHECK --error-exitcode=1)
  endif()
endif()

if(RAVEN_QUALITY_IWYU)
  # Older CMake versions ignore IWYU's error exit status.
  if(RAVEN_QUALITY_WERROR AND CMAKE_VERSION VERSION_LESS 3.30)
    message(FATAL_ERROR "Strict IWYU checks require CMake 3.30 or newer; use the Yocto toolchain")
  endif()
  find_program(RAVEN_IWYU include-what-you-use REQUIRED)
  execute_process(
    COMMAND "${RAVEN_IWYU}" --version
    RESULT_VARIABLE _raven_iwyu_result
    OUTPUT_VARIABLE _raven_iwyu_output
    ERROR_VARIABLE _raven_iwyu_error)
  if(NOT _raven_iwyu_result STREQUAL "0")
    message(FATAL_ERROR "IWYU cannot run: ${_raven_iwyu_output}${_raven_iwyu_error}")
  endif()
  # Vendor umbrella headers expose their private definitions; keep public APIs.
  # IWYU enforces includes. Compiler warnings remain enforced by the compiler
  # and clang-tidy; do not duplicate compiler-specific warnings in this parser.
  set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE
      "${RAVEN_IWYU}"
      -w
      -Xiwyu
      --no_fwd_decls
      -Xiwyu
      "--mapping_file=${_raven_config}/iwyu.imp"
      -Xiwyu
      "--check_also=${PROJECT_SOURCE_DIR}/*"
      "-isystem${_raven_resource_dir}/include")
  foreach(dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    list(APPEND CMAKE_CXX_INCLUDE_WHAT_YOU_USE "-isystem${dir}")
  endforeach()
  if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    list(APPEND CMAKE_CXX_INCLUDE_WHAT_YOU_USE --target=aarch64-linux-gnu)
  endif()
  if(RAVEN_QUALITY_WERROR)
    list(APPEND CMAKE_CXX_INCLUDE_WHAT_YOU_USE -Xiwyu --error=1)
  endif()
endif()
