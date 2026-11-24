#pragma once

#include "../../include/maya/program/thread_turn.hpp"

namespace maya::ds4 {
using threads::ConversationOwner;
using threads::ThreadTurn;
using threads::parse_thread;
using threads::replaces_slot;
using threads::thread_digest;

// A common run of tokens is usable only at the serialized end or a saved checkpoint.
inline size_t slot_reuse(const std::vector<int32_t>& saved, const std::vector<int>& prompt,
                         const std::vector<int64_t>& checkpoints, size_t picture_limit) {
    if (prompt.empty()) return 0;
    const size_t limit = std::min({saved.size(), prompt.size() - 1, picture_limit});
    size_t common = 0;
    while (common < limit && saved[common] == prompt[common]) ++common;
    if (common == saved.size()) return common;
    size_t reuse = 0;
    for (int64_t k : checkpoints)
        if (k > 0 && (size_t) k <= common) reuse = std::max(reuse, (size_t) k);
    return reuse;
}

}  // namespace maya::ds4
