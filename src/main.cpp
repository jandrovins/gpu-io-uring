#include "io_uring/host.h"
#include "io_uring/hsa.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>

#ifndef DEVICE_BINARY_PATH
#error "DEVICE_BINARY_PATH must be defined"
#endif

int main() {
  using namespace io_uring::hsa;

  HSA_CHECK(hsa_init());

  hsa_agent_t cpu_agent;
  hsa_agent_t gpu_agent;
  HSA_CHECK(find_agent<HSA_DEVICE_TYPE_CPU>(&cpu_agent));
  HSA_CHECK(find_agent<HSA_DEVICE_TYPE_GPU>(&gpu_agent));

  hsa_amd_memory_pool_t fg_pool;
  HSA_CHECK(find_memory_pool<HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED>(
      cpu_agent, &fg_pool));

  hsa_amd_memory_pool_t kernargs_pool;
  HSA_CHECK(find_memory_pool<HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT>(
      cpu_agent, &kernargs_pool));

  auto executable = HSA_CHECK(
      load_executable(std::string_view(DEVICE_BINARY_PATH), gpu_agent));

  // Allocate the io_uring ring buffers from fine-grained HSA memory so
  // that the GPU can directly observe SQE/CQE state over PCIe/xGMI.
  constexpr uint32_t RING_ENTRIES = 4;
  constexpr size_t SQE_SIZE = RING_ENTRIES * sizeof(io_uring::sqe);
  constexpr size_t RING_SIZE =
      io_uring::KRING_SIZE + 2ull * RING_ENTRIES * sizeof(io_uring::cqe);

  void *sqe_buf = nullptr;
  HSA_CHECK(hsa_amd_memory_pool_allocate(fg_pool, SQE_SIZE, 0, &sqe_buf));
  HSA_CHECK(hsa_amd_agents_allow_access(1, &gpu_agent, nullptr, sqe_buf));

  void *ring_buf = nullptr;
  HSA_CHECK(hsa_amd_memory_pool_allocate(fg_pool, RING_SIZE, 0, &ring_buf));
  HSA_CHECK(hsa_amd_agents_allow_access(1, &gpu_agent, nullptr, ring_buf));

  const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  void *data_pool = nullptr;
  HSA_CHECK(hsa_amd_memory_pool_allocate(fg_pool, RING_ENTRIES * page_size, 0,
                                         &data_pool));
  HSA_CHECK(hsa_amd_agents_allow_access(1, &gpu_agent, nullptr, data_pool));

  auto hr = URING_CHECK(io_uring::init_ring(RING_ENTRIES, sqe_buf, ring_buf));
  URING_CHECK(
      io_uring::register_buffers(hr, data_pool, page_size, RING_ENTRIES));

  // Create an HSA interrupt signal for the GPU to wake the waker thread.
  auto [signal, doorbell] = HSA_CHECK(create_doorbell(gpu_agent));

  // The waker thread sleeps in kernel space via hsa_signal_wait until the
  // GPU fires s_sendmsg. On wake it pokes the SQPOLL thread if needed,
  // then resets the signal to zero for the next notification.
  std::atomic<bool> waker_done{false};
  std::thread waker([&] {
    while (!waker_done.load(std::memory_order_acquire)) {
      hsa_signal_wait_scacquire(signal, HSA_SIGNAL_CONDITION_NE, 0,
                                /*timeout_hint=*/0, HSA_WAIT_STATE_BLOCKED);
      if (waker_done.load(std::memory_order_acquire))
        return;
      io_uring::wakeup_ring(hr);
      hsa_signal_store_screlease(signal, 0);
    }
  });

  uint32_t queue_size;
  HSA_CHECK(hsa_agent_get_info(gpu_agent, HSA_AGENT_INFO_QUEUE_MAX_SIZE,
                               &queue_size));
  hsa_queue_t *queue = nullptr;
  HSA_CHECK(hsa_queue_create(gpu_agent, queue_size, HSA_QUEUE_TYPE_MULTI,
                             nullptr, nullptr, UINT32_MAX, UINT32_MAX, &queue));

  struct {
    io_uring::Ring ring;
    io_uring::Doorbell doorbell;
    int32_t fd;
  } args{
      .ring = hr.ring,
      .doorbell = doorbell,
      .fd = STDOUT_FILENO,
  };
  HSA_CHECK(launch_kernel(gpu_agent, executable, kernargs_pool, queue,
                          "kernel.kd", args));

  waker_done.store(true, std::memory_order_release);
  hsa_signal_store_screlease(signal, 1);
  waker.join();

  HSA_CHECK(hsa_signal_destroy(signal));
  URING_CHECK(io_uring::unregister_buffers(hr));
  URING_CHECK(io_uring::destroy_ring(hr));
  HSA_CHECK(hsa_amd_memory_pool_free(data_pool));
  HSA_CHECK(hsa_amd_memory_pool_free(ring_buf));
  HSA_CHECK(hsa_amd_memory_pool_free(sqe_buf));
  HSA_CHECK(hsa_queue_destroy(queue));
  HSA_CHECK(hsa_executable_destroy(executable));
  HSA_CHECK(hsa_shut_down());

  return EXIT_SUCCESS;
}
