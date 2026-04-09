#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace marketsim::eventbus {

/// Typical L1D cache line size; keeps producer/consumer atomics off the same line.
inline constexpr std::size_t kCacheLineSize = 64;

/**
 * Single-producer single-consumer bounded ring buffer.
 *
 * - Capacity must be a power of two >= 2; indexing uses (idx & mask).
 * - Monotonic uint64_t indices avoid ABA on slot reuse; no extra "full" slot is reserved.
 * - No heap allocation after construction: storage is inline `std::array<T, Capacity>`.
 *
 * Memory ordering (brief):
 * - Producer loads consumer index with acquire so it observes consumer releases and does not
 *   overwrite unread slots (full check is synchronized with the consumer).
 * - Producer publishes new data with release on the write index after the slot write is complete.
 * - Consumer loads producer index with acquire so it observes producer releases before reading a slot.
 * - Consumer publishes consumption with release on the read index after the read completes.
 *
 * Relaxed loads of "own" index are safe: only one thread mutates each counter.
 */
template <typename T, std::size_t Capacity>
class SpscRing {
  static_assert(Capacity >= 2, "Capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
  using value_type = T;

  SpscRing() = default;

  SpscRing(const SpscRing&) = delete;
  SpscRing& operator=(const SpscRing&) = delete;

  /// Non-blocking: returns false if the queue is full.
  template <typename U>
  [[nodiscard]] bool try_push(U&& value) {
    static_assert(std::is_assignable_v<T&, U&&>,
                  "T must be assignable from U (use emplace-style construction via assignment)");
    const std::uint64_t w = write_idx_.load(std::memory_order_relaxed);
    const std::uint64_t r = read_idx_.load(std::memory_order_acquire);
    if (w - r >= Capacity) {
      return false;
    }
    slots_[static_cast<std::size_t>(w & kMask)] = std::forward<U>(value);
    write_idx_.store(w + 1, std::memory_order_release);
    return true;
  }

  /// Non-blocking: returns false if the queue is empty.
  [[nodiscard]] bool try_pop(T& out) {
    const std::uint64_t w = write_idx_.load(std::memory_order_acquire);
    const std::uint64_t r = read_idx_.load(std::memory_order_relaxed);
    if (w == r) {
      return false;
    }
    out = std::move(slots_[static_cast<std::size_t>(r & kMask)]);
    read_idx_.store(r + 1, std::memory_order_release);
    return true;
  }

  /// Approximate number of elements (may race; fine for telemetry).
  [[nodiscard]] std::uint64_t size_approx() const noexcept {
    const std::uint64_t w = write_idx_.load(std::memory_order_relaxed);
    const std::uint64_t r = read_idx_.load(std::memory_order_relaxed);
    return w - r;
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
  static constexpr std::uint64_t kMask = static_cast<std::uint64_t>(Capacity - 1);

  alignas(kCacheLineSize) std::atomic<std::uint64_t> write_idx_{0};
  alignas(kCacheLineSize) std::atomic<std::uint64_t> read_idx_{0};
  alignas(kCacheLineSize) std::array<T, Capacity> slots_{};
};

}
