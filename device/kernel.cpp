#include "io_uring/uring.h"

#include <gpuintrin.h>

constexpr uint32_t MSG_INTERRUPT = 1;

void wake_sq(io_uring::Doorbell &doorbell) {
  __scoped_atomic_fetch_add(doorbell.value, 1UL, __ATOMIC_RELAXED,
                            __MEMORY_SCOPE_SYSTEM);
  __scoped_atomic_store_n(doorbell.mailbox,
                          static_cast<uint64_t>(doorbell.event_id),
                          __ATOMIC_RELEASE, __MEMORY_SCOPE_SYSTEM);
  uint32_t event_id =
      __gpu_read_first_lane_u32(__gpu_lane_mask(), doorbell.event_id);
  __builtin_amdgcn_s_sendmsg(MSG_INTERRUPT, event_id);
}

extern "C" {
__gpu_kernel void kernel(io_uring::Ring ring, io_uring::Doorbell doorbell,
                         int32_t fd) {
  char msg[] = "Hello io_uring from the GPU!\n";
  auto ticket = io_uring::submit(ring, [&](io_uring::sqe *s) {
    io_uring::prep_write_fixed(s, ring, fd, msg, sizeof(msg) - 1);
  });

  if (io_uring::needs_wakeup(ring))
    wake_sq(doorbell);

  io_uring::complete(ring, ticket);
}
}
