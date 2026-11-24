// Ownership of a parked conversation. Tokens and checkpoints decide what can be reused.
#pragma once

#include <cstddef>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace maya::threads {

struct ThreadTurn {
    std::string key;        // SHA-256 of the caller-scoped header, empty for anonymous requests
    uint64_t order = 0;     // HTTP admission order, shared by every engine call of that request

    bool same(const ThreadTurn& other) const { return !key.empty() && key == other.key; }
    bool newer(const ThreadTurn& other) const { return same(other) && order > other.order; }
};

inline bool thread_digest(const std::string& key) {
    return key.size() == 64 && key.find_first_not_of("0123456789abcdef") == std::string::npos;
}

inline ThreadTurn parse_thread(const std::string& field) {
    if (field.size() < 66 || field.size() > 85 || field[64] != ':') return {};
    auto key = field.substr(0, 64);
    uint64_t order = 0;
    const auto end = field.data() + field.size();
    const auto n = std::from_chars(field.data() + 65, end, order);
    if (n.ec != std::errc{} || n.ptr != end || order == 0 || !thread_digest(key)) return {};
    return {std::move(key), order};
}

inline bool replaces_slot(const ThreadTurn& incoming, const ThreadTurn& stored,
                          const std::vector<int32_t>& tokens, const std::vector<int32_t>& old_tokens) {
    if (!incoming.key.empty() || !stored.key.empty()) return incoming.same(stored) && incoming.order >= stored.order;
    return old_tokens.size() <= tokens.size() && std::equal(old_tokens.begin(), old_tokens.end(), tokens.begin());
}

struct ConversationOwner {
    ThreadTurn turn;
    bool retired = false;  // a newer usable turn exists, or this lane's prompt has not finished reading

    // Retire older lanes only once their replacement has actually serialized. A short prompt,
    // cancellation, projected pass or failed save must leave the last usable primary available.
    static void parked(std::vector<ConversationOwner>& lanes, const ThreadTurn& saved) {
        for (auto& lane : lanes)
            if (saved.newer(lane.turn)) lane.retired = true;
    }
};

}   // namespace maya::threads
