#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../include/maya/program/glm_thread_slots.hpp"
#include <cassert>
#include <sstream>

int main() {
    using namespace maya::glm_slots;
    ThreadTurn a{std::string(64, 'a'), 5}, b{std::string(64, 'b'), 4}, old{a.key, 2};
    std::vector<int32_t> original{1, 2, 7}, edit{1, 2, 8}, continuation{1, 2, 7, 9};
    assert(supersedes(a, old, false, true, false, edit, original));
    assert(!supersedes(old, a, false, false, true, continuation, original));
    assert(!supersedes(a, b, false, false, true, continuation, original));
    assert(!supersedes(a, old, true, false, true, continuation, original));
    assert(!supersedes({}, a, false, false, true, continuation, original));
    assert(supersedes({}, {}, false, false, true, continuation, original));
    assert(!supersedes({}, {}, false, true, true, continuation, original));
    assert(!supersedes({}, {}, false, false, false, continuation, original));
    assert(!supersedes({}, {}, false, false, true, edit, original));
    assert(candidate(original, continuation, 0, false));
    assert(!candidate(original, edit, 0, true));  // same thread cannot restore a divergent snapshot
    assert(!candidate(original, original, 0, true));  // GLM needs the last prompt token for logits
    assert(!candidate(original, continuation, original.size(), false));
    assert(candidate(original, continuation, original.size(), true));
    assert(prefer_lane(8, {b}, 2, {a}, a));
    assert(prefer_lane(2, {a}, 2, {old}, a));
    assert(!prefer_lane(2, {a, true}, 2, {b}, a));
    assert(!prefer_lane(0, {a}, 2, {b}, a));

    std::stringstream kept;
    write_owner(kept, a);
    assert(kept.str() == a.key);
    ThreadTurn indexed;
    assert(read_owner(kept, indexed) && indexed.key == a.key && indexed.order == 0);
    std::stringstream legacy;
    assert(read_owner(legacy, indexed) && indexed.key.empty());
    for (const auto& bad : {a.key.substr(1), a.key + "x", std::string(64, 'z')}) {
        std::istringstream broken(bad);
        assert(!read_owner(broken, indexed) && indexed.key.empty());
    }
}
