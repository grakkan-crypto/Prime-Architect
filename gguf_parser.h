// gguf_parser.h — Prime Engine GGUF loader
//
// Maps a GGUF file directly into the flat address space, parses its header
// and metadata, resolves every tensor to an absolute address, and pins the
// vocabulary. Emits one WeightRegion.
//
// CONTRACT
//   - load(path) returns a WeightRegion whose tensor data pointers address
//     directly into the mapping. Nothing is copied.
//   - The same path loaded twice returns the SAME region (ref-counted). This
//     is the shared-model case: Arbiter_1, Arbiter_2 and Agent all point at
//     one GGUF and get one mapping between them.
//   - The mapping lives for as long as the WeightRegion is referenced. The
//     last release() unmaps.
//
// This component reads files and addresses memory. It makes no allocation,
// pool, or orchestration decisions — those belong to the layers above it.

#pragma once

#include "prime_types.h"
#include <memory>
#include <mutex>
#include <string>

namespace prime {

class GgufParser {
public:
    GgufParser();
    ~GgufParser();

    GgufParser(const GgufParser&) = delete;
    GgufParser& operator=(const GgufParser&) = delete;

    WeightRegion* load(const std::string& path);

    void release(const std::string& path);

    size_t mapped_count() const;

private:
    struct Mapping;

    std::unique_ptr<Mapping> open_mapping(const std::string& path);
    void  parse(WeightRegion& region, const Mapping& m);

    struct Entry {
        std::unique_ptr<Mapping> mapping;
        WeightRegion             region;
    };

    mutable std::mutex                                  mutex_;
    std::unordered_map<std::string, std::unique_ptr<Entry>> entries_;
};

}
