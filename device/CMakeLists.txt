add_executable(kernel kernel.cpp)

target_include_directories(kernel PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_options(kernel PRIVATE
  ${common_compile_options}
  --target=${IO_URING_GPU_TRIPLE}
  -mcpu=${IO_URING_GPU_ARCH}
)

target_link_options(kernel PRIVATE
  --target=${IO_URING_GPU_TRIPLE}
  -mcpu=${IO_URING_GPU_ARCH}
)
