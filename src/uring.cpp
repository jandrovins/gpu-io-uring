#include "io_uring/host.h"

#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>

namespace io_uring {

namespace {

long sys_io_uring_setup(uint32_t entries, params *p) {
  return syscall(__NR_io_uring_setup, entries, p);
}

long sys_io_uring_enter(int fd, uint32_t to_submit, uint32_t min_complete,
                        uint32_t flags) {
  return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags,
                 nullptr, 0);
}

} // namespace

std::expected<HostRing, int> init_ring(uint32_t entries, void *sqe_buf,
                                       void *ring_buf,
                                       uint32_t sq_thread_idle) {
  // Configure the io_uring interface to reside in user-provided memory buffers
  // without SQARRAY indirection. Requires Linux kernel version 6.6 to use. We
  // rely on polling mode to handle IO initiated by the device.
  params p{};
  p.flags = SETUP_NO_MMAP | SETUP_NO_SQARRAY | SETUP_SQPOLL;
  p.sq_thread_idle = sq_thread_idle;
  p.sq_off.user_addr = reinterpret_cast<uint64_t>(sqe_buf);
  p.cq_off.user_addr = reinterpret_cast<uint64_t>(ring_buf);

  long fd = sys_io_uring_setup(entries, &p);
  if (fd < 0)
    return std::unexpected(errno);

  auto *ring_base = static_cast<uint8_t *>(ring_buf);

  // Initialize the submission and completion queues with the generated offsets.
  Ring ring{};
  ring.sq_head = reinterpret_cast<uint32_t *>(ring_base + p.sq_off.head);
  ring.sq_tail = reinterpret_cast<uint32_t *>(ring_base + p.sq_off.tail);
  ring.sq_mask = reinterpret_cast<uint32_t *>(ring_base + p.sq_off.ring_mask);
  ring.sq_flags = reinterpret_cast<uint32_t *>(ring_base + p.sq_off.flags);
  ring.sq_array = nullptr;
  ring.sqes = static_cast<sqe *>(sqe_buf);

  ring.cq_head = reinterpret_cast<uint32_t *>(ring_base + p.cq_off.head);
  ring.cq_tail = reinterpret_cast<uint32_t *>(ring_base + p.cq_off.tail);
  ring.cq_mask = reinterpret_cast<uint32_t *>(ring_base + p.cq_off.ring_mask);
  ring.cqes = reinterpret_cast<cqe *>(ring_base + p.cq_off.cqes);

  // Bookkeeping for submissions, will need to be changed to MPSC / SPMC later.
  ring.sq_pending = 0;

  return HostRing{
      .ring = ring,
      .fd = static_cast<int>(fd),
  };
}

int destroy_ring(HostRing &hr) {
  if (close(hr.fd) < 0)
    return -errno;
  hr.fd = -1;
  return 0;
}

long wakeup_ring(HostRing &hr) {
  uint32_t flags = __atomic_load_n(hr.ring.sq_flags, __ATOMIC_RELAXED);
  if (flags & io_uring::SQ_NEED_WAKEUP)
    return sys_io_uring_enter(hr.fd, 0, 0, io_uring::ENTER_SQ_WAKEUP);
  return 0;
}

} // namespace io_uring
