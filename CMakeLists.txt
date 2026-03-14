cmake_minimum_required(VERSION 3.28)
project(io_uring LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "This project requires Clang as the C++ compiler")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "22.0")
  message(FATAL_ERROR
    "Clang >= 22 is required, found ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(IO_URING_GPU_TRIPLE "amdgcn-amd-amdhsa" CACHE STRING "GPU target triple")
set(IO_URING_GPU_ARCH "native" CACHE STRING "GPU target architecture")

option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)

if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
  else()
    message(WARNING "clang-tidy requested but not found")
  endif()
endif()

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wsign-conversion
  -Wshadow
  -Wformat=2
  -Wno-unused-parameter
)

find_package(hsa-runtime64 1.2.0 REQUIRED HINTS ${CMAKE_INSTALL_PREFIX}
             PATHS /opt/rocm)
message(STATUS "Found HSA runtime: ${hsa-runtime64_DIR}")

add_subdirectory(device)

add_executable(io_uring)
target_include_directories(io_uring PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(io_uring PRIVATE hsa-runtime64::hsa-runtime64)
target_compile_definitions(io_uring PRIVATE
  DEVICE_BINARY_PATH="$<TARGET_FILE:device>"
)
add_dependencies(io_uring device)

add_subdirectory(src)

install(TARGETS io_uring DESTINATION bin)
install(TARGETS device DESTINATION lib/io_uring)
