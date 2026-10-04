// vocab_translation.h — Prime Engine vocabulary translation layer
//
// Builds and holds the canonical token union for a fusion at pool load time.
// Every model declared in a pipeline has its translation pre-compiled before
// any generation begins. By the time an agent writes to a pool, the
// translation between every model family touching that pool is already waiting.
//
// SCOPE
//   One concern: given a set of VocabMaps, produce a canonical union and
//   answer translation queries against it. Nothing else. It does not touch
//   the allocator, the pool compiler, or any agent directly.
//
// CANONICAL UNION
//   The union is the set of all distinct token strings across every model in
//   the fusion, each assigned a canonical id. Native model ids map to and from
//   canonical ids through pre-built lookup tables — one per model in the
//   fusion. Same-family fusions are treated identically to cross-family
//   fusions. No special casing, no branching, no additional failure surface.
//
// UNKNOWN TOKENS
//   A token present in one model's vocabulary but absent from another's maps
//   to a typed UNKNOWN primitive in the receiving model's space. This is a
//   declared unknown — the receiving agent sees it explicitly rather than
//   silently misreading it. The confidence axis (Prime) carries the epistemic
//   weight; an UNKNOWN token always arrives with confidence:0.0 so downstream
//   agents treat it accordingly.
//
// LIFECYCLE
//   Built once at pipeline load. Immutable for the lifetime of that pipeline.
//   Discarded and rebuilt when a new pool matrix arrives (tab switch, specialist
//   bolt-on). The specialist's translation is built alongside its dormant pools
//   at tab load — zero translation latency when it activates.

#pragma once

#include "prime_types.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace prime {

using CanonicalId = uint32_t;
constexpr CanonicalId CANONICAL_UNKNOWN = 0;

struct ModelTranslation {
    std::string              source_path;
    std::vector<CanonicalId> native_to_canon;
    std::vector<uint32_t>    canon_to_native;
};

class VocabUnion {
public:

    uint32_t canonical_size() const { return static_cast<uint32_t>(canon_to_text_.size()); }

    CanonicalId to_canonical(const std::string& source_path, uint32_t native_id) const;

    uint32_t to_native(const std::string& source_path, CanonicalId canon_id) const;

    const std::string& canonical_text(CanonicalId id) const;

    bool is_known(const std::string& source_path, CanonicalId canon_id) const;

    std::vector<std::string> members() const;

private:
    friend class VocabTranslationLayer;

    std::vector<std::string>                             canon_to_text_;
    std::unordered_map<std::string, ModelTranslation>    by_path_;
    std::string                                          empty_;
};

class VocabTranslationLayer {
public:

    VocabUnion build(const std::vector<const VocabMap*>& maps) const;
};

}
