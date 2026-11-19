// K Engine — Engine/Identity/Handle.h
//
// Runtime generational handles (ADR-0003).
//
// A Handle is a live, in-process reference: {slot index, generation}. It is
// NOT persistent identity — never serialize it, never use it across
// processes. Persistent identity is Uuid (Uuid.h).
//
// Guarantees, enforced by SlotMap and covered by Tests/Unit/test_handle.cpp:
//   * A destroyed object's handle becomes invalid.
//   * Reusing a slot increments its generation.
//   * A stale handle never resolves to a different/new object.
//   * Lookup of a bad handle yields a typed status / nullptr, never UB.
//   * A slot whose generation counter is exhausted is retired, never reused,
//     so the counter cannot wrap around and resurrect an old handle.
//   * A Handle holds no pointer; resolving one always goes through its owner.
//
// Requires T to be default-constructible and move-assignable.
//
// Threading: SlotMap has no internal synchronization. It follows the
// thread-affinity of its owner (for Scene: the authoring/editor thread).
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace k {

// A handle is typed by a Tag so that handles of different domains are
// different, non-convertible C++ types (ADR-0003): an authoring ObjectHandle can
// never be passed where a runtime handle is expected, nor used with a
// SlotMap of another domain — that is a compile error, not a silent alias.
template <class Tag>
struct BasicHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;  // 0 is never a valid generation: BasicHandle{} is the null handle

    constexpr bool is_null() const { return generation == 0; }
    friend constexpr bool operator==(const BasicHandle&, const BasicHandle&) = default;
};

// Untyped handle: for generic containers and tests only. Domain code uses a
// dedicated typed handle.
struct UntypedTag;
using Handle = BasicHandle<UntypedTag>;
inline constexpr Handle kNullHandle{};

// Authoring-layer object (Engine/Scene).
struct ObjectTag;
using ObjectHandle = BasicHandle<ObjectTag>;
inline constexpr ObjectHandle kNullObjectHandle{};

enum class HandleStatus : std::uint8_t {
    Valid,
    Null,         // default-constructed / generation 0
    OutOfRange,   // index does not refer to any slot this map ever created
    Stale,        // slot exists but the object this handle named was destroyed
};

constexpr const char* to_string(HandleStatus s) {
    switch (s) {
        case HandleStatus::Valid: return "valid";
        case HandleStatus::Null: return "null";
        case HandleStatus::OutOfRange: return "out_of_range";
        case HandleStatus::Stale: return "stale";
    }
    return "unknown";
}

template <class T, class H = Handle>
class SlotMap {
public:
    using handle_type = H;

    // `max_generation` is a test hook so generation exhaustion can be
    // exercised without 2^32 iterations. Production code uses the default.
    explicit SlotMap(std::uint32_t max_generation = std::numeric_limits<std::uint32_t>::max())
        : max_generation_(max_generation) {}

    H insert(T value) {
        std::uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
            slots_.back().generation = 1;
        }
        Slot& slot = slots_[index];
        slot.value = std::move(value);
        slot.alive = true;
        ++size_;
        return H{index, slot.generation};
    }

    HandleStatus status(H h) const {
        if (h.is_null()) return HandleStatus::Null;
        if (h.index >= slots_.size()) return HandleStatus::OutOfRange;
        const Slot& slot = slots_[h.index];
        if (!slot.alive || slot.generation != h.generation) return HandleStatus::Stale;
        return HandleStatus::Valid;
    }

    bool contains(H h) const { return status(h) == HandleStatus::Valid; }

    // nullptr unless the handle is Valid. The pointer is only good until the
    // next insert() (storage may reallocate); do not store it.
    T* get(H h) { return contains(h) ? &slots_[h.index].value : nullptr; }
    const T* get(H h) const { return contains(h) ? &slots_[h.index].value : nullptr; }

    // Returns false (and changes nothing) for a null/stale/out-of-range handle.
    bool erase(H h) {
        if (!contains(h)) return false;
        Slot& slot = slots_[h.index];
        slot.value = T{};  // release resources now rather than at slot reuse
        slot.alive = false;
        --size_;
        if (slot.generation >= max_generation_) {
            ++retired_;  // never reused: generation cannot advance without wrapping
        } else {
            ++slot.generation;
            free_.push_back(h.index);
        }
        return true;
    }

    std::size_t size() const { return size_; }
    std::size_t retired_slots() const { return retired_; }

    // Visits live objects in ascending slot order (deterministic for a given
    // insert/erase history). Do not insert/erase from inside `fn`.
    template <class Fn>
    void for_each(Fn&& fn) const {
        for (std::uint32_t i = 0; i < slots_.size(); ++i) {
            const Slot& s = slots_[i];
            if (s.alive) fn(H{i, s.generation}, s.value);
        }
    }

private:
    struct Slot {
        T value{};
        std::uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::size_t size_ = 0;
    std::size_t retired_ = 0;
    std::uint32_t max_generation_;
};

}  // namespace k
