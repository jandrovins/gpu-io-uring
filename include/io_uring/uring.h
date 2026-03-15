#ifndef IO_URING_URING_H
#define IO_URING_URING_H

#include "common.h"
#include <stdint.h>

namespace io_uring {

// Structures mirrored from <linux/io_uring.h>. Defined here so this header
// remains freestanding and usable from both CPU and GPU code.

struct sqe {
  uint8_t opcode;
  uint8_t flags;
  uint16_t ioprio;
  int32_t fd;
  union {
    uint64_t off;
    uint64_t addr2;
  };
  union {
    uint64_t addr;
    uint64_t splice_off_in;
  };
  uint32_t len;
  union {
    uint32_t rw_flags;
    uint32_t fsync_flags;
    uint32_t poll32_events;
    uint32_t msg_flags;
    uint32_t timeout_flags;
    uint32_t accept_flags;
    uint32_t cancel_flags;
    uint32_t open_flags;
    uint32_t statx_flags;
    uint32_t splice_flags;
    uint32_t rename_flags;
    uint32_t unlink_flags;
    uint32_t hardlink_flags;
    uint32_t xattr_flags;
    uint32_t msg_ring_flags;
    uint32_t uring_cmd_flags;
    uint32_t futex_flags;
    uint32_t nop_flags;
  };
  uint64_t user_data;
  union {
    uint16_t buf_index;
    uint16_t buf_group;
  } __attribute__((packed));
  uint16_t personality;
  union {
    int32_t splice_fd_in;
    uint32_t file_index;
    uint32_t optlen;
  };
  union {
    uint64_t addr3;
    uint64_t optval;
    uint8_t __pad2[16];
  };
};
static_assert(sizeof(sqe) == 64);

struct cqe {
  uint64_t user_data;
  int32_t res;
  uint32_t flags;
};
static_assert(sizeof(cqe) == 16);

struct sq_ring_offsets {
  uint32_t head;
  uint32_t tail;
  uint32_t ring_mask;
  uint32_t ring_entries;
  uint32_t flags;
  uint32_t dropped;
  uint32_t array;
  uint32_t resv1;
  uint64_t user_addr;
};

struct cq_ring_offsets {
  uint32_t head;
  uint32_t tail;
  uint32_t ring_mask;
  uint32_t ring_entries;
  uint32_t overflow;
  uint32_t cqes;
  uint32_t flags;
  uint32_t resv1;
  uint64_t user_addr;
};

struct params {
  uint32_t sq_entries;
  uint32_t cq_entries;
  uint32_t flags;
  uint32_t sq_thread_cpu;
  uint32_t sq_thread_idle;
  uint32_t features;
  uint32_t wq_fd;
  uint32_t resv[3];
  sq_ring_offsets sq_off;
  cq_ring_offsets cq_off;
};
static_assert(sizeof(params) == 120);

// Setup flags.
inline constexpr uint32_t SETUP_IOPOLL = 1u << 0;
inline constexpr uint32_t SETUP_SQPOLL = 1u << 1;
inline constexpr uint32_t SETUP_SQ_AFF = 1u << 2;
inline constexpr uint32_t SETUP_CQSIZE = 1u << 3;
inline constexpr uint32_t SETUP_CLAMP = 1u << 4;
inline constexpr uint32_t SETUP_ATTACH_WQ = 1u << 5;
inline constexpr uint32_t SETUP_NO_MMAP = 1u << 14;
inline constexpr uint32_t SETUP_NO_SQARRAY = 1u << 16;

// Feature flags reported by the kernel.
inline constexpr uint32_t FEAT_NODROP = 1u << 1;
inline constexpr uint32_t FEAT_SUBMIT_STABLE = 1u << 2;

// SQ ring flags (read from sq_flags).
inline constexpr uint32_t SQ_NEED_WAKEUP = 1u << 0;
inline constexpr uint32_t SQ_CQ_OVERFLOW = 1u << 1;

// io_uring_enter flags.
inline constexpr uint32_t ENTER_GETEVENTS = 1u << 0;
inline constexpr uint32_t ENTER_SQ_WAKEUP = 1u << 1;
inline constexpr uint32_t ENTER_SQ_WAIT = 1u << 2;

// Opcodes.
inline constexpr uint8_t OP_NOP = 0;
inline constexpr uint8_t OP_READV = 1;
inline constexpr uint8_t OP_WRITEV = 2;
inline constexpr uint8_t OP_FSYNC = 3;
inline constexpr uint8_t OP_READ_FIXED = 4;
inline constexpr uint8_t OP_WRITE_FIXED = 5;
inline constexpr uint8_t OP_READ = 22;
inline constexpr uint8_t OP_WRITE = 23;

// SQE flags.
inline constexpr uint8_t SQE_FIXED_FILE = 1u << 0;
inline constexpr uint8_t SQE_IO_DRAIN = 1u << 1;
inline constexpr uint8_t SQE_IO_LINK = 1u << 2;
inline constexpr uint8_t SQE_IO_HARDLINK = 1u << 3;
inline constexpr uint8_t SQE_ASYNC = 1u << 4;
inline constexpr uint8_t SQE_BUFFER_SELECT = 1u << 5;

// Size of the kernel's internal ring header (head, tail, mask, etc.).
inline constexpr uint64_t KRING_SIZE = 64;

// Register opcodes.
inline constexpr uint32_t REGISTER_BUFFERS = 0;
inline constexpr uint32_t UNREGISTER_BUFFERS = 1;
inline constexpr uint32_t REGISTER_FILES = 2;
inline constexpr uint32_t UNREGISTER_FILES = 3;

// A doorbell interface to futex wake a descheduled worker thread sleeping on an
// HSA signal. Shared with the HSA interface to allow waking the kernel thread.
struct Doorbell {
  uint64_t *value;
  uint64_t *mailbox;
  uint32_t event_id;
};

// Ring state holding pointers into the shared memory-mapped buffers. This
// struct is freestanding and usable from both CPU and GPU code.
//
// The current protocol is strictly Single-Producer / Single-Consumer.
struct Ring {
  uint32_t *sq_head;
  uint32_t *sq_tail;
  uint32_t *sq_mask;
  uint32_t *sq_flags;
  uint32_t *sq_array;
  sqe *sqes;

  uint32_t *cq_head;
  uint32_t *cq_tail;
  uint32_t *cq_mask;
  cqe *cqes;

  uint8_t *data_pool;
  uint32_t buf_stride;

  uint32_t sq_pending;
};

// Acquire the next available SQE slot, or nullptr if the queue is full.
// Not thread-safe — assumes a single producer (see SPSC note on Ring).
inline sqe *get_sqe(Ring &ring) {
  uint32_t tail = __scoped_atomic_load_n(ring.sq_tail, __ATOMIC_RELAXED,
                                         __MEMORY_SCOPE_SYSTEM) +
                  ring.sq_pending;
  uint32_t head = __scoped_atomic_load_n(ring.sq_head, __ATOMIC_ACQUIRE,
                                         __MEMORY_SCOPE_SYSTEM);
  if (tail - head > *ring.sq_mask)
    return nullptr;

  uint32_t index = tail & *ring.sq_mask;
  if (ring.sq_array)
    ring.sq_array[index] = index;
  ring.sq_pending++;
  return &ring.sqes[index];
}

// Make all pending SQEs visible to the consumer. Returns the number flushed.
// Not thread-safe — assumes a single producer (see SPSC note on Ring).
inline uint32_t sq_flush(Ring &ring) {
  uint32_t count = ring.sq_pending;
  if (count) {
    uint32_t tail = __scoped_atomic_load_n(ring.sq_tail, __ATOMIC_RELAXED,
                                           __MEMORY_SCOPE_SYSTEM);
    __scoped_atomic_store_n(ring.sq_tail, tail + count, __ATOMIC_RELEASE,
                            __MEMORY_SCOPE_SYSTEM);
    ring.sq_pending = 0;
  }
  return count;
}

// Check if a CQE is available without blocking. Returns nullptr if empty.
// Not thread-safe — assumes a single consumer (see SPSC note on Ring).
inline cqe *peek_cqe(Ring &ring) {
  uint32_t head = __scoped_atomic_load_n(ring.cq_head, __ATOMIC_RELAXED,
                                         __MEMORY_SCOPE_SYSTEM);
  uint32_t tail = __scoped_atomic_load_n(ring.cq_tail, __ATOMIC_ACQUIRE,
                                         __MEMORY_SCOPE_SYSTEM);
  if (head == tail)
    return nullptr;
  return &ring.cqes[head & *ring.cq_mask];
}

// Advance the CQ head after consuming CQEs.
// Not thread-safe — assumes a single consumer (see SPSC note on Ring).
inline void cq_advance(Ring &ring, uint32_t count = 1) {
  uint32_t head = __scoped_atomic_load_n(ring.cq_head, __ATOMIC_RELAXED,
                                         __MEMORY_SCOPE_SYSTEM);
  __scoped_atomic_store_n(ring.cq_head, head + count, __ATOMIC_RELEASE,
                          __MEMORY_SCOPE_SYSTEM);
}

// Check if the SQPOLL thread has gone idle and needs a wakeup.
inline bool needs_wakeup(Ring &ring) {
  return __scoped_atomic_load_n(ring.sq_flags, __ATOMIC_RELAXED,
                                __MEMORY_SCOPE_SYSTEM) &
         SQ_NEED_WAKEUP;
}

// Submit an operation. |fn| is invoked as fn(sqe*, args...) to populate the
// SQE. Tags the submission with a ticket derived from the SQE slot index and
// stored in user_data. Returns the ticket for use with complete().
template <typename Fn, typename... Args>
uint64_t submit(Ring &ring, Fn &&fn, Args &&...args) {
  sqe *s;
  while (!(s = get_sqe(ring)))
    ;
  uint64_t ticket = static_cast<uint64_t>(s - ring.sqes);
  io_uring::forward<Fn>(fn)(s, io_uring::forward<Args>(args)...);
  s->user_data = ticket;
  sq_flush(ring);
  return ticket;
}

// Wait for the completion of a specific ticket. Returns the CQE result.
inline int32_t complete(Ring &ring, uint64_t ticket) {
  cqe *c;
  do {
    c = peek_cqe(ring);
  } while (!c || c->user_data != ticket);
  int32_t res = c->res;
  cq_advance(ring);
  return res;
}

// Prep helpers — zero the SQE and fill fields for common operations.
inline void prep_nop(sqe *s) {
  *s = {};
  s->opcode = OP_NOP;
}

inline void prep_write(sqe *s, int fd, const void *buf, uint32_t len,
                       uint64_t offset = 0) {
  *s = {};
  s->opcode = OP_WRITE;
  s->fd = fd;
  s->addr = reinterpret_cast<uint64_t>(buf);
  s->len = len;
  s->off = offset;
}

inline void prep_read(sqe *s, int fd, void *buf, uint32_t len,
                      uint64_t offset = 0) {
  *s = {};
  s->opcode = OP_READ;
  s->fd = fd;
  s->addr = reinterpret_cast<uint64_t>(buf);
  s->len = len;
  s->off = offset;
}

inline uint8_t *get_buf(sqe *s, Ring &ring) {
  return ring.data_pool +
         static_cast<uint32_t>(s - ring.sqes) * ring.buf_stride;
}

inline void prep_write_fixed(sqe *s, int fd, const void *buf, uint32_t len,
                             uint16_t buf_idx, uint64_t offset = 0) {
  *s = {};
  s->opcode = OP_WRITE_FIXED;
  s->fd = fd;
  s->addr = reinterpret_cast<uint64_t>(buf);
  s->len = len;
  s->off = offset;
  s->buf_index = buf_idx;
}

inline void prep_write_fixed(sqe *s, Ring &ring, int fd, const void *msg,
                             uint32_t len, uint64_t offset = 0) {
  uint8_t *buf = io_uring::get_buf(s, ring);
  __builtin_memcpy(buf, msg, io_uring::min(len, ring.buf_stride));

  prep_write_fixed(s, fd, buf, len, static_cast<uint16_t>(s - ring.sqes),
                   offset);
}

inline void prep_read_fixed(sqe *s, int fd, void *buf, uint32_t len,
                            uint16_t buf_idx, uint64_t offset = 0) {
  *s = {};
  s->opcode = OP_READ_FIXED;
  s->fd = fd;
  s->addr = reinterpret_cast<uint64_t>(buf);
  s->len = len;
  s->off = offset;
  s->buf_index = buf_idx;
}

inline void prep_read_fixed(sqe *s, Ring &ring, int fd, void *msg, uint32_t len,
                            uint64_t offset = 0) {
  uint8_t *buf = io_uring::get_buf(s, ring);
  prep_read_fixed(s, fd, buf, len, static_cast<uint16_t>(s - ring.sqes),
                  offset);
  __builtin_memcpy(msg, buf, io_uring::min(len, ring.buf_stride));
}

} // namespace io_uring

#endif // IO_URING_URING_H
