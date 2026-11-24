// Thread ownership for GLM Flash conversation slots. Reuse still depends on the model state.
#pragma once

#include "thread_turn.hpp"
#include <istream>
#include <ostream>

namespace maya::glm_slots {
using threads::ConversationOwner;
using threads::ThreadTurn;

// The legacy metadata ends directly after the footer. Restarted servers admit turns from one again.
inline bool read_owner(std::istream& input, ThreadTurn& thread) {
    thread = {};
    if (input.peek() == std::istream::traits_type::eof()) return input.eof() && !input.bad();
    std::string digest(64, '\0');
    if (!input.read(digest.data(), 64) || !threads::thread_digest(digest) ||
        input.peek() != std::istream::traits_type::eof() || input.bad()) return false;
    thread.key = std::move(digest);
    return true;
}

inline void write_owner(std::ostream& output, const ThreadTurn& thread) {
    if (!thread.key.empty()) output.write(thread.key.data(), (std::streamsize) thread.key.size());
}

inline bool supersedes(const ThreadTurn& incoming, const ThreadTurn& stored, bool system_prefix,
                       bool anchor, bool same_images, const std::vector<int32_t>& tokens,
                       const std::vector<int32_t>& old_tokens) {
    if (system_prefix) return false;
    if (incoming.key.empty() && (anchor || !same_images)) return false;
    return threads::replaces_slot(incoming, stored, tokens, old_tokens);
}

// A GLM disk/RAM slot serializes a single snapshot position. A matching thread breaks ties only.
inline bool candidate(const std::vector<int32_t>& tokens, const std::vector<int32_t>& prompt,
                      size_t best, bool prefer) {
    return !tokens.empty() && (tokens.size() > best || (prefer && tokens.size() == best)) &&
           tokens.size() < prompt.size() && std::equal(tokens.begin(), tokens.end(), prompt.begin());
}

inline bool prefer_lane(size_t reuse, const ConversationOwner& owner, size_t best_reuse,
                        const ConversationOwner& best, const ThreadTurn& request) {
    if (reuse != best_reuse) return reuse > best_reuse;
    const bool affinity = !owner.retired && owner.turn.same(request);
    const bool best_affinity = !best.retired && best.turn.same(request);
    return affinity && (!best_affinity || owner.turn.order > best.turn.order);
}
}  // namespace maya::glm_slots
