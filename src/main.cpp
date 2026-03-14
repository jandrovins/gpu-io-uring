#include "io_uring/hsa.h"

#include <cstdio>
#include <cstdlib>

#ifndef DEVICE_BINARY_PATH
#error "DEVICE_BINARY_PATH must be defined"
#endif

int main(int argc, char *argv[]) {
  using namespace io_uring::hsa;

  HSA_CHECK(hsa_init());

  hsa_agent_t dev_agent;
  hsa_agent_t host_agent;
  HSA_CHECK(find_agent<HSA_DEVICE_TYPE_GPU>(&dev_agent));
  HSA_CHECK(find_agent<HSA_DEVICE_TYPE_CPU>(&host_agent));

  auto executable =
      load_executable(std::string_view(DEVICE_BINARY_PATH), dev_agent);
  HSA_CHECK(executable);

  hsa_amd_memory_pool_t kernargs_pool;
  HSA_CHECK(find_memory_pool<HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT>(
      host_agent, &kernargs_pool));

  uint32_t queue_size;
  HSA_CHECK(hsa_agent_get_info(dev_agent, HSA_AGENT_INFO_QUEUE_MAX_SIZE,
                               &queue_size));
  hsa_queue_t *queue = nullptr;
  HSA_CHECK(hsa_queue_create(dev_agent, queue_size, HSA_QUEUE_TYPE_MULTI,
                             nullptr, nullptr, UINT32_MAX, UINT32_MAX, &queue));

  struct {
  } args{};
  HSA_CHECK(launch_kernel(dev_agent, *executable, kernargs_pool, queue,
                          "kernel.kd", args));

  std::printf("kernel executed successfully\n");

  HSA_CHECK(hsa_queue_destroy(queue));
  HSA_CHECK(hsa_executable_destroy(*executable));
  HSA_CHECK(hsa_shut_down());

  return EXIT_SUCCESS;
}
