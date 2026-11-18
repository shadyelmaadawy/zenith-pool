cmake_minimum_required(VERSION 3.15)

project(zenith-pool VERSION 0.1.0 LANGUAGES C)

if(NOT WIN32)
  message(FATAL_ERROR "zenith-pool supports Windows only for now.")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "zenith-pool needs the MSVC toolchain (cl.exe or clang-cl).")
endif()

# ---- layout: edit these if your tree differs -------------------------------------------------
set(ZT_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(ZT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(NOT EXISTS "${ZT_INCLUDE_DIR}")
  set(ZT_INCLUDE_DIR "${ZT_SOURCE_DIR}")
endif()

# ---- sources: portable files + the backend of the current platform only ----------------------
# A file ending in _win32.c is the Windows backend, _linux.c / _posix.c / _macos.c are future backends.
# A file with no platform suffix is portable and is always built.
set(ZT_PLATFORM win32)
set(ZT_ALL_PLATFORMS win32 linux posix macos)

file(GLOB_RECURSE ZT_SOURCES CONFIGURE_DEPENDS "${ZT_SOURCE_DIR}/*.c")
list(FILTER ZT_SOURCES EXCLUDE REGEX "/(main|test_[^/]*)\\.c$")
foreach(_platform IN LISTS ZT_ALL_PLATFORMS)
  if(NOT _platform STREQUAL ZT_PLATFORM)
    list(FILTER ZT_SOURCES EXCLUDE REGEX "_${_platform}\\.c$")
  endif()
endforeach()

if(NOT ZT_SOURCES)
  message(FATAL_ERROR "No .c files found in ${ZT_SOURCE_DIR}")
endif()
list(LENGTH ZT_SOURCES ZT_SOURCE_COUNT)
message(STATUS "zenith-pool: ${ZT_SOURCE_COUNT} source files, platform ${ZT_PLATFORM}")

# ---- no C runtime ---------------------------------------------------------------------------
# /RTC1 (Debug runtime checks) calls into the C runtime, /W3 would clash with /W4.
string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

add_library(zenith_pool STATIC ${ZT_SOURCES})

set_target_properties(zenith_pool PROPERTIES
  OUTPUT_NAME "zenith-pool"
  DEBUG_POSTFIX "_d"
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  # A generator expression stops Visual Studio from adding a Release\ or Debug\ sub folder.
  ARCHIVE_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")

target_include_directories(zenith_pool
  PUBLIC  "$<BUILD_INTERFACE:${ZT_INCLUDE_DIR}>"
  PRIVATE "${ZT_SOURCE_DIR}")

target_compile_options(zenith_pool PRIVATE
  /W4          # warnings
  /GS-         # no stack cookies: they need __security_cookie from the C runtime
  /Zl          # do not record a default C runtime library in the object files
  /Gy          # one COMDAT per function, so unused functions are dropped at link time
  /utf-8)

target_link_libraries(zenith_pool PUBLIC kernel32 bcrypt)   # bcrypt: BCryptGenRandom in the random module