add_executable(device kernel.cpp)

set_target_properties(device PROPERTIES OUTPUT_NAME device)

target_compile_options(device PRIVATE
  --target=${IO_URING_GPU_TRIPLE}
  -mcpu=${IO_URING_GPU_ARCH}
  -nogpulib
)

target_link_options(device PRIVATE
  --target=${IO_URING_GPU_TRIPLE}
  -mcpu=${IO_URING_GPU_ARCH}
)
