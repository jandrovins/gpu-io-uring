#ifndef IO_URING_HOST_H
#define IO_URING_HOST_H

#include "io_uring/uring.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <utility>

namespace io_uring {

// Print the error description and terminate if the status indicates failure.
inline void check_impl(const char *file, int32_t line, int code) {
  if (code >= 0)
    return;
  std::fprintf(stderr, "%s:%d: error: %s\n", file, line, strerror(-code));
  std::exit(EXIT_FAILURE);
}

template <typename T>
inline T check_impl(const char *file, int32_t line,
                    std::expected<T, int> result) {
  if (!result)
    check_impl(file, line, -result.error());
  return std::move(*result);
}

#define URING_CHECK(x) ::io_uring::check_impl(__FILE__, __LINE__, (x))

// Host-side ring state wrapping the freestanding Ring with the io_uring
// file descriptor. Memory is provided by the caller (IORING_SETUP_NO_MMAP)
// and must be freed after destroy_ring().
struct HostRing {
  Ring ring;
  int fd;
};

// Set up an io_uring instance using application-provided memory for the
// ring buffers (IORING_SETUP_NO_MMAP | IORING_SETUP_NO_SQARRAY). |sqe_buf|
// holds the SQE array (entries * sizeof(sqe)). |ring_buf| holds the shared
// ring metadata and CQE array (KRING_SIZE + 2 * entries * sizeof(cqe)).
// The caller is responsible for freeing both buffers after destroy_ring().
[[nodiscard]] std::expected<HostRing, int>
init_ring(uint32_t entries, void *sqe_buf, void *ring_buf,
          uint32_t sq_thread_idle = 0);

// Close the io_uring file descriptor.
int destroy_ring(HostRing &hr);

// Wake up the kernel thread.
long wakeup_ring(HostRing &hr);

} // namespace io_uring

#endif // IO_URING_HOST_H
