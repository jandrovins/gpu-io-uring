#include "io_uring/uring.h"

#include <gpuintrin.h>

constexpr uint32_t MSG_INTERRUPT = 1;

static const char msg[] = "hello io_uring from the GPU!\n";

extern "C" __gpu_kernel void kernel(io_uring::Ring ring,
                                    io_uring::Doorbell doorbell, int32_t fd) {
  auto *sqe = io_uring::get_sqe(ring);
  io_uring::prep_write(sqe, fd, msg, sizeof(msg) - 1);
  io_uring::sq_flush(ring);

  uint32_t flags = __scoped_atomic_load_n(ring.sq_flags, __ATOMIC_RELAXED,
                                          __MEMORY_SCOPE_SYSTEM);
  if (flags & io_uring::SQ_NEED_WAKEUP) {
    __scoped_atomic_fetch_add(doorbell.value, 1UL, __ATOMIC_RELAXED,
                              __MEMORY_SCOPE_SYSTEM);
    __scoped_atomic_store_n(doorbell.mailbox,
                            static_cast<uint64_t>(doorbell.event_id),
                            __ATOMIC_RELEASE, __MEMORY_SCOPE_SYSTEM);
    uint32_t event_id =
        __gpu_read_first_lane_u32(__gpu_lane_mask(), doorbell.event_id);
    __builtin_amdgcn_s_sendmsg(MSG_INTERRUPT, event_id);
  }

  while (!io_uring::peek_cqe(ring))
    ;
  io_uring::cq_advance(ring);
}
