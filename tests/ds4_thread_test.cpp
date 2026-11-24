// The same replacement and checkpoint rules used by DS4's serial and multi-lane serving loops.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ds4/ds4_thread.hpp"
#include <cassert>

using maya::ds4::ConversationOwner;
using maya::ds4::ThreadTurn;
using maya::ds4::parse_thread;
using maya::ds4::replaces_slot;
using maya::ds4::slot_reuse;

int main() {
    const std::string a(64, 'a'), b(64, 'b');
    const ThreadTurn first = parse_thread(a + ":1"), second = parse_thread(a + ":3"), other = parse_thread(b + ":2");
    assert(first.key == a && first.order == 1);
    assert(parse_thread(a + ":18446744073709551615").order == UINT64_MAX);
    for (const auto& suffix : {"", ":0", ":18446744073709551616", ":-1", ":2x", ":1 rid=2"})
        assert(parse_thread(a + suffix).key.empty());
    assert(parse_thread(std::string(63, 'a') + ":2").key.empty());
    assert(parse_thread(std::string(64, 'G') + ":2").key.empty());

    std::vector<int32_t> old_tail{1, 2, 3, 4, 5}, edited{1, 2, 3, 8}, continuation{1, 2, 3, 4, 5, 6};
    // A, B, then A with an edited tail. A's ID supersedes its prior generation even with no prefix
    // relation. It cannot delete B, even if B happens to be an exact prefix.
    assert(replaces_slot(second, first, edited, old_tail));
    assert(!replaces_slot(second, other, continuation, old_tail));
    assert(!replaces_slot(other, first, continuation, old_tail));
    assert(!replaces_slot(first, second, old_tail, edited));
    assert(replaces_slot(second, ThreadTurn{a, 0}, edited, old_tail));  // indexed before restart

    // Same ID with a changed tail: the run agrees through 3, but only token 2 has a tensor checkpoint.
    assert(slot_reuse(old_tail, edited, {2, 4}, SIZE_MAX) == 2);
    assert(slot_reuse(old_tail, edited, {2, 4}, 1) == 0);
    assert(slot_reuse(old_tail, continuation, {2, 4}, SIZE_MAX) == old_tail.size());
    assert(slot_reuse(old_tail, old_tail, {2, 4}, SIZE_MAX) == 4); // recompute the last prompt token
    assert(slot_reuse(old_tail, {8, 9}, {2, 4}, SIZE_MAX) == 0);
    // An anonymous request still coalesces exact continuations, and distinct branches stay distinct.
    assert(replaces_slot({}, {}, continuation, old_tail));
    assert(!replaces_slot({}, {}, edited, old_tail));
    assert(!replaces_slot({}, first, continuation, old_tail));

    std::vector<ConversationOwner> lanes{{first, false}, {other, false}, {second, false}, {}};
    ConversationOwner::parked(lanes, second);
    assert(lanes[0].retired && !lanes[1].retired && !lanes[2].retired && !lanes[3].retired);
    ConversationOwner::parked(lanes, first);
    assert(!lanes[2].retired);
}
