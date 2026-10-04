// vocab_translation.cpp — Prime Engine vocabulary translation layer

#include "vocab_translation.h"
#include <stdexcept>

namespace prime {

CanonicalId VocabUnion::to_canonical(const std::string& source_path,
                                      uint32_t native_id) const {
    auto it = by_path_.find(source_path);
    if (it == by_path_.end()) return CANONICAL_UNKNOWN;
    const auto& tbl = it->second.native_to_canon;
    if (native_id >= tbl.size()) return CANONICAL_UNKNOWN;
    return tbl[native_id];
}

uint32_t VocabUnion::to_native(const std::string& source_path,
                                CanonicalId canon_id) const {
    auto it = by_path_.find(source_path);
    if (it == by_path_.end()) return 0;
    const auto& tbl = it->second.canon_to_native;
    if (canon_id >= tbl.size()) return 0;
    return tbl[canon_id];
}

const std::string& VocabUnion::canonical_text(CanonicalId id) const {
    if (id >= canon_to_text_.size()) return empty_;
    return canon_to_text_[id];
}

bool VocabUnion::is_known(const std::string& source_path,
                           CanonicalId canon_id) const {
    return to_native(source_path, canon_id) != 0 || canon_id == 0;
}

std::vector<std::string> VocabUnion::members() const {
    std::vector<std::string> out;
    out.reserve(by_path_.size());
    for (const auto& [path, _] : by_path_) out.push_back(path);
    return out;
}

VocabUnion VocabTranslationLayer::build(const std::vector<const VocabMap*>& maps) const {
    VocabUnion u;

    u.canon_to_text_.push_back("");

    std::unordered_map<std::string, CanonicalId> text_to_canon;
    text_to_canon[""] = CANONICAL_UNKNOWN;

    for (const VocabMap* vm : maps) {
        if (!vm) continue;
        for (const auto& text : vm->id_to_text) {
            if (text.empty()) continue;
            if (!text_to_canon.count(text)) {
                CanonicalId cid = static_cast<CanonicalId>(u.canon_to_text_.size());
                text_to_canon[text] = cid;
                u.canon_to_text_.push_back(text);
            }
        }
    }

    const uint32_t canon_size = static_cast<uint32_t>(u.canon_to_text_.size());

    for (const VocabMap* vm : maps) {
        if (!vm) continue;

        ModelTranslation mt;
        mt.source_path = vm->tokenizer_model;

        mt.native_to_canon.resize(vm->id_to_text.size(), CANONICAL_UNKNOWN);
        for (uint32_t nid = 0; nid < vm->id_to_text.size(); ++nid) {
            auto it = text_to_canon.find(vm->id_to_text[nid]);
            if (it != text_to_canon.end())
                mt.native_to_canon[nid] = it->second;
        }

        mt.canon_to_native.resize(canon_size, 0);
        for (uint32_t nid = 0; nid < vm->id_to_text.size(); ++nid) {
            auto it = text_to_canon.find(vm->id_to_text[nid]);
            if (it != text_to_canon.end() && it->second != CANONICAL_UNKNOWN)
                mt.canon_to_native[it->second] = nid;
        }

        u.by_path_.emplace(mt.source_path, std::move(mt));
    }

    return u;
}

}
