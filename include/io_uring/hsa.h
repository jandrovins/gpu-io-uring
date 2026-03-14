#ifndef IO_URING_HSA_H
#define IO_URING_HSA_H

#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "io_uring/uring.h"

namespace io_uring {
namespace hsa {

// Print the error description and terminate if the status indicates failure.
inline void check_impl(const char *file, int32_t line, hsa_status_t code) {
  if (code == HSA_STATUS_SUCCESS || code == HSA_STATUS_INFO_BREAK)
    return;

  const char *desc;
  if (hsa_status_string(code, &desc) != HSA_STATUS_SUCCESS)
    desc = "Unknown error";
  std::fprintf(stderr, "%s:%d: error: %s\n", file, line, desc);
  std::exit(EXIT_FAILURE);
}
template <typename T>
inline T check_impl(const char *file, int32_t line,
                    std::expected<T, hsa_status_t> code) {
  if (!code)
    check_impl(file, line, code.error());
  return code.value();
}

#define HSA_CHECK(x) ::io_uring::hsa::check_impl(__FILE__, __LINE__, (x))

// Generic adapter for HSA callbacks that iterate over a collection. Wraps a
// callable so it can be passed through the C void* interface.
template <typename elem_ty, typename func_ty, typename callback_ty>
hsa_status_t iterate(func_ty func, callback_ty cb) {
  auto wrapper = [](elem_ty elem, void *data) -> hsa_status_t {
    return (*static_cast<callback_ty *>(data))(elem);
  };
  return func(wrapper, static_cast<void *>(&cb));
}

template <typename elem_ty, typename func_ty, typename arg_ty,
          typename callback_ty>
hsa_status_t iterate(func_ty func, arg_ty arg, callback_ty cb) {
  auto wrapper = [](elem_ty elem, void *data) -> hsa_status_t {
    return (*static_cast<callback_ty *>(data))(elem);
  };
  return func(arg, wrapper, static_cast<void *>(&cb));
}

// Find the first agent matching the requested device type.
template <hsa_device_type_t device_type>
hsa_status_t find_agent(hsa_agent_t *output) {
  auto cb = [&](hsa_agent_t agent) -> hsa_status_t {
    hsa_device_type_t type;
    hsa_status_t status =
        hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &type);
    if (status != HSA_STATUS_SUCCESS)
      return status;

    if (type != device_type)
      return HSA_STATUS_SUCCESS;

    if (type == HSA_DEVICE_TYPE_GPU) {
      hsa_agent_feature_t features;
      status = hsa_agent_get_info(agent, HSA_AGENT_INFO_FEATURE, &features);
      if (status != HSA_STATUS_SUCCESS)
        return status;
      if (!(features & HSA_AGENT_FEATURE_KERNEL_DISPATCH))
        return HSA_STATUS_SUCCESS;
    }

    *output = agent;
    return HSA_STATUS_INFO_BREAK;
  };
  return iterate<hsa_agent_t>(hsa_iterate_agents, cb);
}

// Retrieve a global memory pool with the specified flag from an agent.
template <hsa_amd_memory_pool_global_flag_t flag>
hsa_status_t find_memory_pool(hsa_agent_t agent,
                              hsa_amd_memory_pool_t *output) {
  auto cb = [&](hsa_amd_memory_pool_t pool) -> hsa_status_t {
    hsa_amd_segment_t segment;
    if (auto err = hsa_amd_memory_pool_get_info(
            pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &segment))
      return err;

    if (segment != HSA_AMD_SEGMENT_GLOBAL)
      return HSA_STATUS_SUCCESS;

    uint32_t flags;
    if (auto err = hsa_amd_memory_pool_get_info(
            pool, HSA_AMD_MEMORY_POOL_INFO_GLOBAL_FLAGS, &flags))
      return err;

    if (flags & flag)
      *output = pool;

    return HSA_STATUS_SUCCESS;
  };
  return iterate<hsa_amd_memory_pool_t>(hsa_amd_agent_iterate_memory_pools,
                                        agent, cb);
}

// Create an HSA interrupt signal and extract the doorbell fields needed
// for GPU-to-host notification.
struct SignalDoorbell {
  hsa_signal_t signal;
  Doorbell doorbell;
};

[[nodiscard]] inline std::expected<SignalDoorbell, hsa_status_t>
create_doorbell(hsa_agent_t gpu_agent) {
  hsa_signal_t signal;
  if (hsa_status_t err = hsa_signal_create(0, 0, nullptr, &signal))
    return std::unexpected(err);

  // Peeking behind the curtains to grab the internal signal state so we do not
  // need to define a full HSA signal on the device.
  struct amd_signal_t {
    int64_t kind;
    int64_t value;
    uint64_t event_mailbox_ptr;
    uint32_t event_id;
  };
  auto *s = reinterpret_cast<amd_signal_t *>(signal.handle);

  char name[64] = {};
  if (hsa_status_t err =
          hsa_agent_get_info(gpu_agent, HSA_AGENT_INFO_NAME, name))
    return std::unexpected(err);
  bool is_gfx10 = (name[3] == '1' && name[4] == '0');
  uint32_t mask = is_gfx10 ? 0x7fffffu : 0xffffffu;

  return SignalDoorbell{
      .signal = signal,
      .doorbell =
          {
              .value = reinterpret_cast<uint64_t *>(&s->value),
              .mailbox = reinterpret_cast<uint64_t *>(s->event_mailbox_ptr),
              .event_id = s->event_id & mask,
          },
  };
}

// Load a code object from a memory buffer and produce a validated, frozen
// executable ready for kernel dispatch.
[[nodiscard]] inline std::expected<hsa_executable_t, hsa_status_t>
load_executable(std::span<const char> image, hsa_agent_t agent) {
  hsa_code_object_reader_t reader;
  if (hsa_status_t err = hsa_code_object_reader_create_from_memory(
          image.data(), image.size(), &reader))
    return std::unexpected(err);

  hsa_executable_t executable;
  if (hsa_status_t err = hsa_executable_create_alt(
          HSA_PROFILE_FULL, HSA_DEFAULT_FLOAT_ROUNDING_MODE_ZERO, "",
          &executable))
    return std::unexpected(err);

  hsa_loaded_code_object_t object;
  if (hsa_status_t err = hsa_executable_load_agent_code_object(
          executable, agent, reader, "", &object))
    return std::unexpected(err);

  if (hsa_status_t err = hsa_executable_freeze(executable, ""))
    return std::unexpected(err);

  uint32_t result;
  if (hsa_status_t err = hsa_executable_validate(executable, &result))
    return std::unexpected(err);
  if (result)
    return std::unexpected(HSA_STATUS_ERROR);

  if (hsa_status_t err = hsa_code_object_reader_destroy(reader))
    return std::unexpected(err);

  return executable;
}

// Load a code object from a file path and produce a validated, frozen
// executable ready for kernel dispatch.
[[nodiscard]] inline std::expected<hsa_executable_t, hsa_status_t>
load_executable(std::string_view path, hsa_agent_t agent) {
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
  if (!file) {
    std::fprintf(stderr, "error: failed to open '%.*s'\n",
                 static_cast<int>(path.size()), path.data());
    return std::unexpected(HSA_STATUS_ERROR);
  }

  auto size = file.tellg();
  file.seekg(0);
  std::vector<char> image(static_cast<size_t>(size));
  file.read(image.data(), size);

  return load_executable(std::span<const char>(image), agent);
}

// Launch a kernel on the device with the given arguments. The kernel is looked
// up by name in the executable and dispatched through the provided queue.
template <typename args_ty>
hsa_status_t launch_kernel(hsa_agent_t dev_agent, hsa_executable_t executable,
                           hsa_amd_memory_pool_t kernargs_pool,
                           hsa_queue_t *queue, const char *kernel_name,
                           const args_ty &args, uint32_t num_threads_x = 1,
                           uint32_t num_blocks_x = 1) {
  hsa_executable_symbol_t symbol;
  if (hsa_status_t err = hsa_executable_get_symbol_by_name(
          executable, kernel_name, &dev_agent, &symbol))
    return err;

  uint64_t kernel_object;
  uint32_t kernarg_size;
  uint32_t group_size;
  uint32_t private_size;
  std::pair<hsa_executable_symbol_info_t, void *> infos[] = {
      {HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernel_object},
      {HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &kernarg_size},
      {HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE, &group_size},
      {HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &private_size},
  };

  for (auto &[info, value] : infos)
    if (hsa_status_t err = hsa_executable_symbol_get_info(symbol, info, value))
      return err;

  void *kernargs;
  if (hsa_status_t err = hsa_amd_memory_pool_allocate(
          kernargs_pool, kernarg_size, /*flags=*/0, &kernargs))
    return err;
  hsa_amd_agents_allow_access(1, &dev_agent, nullptr, kernargs);

  std::memset(kernargs, 0, kernarg_size);
  constexpr size_t ARGS_SIZE = std::is_empty_v<args_ty> ? 0 : sizeof(args_ty);
  if constexpr (ARGS_SIZE > 0)
    std::memcpy(kernargs, &args, ARGS_SIZE);

  // The implicit arguments appended to every AMDGPU kernel (COV5+).
  struct ImplicitArgs {
    uint32_t grid_size_x;
    uint32_t grid_size_y;
    uint32_t grid_size_z;
    uint16_t workgroup_size_x;
    uint16_t workgroup_size_y;
    uint16_t workgroup_size_z;
    uint8_t reserved0[46];
    uint16_t grid_dims;
    uint8_t reserved1[190];
  };

  // Fill in the implicit arguments that follow the explicit ones.
  auto *implicit = reinterpret_cast<ImplicitArgs *>(
      reinterpret_cast<uint8_t *>(kernargs) + ARGS_SIZE);
  implicit->grid_dims = 1;
  implicit->grid_size_x = num_blocks_x;
  implicit->grid_size_y = 1;
  implicit->grid_size_z = 1;
  implicit->workgroup_size_x = static_cast<uint16_t>(num_threads_x);
  implicit->workgroup_size_y = 1;
  implicit->workgroup_size_z = 1;

  // Acquire a slot in the queue.
  uint64_t packet_id = hsa_queue_add_write_index_relaxed(queue, 1);
  while (packet_id - hsa_queue_load_read_index_scacquire(queue) >= queue->size)
    ;

  const uint32_t mask = queue->size - 1;
  auto *packet =
      static_cast<hsa_kernel_dispatch_packet_t *>(queue->base_address) +
      (packet_id & mask);

  uint16_t setup = 1 << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
  packet->workgroup_size_x = static_cast<uint16_t>(num_threads_x);
  packet->workgroup_size_y = 1;
  packet->workgroup_size_z = 1;
  packet->reserved0 = 0;
  packet->grid_size_x = num_blocks_x * num_threads_x;
  packet->grid_size_y = 1;
  packet->grid_size_z = 1;
  packet->private_segment_size = private_size;
  packet->group_segment_size = group_size;
  packet->kernel_object = kernel_object;
  packet->kernarg_address = kernargs;
  packet->reserved2 = 0;

  if (hsa_status_t err =
          hsa_signal_create(1, 0, nullptr, &packet->completion_signal))
    return err;

  // Commit the packet header and ring the doorbell to begin execution.
  uint16_t header =
      (1u << HSA_PACKET_HEADER_BARRIER) |
      (HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE) |
      (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE) |
      (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE);
  uint32_t header_word = header | (static_cast<uint32_t>(setup) << 16u);
  __atomic_store_n(reinterpret_cast<uint32_t *>(&packet->header), header_word,
                   __ATOMIC_RELEASE);
  hsa_signal_store_relaxed(queue->doorbell_signal,
                           static_cast<hsa_signal_value_t>(packet_id));

  while (hsa_signal_wait_scacquire(packet->completion_signal,
                                   HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX,
                                   HSA_WAIT_STATE_BLOCKED) != 0)
    ;

  if (hsa_status_t err = hsa_amd_memory_pool_free(kernargs))
    return err;
  if (hsa_status_t err = hsa_signal_destroy(packet->completion_signal))
    return err;

  return HSA_STATUS_SUCCESS;
}

} // namespace hsa
} // namespace io_uring

#endif // IO_URING_HSA_H
