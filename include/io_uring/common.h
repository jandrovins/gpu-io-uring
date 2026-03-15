#ifndef IO_URING_COMMON_H
#define IO_URING_COMMON_H

namespace io_uring {

template <typename T> struct type_identity {
  using type = T;
};

template <typename T, T v> struct type_constant {
  static inline constexpr T value = v;
};

/// Freestanding type trait helpers.
template <typename T> struct remove_cv : type_identity<T> {};
template <typename T> struct remove_cv<const T> : type_identity<T> {};
template <typename T> using remove_cv_t = typename remove_cv<T>::type;

template <typename T> struct remove_pointer : type_identity<T> {};
template <typename T> struct remove_pointer<T *> : type_identity<T> {};
template <typename T> using remove_pointer_t = typename remove_pointer<T>::type;

template <typename T> struct remove_const : type_identity<T> {};
template <typename T> struct remove_const<const T> : type_identity<T> {};
template <typename T> using remove_const_t = typename remove_const<T>::type;

template <typename T> struct remove_reference : type_identity<T> {};
template <typename T> struct remove_reference<T &> : type_identity<T> {};
template <typename T> struct remove_reference<T &&> : type_identity<T> {};
template <typename T>
using remove_reference_t = typename remove_reference<T>::type;

template <typename T>
constexpr T &&forward(typename remove_reference<T>::type &value) {
  return static_cast<T &&>(value);
}
template <typename T>
constexpr T &&forward(typename remove_reference<T>::type &&value) {
  return static_cast<T &&>(value);
}

template <typename T, typename U> constexpr T min(const T &a, const U &b) {
  return (a < b) ? a : b;
}

template <typename T, typename U> constexpr T max(const T &a, const U &b) {
  return (a < b) ? b : a;
}
} // namespace io_uring
#endif // IO_URING_COMMON_H
