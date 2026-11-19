#include <type_traits>
#include <cstdint>
#include <memory>
#include <vector>

#include "Engine/Identity/Handle.h"
#include "Tests/Unit/TestFramework.h"

using k::Handle;
using k::HandleStatus;
using k::kNullHandle;
using k::SlotMap;

KTEST(handle_null_is_never_valid) {
    SlotMap<int> m;
    KCHECK(kNullHandle.is_null());
    KCHECK(Handle{}.is_null());
    KCHECK(m.status(kNullHandle) == HandleStatus::Null);
    KCHECK(m.get(kNullHandle) == nullptr);
    KCHECK(!m.erase(kNullHandle));
    // Even after the map has live objects, the null handle stays invalid.
    m.insert(1);
    KCHECK(m.get(kNullHandle) == nullptr);
}

KTEST(handle_insert_get_erase) {
    SlotMap<int> m;
    const Handle a = m.insert(10);
    const Handle b = m.insert(20);
    KCHECK(!a.is_null());
    KCHECK(a != b);
    KCHECK_EQ(m.size(), static_cast<std::size_t>(2));
    KCHECK(m.get(a) != nullptr && *m.get(a) == 10);
    KCHECK(m.get(b) != nullptr && *m.get(b) == 20);

    KCHECK(m.erase(a));
    KCHECK_EQ(m.size(), static_cast<std::size_t>(1));
    KCHECK(m.get(a) == nullptr);
    KCHECK(m.status(a) == HandleStatus::Stale);
    KCHECK(*m.get(b) == 20);  // unaffected
    KCHECK(!m.erase(a));      // double erase is a safe no-op
    KCHECK_EQ(m.size(), static_cast<std::size_t>(1));
}

KTEST(handle_slot_reuse_increments_generation_and_old_handle_stays_stale) {
    SlotMap<int> m;
    const Handle first = m.insert(111);
    KCHECK(m.erase(first));
    const Handle second = m.insert(222);  // LIFO free list: reuses the same slot

    KCHECK_EQ(second.index, first.index);
    KCHECK(second.generation > first.generation);
    KCHECK(m.get(first) == nullptr);       // stale handle must NOT see the new object
    KCHECK(m.status(first) == HandleStatus::Stale);
    KCHECK(m.get(second) != nullptr && *m.get(second) == 222);
    KCHECK(!m.erase(first));               // and must not be able to destroy it either
    KCHECK(m.get(second) != nullptr);
}

KTEST(handle_out_of_range_index_is_reported_not_crashed) {
    SlotMap<int> m;
    m.insert(1);
    const Handle forged{9999, 1};
    KCHECK(m.status(forged) == HandleStatus::OutOfRange);
    KCHECK(m.get(forged) == nullptr);
    KCHECK(!m.erase(forged));
}

KTEST(handle_forged_generation_does_not_resolve) {
    SlotMap<int> m;
    const Handle h = m.insert(5);
    KCHECK(m.get(Handle{h.index, h.generation + 1}) == nullptr);
    KCHECK(m.get(Handle{h.index, h.generation - 1}) == nullptr);  // generation 0 == null
    KCHECK(m.get(h) != nullptr);
}

KTEST(handle_generation_exhaustion_retires_slot_instead_of_wrapping) {
    SlotMap<int> m(/*max_generation=*/3);
    std::vector<Handle> old;
    Handle h = m.insert(0);
    KCHECK_EQ(h.generation, 1u);
    for (int i = 0; i < 2; ++i) {  // generation 1 -> 2 -> 3, slot still reusable
        old.push_back(h);
        KCHECK(m.erase(h));
        h = m.insert(i);
        KCHECK_EQ(h.index, 0u);
    }
    KCHECK_EQ(h.generation, 3u);
    KCHECK_EQ(m.retired_slots(), static_cast<std::size_t>(0));

    old.push_back(h);
    KCHECK(m.erase(h));  // generation 3 == max: slot is retired, not recycled
    KCHECK_EQ(m.retired_slots(), static_cast<std::size_t>(1));

    const Handle next = m.insert(99);
    KCHECK(next.index != 0u);  // a fresh slot, never the retired one
    for (const Handle& o : old) KCHECK(m.get(o) == nullptr);
    KCHECK(m.get(next) != nullptr);
}

KTEST(handle_erase_releases_payload_immediately) {
    SlotMap<std::shared_ptr<int>> m;
    auto payload = std::make_shared<int>(7);
    const Handle h = m.insert(payload);
    KCHECK_EQ(payload.use_count(), 2l);
    KCHECK(m.erase(h));
    KCHECK_EQ(payload.use_count(), 1l);  // not held until the slot is reused
}

KTEST(handle_for_each_visits_live_objects_in_slot_order) {
    SlotMap<int> m;
    const Handle a = m.insert(1), b = m.insert(2), c = m.insert(3);
    m.erase(b);
    std::vector<int> seen;
    m.for_each([&](Handle h, const int& v) {
        KCHECK(m.contains(h));
        seen.push_back(v);
    });
    KCHECK_EQ(seen.size(), static_cast<std::size_t>(2));
    KCHECK_EQ(seen[0], 1);
    KCHECK_EQ(seen[1], 3);
    (void)a; (void)c;
}

// Randomized model check: the SlotMap must agree with a trivially-correct
// reference at every step, and handles that were ever destroyed must never
// become valid again, no matter how many slots are recycled.
KTEST(handle_randomized_model_check_never_resurrects_stale_handles) {
    struct Entry { Handle h; int value; bool alive; };
    std::vector<Entry> model;
    SlotMap<int> m;
    std::uint64_t state = 0x9E3779B97F4A7C15ull;  // fixed seed: reproducible failures
    auto rnd = [&state] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 33);
    };

    std::size_t live = 0;
    for (int step = 0; step < 20000; ++step) {
        const std::uint32_t op = rnd() % 3;
        if (op == 0 || model.empty()) {
            const int v = static_cast<int>(rnd());
            const Handle h = m.insert(v);
            model.push_back({h, v, true});
            ++live;
        } else if (op == 1) {
            Entry& e = model[rnd() % model.size()];
            const bool erased = m.erase(e.h);
            KCHECK_EQ(erased, e.alive);
            if (e.alive) --live;
            e.alive = false;
        } else {
            const Entry& e = model[rnd() % model.size()];
            const int* p = m.get(e.h);
            if (e.alive) {
                KCHECK(p != nullptr);
                if (p) KCHECK_EQ(*p, e.value);
            } else {
                KCHECK(p == nullptr);
            }
        }
        KCHECK_EQ(m.size(), live);
    }
    // Final sweep over every handle ever issued.
    for (const Entry& e : model) {
        KCHECK_EQ(m.contains(e.h), e.alive);
    }
}

// ---- Domain-typed handles (ADR-0003, M05-T02b) -----------------------------
// These are compile-time guarantees: if they regress, the build fails.

namespace {
struct OtherTag;
using OtherHandle = k::BasicHandle<OtherTag>;

template <class Map, class H>
concept CanLookUp = requires(Map m, H h) { m.get(h); };
template <class Map, class H>
concept CanErase = requires(Map m, H h) { m.erase(h); };
}  // namespace

static_assert(!std::is_convertible_v<k::ObjectHandle, OtherHandle>, "handles of different domains must not convert");
static_assert(!std::is_convertible_v<OtherHandle, k::ObjectHandle>);
static_assert(!std::is_convertible_v<k::ObjectHandle, k::Handle>, "typed handle must not decay to the untyped one");
static_assert(!std::is_convertible_v<k::Handle, k::ObjectHandle>);
static_assert(!std::is_constructible_v<k::ObjectHandle, OtherHandle>);
static_assert(!std::is_constructible_v<k::ObjectHandle, k::Handle>);
static_assert(!std::is_same_v<k::ObjectHandle, k::Handle>);
static_assert(sizeof(k::ObjectHandle) == 8 && std::is_trivially_copyable_v<k::ObjectHandle>);

// A map only accepts the handle type of its own domain.
static_assert(CanLookUp<k::SlotMap<int, k::ObjectHandle>, k::ObjectHandle>);
static_assert(!CanLookUp<k::SlotMap<int, k::ObjectHandle>, OtherHandle>);
static_assert(!CanLookUp<k::SlotMap<int, k::ObjectHandle>, k::Handle>);
static_assert(!CanErase<k::SlotMap<int, k::ObjectHandle>, OtherHandle>);
static_assert(CanLookUp<k::SlotMap<int>, k::Handle>);  // default stays untyped
static_assert(!CanLookUp<k::SlotMap<int>, k::ObjectHandle>);

KTEST(handle_typed_map_behaves_like_untyped_map) {
    k::SlotMap<int, k::ObjectHandle> m;
    const k::ObjectHandle a = m.insert(1);
    KCHECK(m.get(a) != nullptr && *m.get(a) == 1);
    KCHECK(m.erase(a));
    KCHECK(m.status(a) == k::HandleStatus::Stale);
    KCHECK(m.status(k::kNullObjectHandle) == k::HandleStatus::Null);
    const k::ObjectHandle b = m.insert(2);
    KCHECK_EQ(a.index, b.index);
    KCHECK(b.generation > a.generation);
    KCHECK(m.get(a) == nullptr);
}
