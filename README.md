# GPU io_uring — GPU-Initiated I/O via Linux's io_uring

This project lets **AMD GPU kernels perform I/O directly** by submitting
requests to Linux's `io_uring` interface. This is in contrast to existing
solutions like LLVM libc's [RPC interface](https://libc.llvm.org/gpu/rpc.html).

The ring buffers live in fine-grained HSA memory visible to both the CPU and GPU
over PCIe/xGMI. A GPU kernel fills submission queue entries (SQEs), flushes
them, and fires an interrupt to wake the host-side SQPOLL thread if it has gone
idle. Completions arrive in the same shared memory and are consumed on-device.

## How It Works

1. **Shared rings in HSA fine-grained memory.** The SQE and CQE arrays are
   allocated through `hsa_amd_memory_pool_allocate` with fine-grained
   coherency. This allows the kernel and device to cooperate.

2. **`SETUP_NO_MMAP | SETUP_NO_SQARRAY | SETUP_SQPOLL`.** The kernel's
   `io_uring` instance uses caller-provided buffers in HSA fine-grained memory
   and a polling thread for submission.

3. **GPU doorbell via HSA interrupt signal.** When the SQPOLL thread sleeps,
   the GPU fires `s_sendmsg(MSG_INTERRUPT)` through the HSA signal mailbox. A
   host waker thread catches the interrupt and pokes the kernel thread awake.

4. **Freestanding ring protocol.** The `io_uring::Ring` struct and its helpers
   (`get_sqe`, `sq_flush`, `peek_cqe`, `cq_advance`) are written in
   freestanding C++ with scoped atomics, usable identically on the CPU and GPU.

## Requirements

| Dependency         | Minimum Version |
|--------------------|-----------------|
| Clang              | 22.0            |
| CMake              | 3.28            |
| Linux kernel       | 6.6             |
| ROCm (HSA runtime) | 1.12            |
| AMD GPU            | GFX9+ with Large BAR (Resizable BAR / Smart Access Memory) |

Large BAR is required so the GPU can directly access host-side ring memory. Not
strictly necessary but used for demonstration purposes.

## Building

```bash
cmake -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
cmake --build build
```

## Running

```bash
./build/io_uring
# → hello io_uring from the GPU!
```
