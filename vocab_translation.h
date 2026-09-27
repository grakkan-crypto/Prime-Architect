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

// Canonical token id — the union-space id shared across all models in a fusion.
using CanonicalId = uint32_t;
constexpr CanonicalId CANONICAL_UNKNOWN = 0; // reserved; unknown tokens map here

// Per-model translation tables — built once, queried at generation time.
struct ModelTranslation {
    std::string              source_path;       // the GGUF file this covers
    std::vector<CanonicalId> native_to_canon;   // index == native token id
    std::vector<uint32_t>    canon_to_native;   // index == canonical id; 0 == unknown
};

// The compiled union for one pipeline. Immutable after construction.
class VocabUnion {
public:
    // Number of distinct tokens in the canonical space (including UNKNOWN at 0).
    uint32_t canonical_size() const { return static_cast<uint32_t>(canon_to_text_.size()); }

    // Translate a native token id from a specific model into canonical space.
    CanonicalId to_canonical(const std::string& source_path, uint32_t native_id) const;

    // Translate a canonical id back to a native id for a specific model.
    // Returns 0 (UNKNOWN) if the canonical token has no equivalent in that model.
    uint32_t to_native(const std::string& source_path, CanonicalId canon_id) const;

    // The text string for a canonical id. Empty string for UNKNOWN.
    const std::string& canonical_text(CanonicalId id) const;

    // Whether a canonical token has a native equivalent in a given model.
    bool is_known(const std::string& source_path, CanonicalId canon_id) const;

    // All source paths in this union.
    std::vector<std::string> members() const;

private:
    friend class VocabTranslationLayer;

    std::vector<std::string>                             canon_to_text_;
    std::unordered_map<std::string, ModelTranslation>    by_path_;
    std::string                                          empty_; // returned for UNKNOWN text
};

// The compiler. Stateless — each build() call produces a fresh VocabUnion.
// Called at pool load time with every VocabMap declared for that pipeline.
class VocabTranslationLayer {
public:
    // Build the canonical union from a set of vocab maps. All maps are treated
    // uniformly regardless of family — same-family pairs are not special-cased.
    VocabUnion build(const std::vector<const VocabMap*>& maps) const;
};

} // namespace prime
