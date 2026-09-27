// gguf_parser.cpp — Prime Engine GGUF loader implementation
//
// POSIX memory-mapped, single-pass header parse. The tensor data span is left
// mapped and addressed in place; only the header/metadata region is walked at
// load time. The whole file is mapped read-only with mmap; the kernel pages it
// in on demand, so mapping a 48GB weight file does not read 48GB up front —
// tensor pages fault in as kernels touch them.

#include "gguf_parser.h"

#include <stdexcept>
#include <cstring>
#include <algorithm>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace prime {

// ---------------------------------------------------------------------------
// GGUF on-disk constants
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t GGUF_MAGIC = 0x46554747; // "GGUF" little-endian
constexpr uint32_t GGUF_DEFAULT_ALIGNMENT = 32;

// GGUF metadata value type ids
enum : uint32_t {
    GT_UINT8 = 0, GT_INT8 = 1, GT_UINT16 = 2, GT_INT16 = 3,
    GT_UINT32 = 4, GT_INT32 = 5, GT_FLOAT32 = 6, GT_BOOL = 7,
    GT_STRING = 8, GT_ARRAY = 9, GT_UINT64 = 10, GT_INT64 = 11,
    GT_FLOAT64 = 12,
};

// A forward-only cursor over the mapped bytes with bounds checking. Every read
// validates against the end pointer so a truncated or hostile file faults here
// rather than walking off the mapping.
class Cursor {
public:
    Cursor(const uint8_t* p, const uint8_t* end) : p_(p), end_(end) {}

    const uint8_t* ptr() const { return p_; }
    size_t remaining() const { return static_cast<size_t>(end_ - p_); }

    template <typename T>
    T read_scalar() {
        require(sizeof(T));
        T v;
        std::memcpy(&v, p_, sizeof(T));
        p_ += sizeof(T);
        return v;
    }

    std::string read_string() {
        uint64_t len = read_scalar<uint64_t>();
        require(len);
        std::string s(reinterpret_cast<const char*>(p_), len);
        p_ += len;
        return s;
    }

    void skip(size_t n) { require(n); p_ += n; }

private:
    void require(size_t n) const {
        if (n > remaining())
            throw std::runtime_error("GGUF parse: read past end of file");
    }
    const uint8_t* p_;
    const uint8_t* end_;
};

size_t scalar_size(uint32_t type) {
    switch (type) {
        case GT_UINT8:  case GT_INT8:  case GT_BOOL:   return 1;
        case GT_UINT16: case GT_INT16:                 return 2;
        case GT_UINT32: case GT_INT32: case GT_FLOAT32: return 4;
        case GT_UINT64: case GT_INT64: case GT_FLOAT64: return 8;
        default: return 0; // STRING / ARRAY handled separately
    }
}

// Read a single metadata value, returning a uint64 when the value is integral
// (so manifest fields can pull numbers uniformly). String/array handling is
// specialised by the caller; here we consume and discard structure we don't
// surface, keeping the cursor aligned.
uint64_t read_uint_value(Cursor& c, uint32_t type) {
    switch (type) {
        case GT_UINT8:  return c.read_scalar<uint8_t>();
        case GT_INT8:   return static_cast<uint64_t>(c.read_scalar<int8_t>());
        case GT_UINT16: return c.read_scalar<uint16_t>();
        case GT_INT16:  return static_cast<uint64_t>(c.read_scalar<int16_t>());
        case GT_UINT32: return c.read_scalar<uint32_t>();
        case GT_INT32:  return static_cast<uint64_t>(c.read_scalar<int32_t>());
        case GT_UINT64: return c.read_scalar<uint64_t>();
        case GT_INT64:  return static_cast<uint64_t>(c.read_scalar<int64_t>());
        case GT_BOOL:   return c.read_scalar<uint8_t>();
        default:        return 0;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ModelFamily helpers
// ---------------------------------------------------------------------------
ModelFamily family_from_arch(const std::string& arch) {
    if (arch == "qwen2") return ModelFamily::Qwen2;
    if (arch == "qwen3") return ModelFamily::Qwen3;
    if (arch == "llama") return ModelFamily::Llama;
    if (arch == "gemma" || arch == "gemma2" || arch == "gemma3")
        return ModelFamily::Gemma;
    if (arch == "phi2" || arch == "phi3") return ModelFamily::Phi;
    if (arch == "mistral") return ModelFamily::Mistral;
    return ModelFamily::Unknown;
}

const char* family_name(ModelFamily f) {
    switch (f) {
        case ModelFamily::Qwen2:   return "Qwen2";
        case ModelFamily::Qwen3:   return "Qwen3";
        case ModelFamily::Llama:   return "Llama";
        case ModelFamily::Gemma:   return "Gemma";
        case ModelFamily::Phi:     return "Phi";
        case ModelFamily::Mistral: return "Mistral";
        default:                   return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Platform mapping — POSIX mmap over the whole file, read-only.
// ---------------------------------------------------------------------------
struct GgufParser::Mapping {
    int            fd   = -1;
    const uint8_t* view = nullptr;
    uint64_t       size = 0;

    ~Mapping() {
        if (view) ::munmap(const_cast<uint8_t*>(view), size);
        if (fd >= 0) ::close(fd);
    }
};

std::unique_ptr<GgufParser::Mapping>
GgufParser::open_mapping(const std::string& path) {
    auto m = std::make_unique<Mapping>();

    m->fd = ::open(path.c_str(), O_RDONLY);
    if (m->fd < 0)
        throw std::runtime_error("GGUF: cannot open file: " + path);

    struct stat st{};
    if (::fstat(m->fd, &st) != 0)
        throw std::runtime_error("GGUF: cannot size file: " + path);
    m->size = static_cast<uint64_t>(st.st_size);

    void* view = ::mmap(nullptr, m->size, PROT_READ, MAP_PRIVATE, m->fd, 0);
    if (view == MAP_FAILED)
        throw std::runtime_error("GGUF: mmap failed: " + path);
    m->view = static_cast<const uint8_t*>(view);

    // The header/metadata region is walked sequentially right now; the tensor
    // payload is touched randomly later by kernels. Advise the kernel of both
    // so readahead is sensible without forcing the whole file resident.
    ::madvise(const_cast<uint8_t*>(m->view), m->size, MADV_RANDOM);

    return m;
}

// ---------------------------------------------------------------------------
// Parse — walk header, metadata, tensor table; resolve tensor addresses.
// ---------------------------------------------------------------------------
void GgufParser::parse(WeightRegion& region, const Mapping& m) {
    const uint8_t* begin = m.view;
    const uint8_t* end   = m.view + m.size;
    Cursor c(begin, end);

    if (c.read_scalar<uint32_t>() != GGUF_MAGIC)
        throw std::runtime_error("GGUF: bad magic in " + region.source_path);

    const uint32_t version = c.read_scalar<uint32_t>();
    if (version < 2 || version > 3)
        throw std::runtime_error("GGUF: unsupported version in " + region.source_path);

    const uint64_t tensor_count = c.read_scalar<uint64_t>();
    const uint64_t kv_count     = c.read_scalar<uint64_t>();

    ModelManifest& man = region.manifest;
    uint32_t alignment = GGUF_DEFAULT_ALIGNMENT;

    // Deferred resolution: arch is announced by general.architecture, but the
    // arch-prefixed keys (e.g. "qwen2.block_count") may appear in any order.
    // Collect prefixed numerics into a side table keyed by suffix, resolve
    // after the metadata pass.
    std::unordered_map<std::string, uint64_t> arch_numeric;

    for (uint64_t i = 0; i < kv_count; ++i) {
        std::string key = c.read_string();
        uint32_t vtype  = c.read_scalar<uint32_t>();

        // ---- string-valued keys we care about ----
        if (vtype == GT_STRING) {
            std::string val = c.read_string();
            if (key == "general.architecture") {
                man.architecture = val;
                man.family = family_from_arch(val);
            } else if (key == "tokenizer.ggml.model") {
                region.vocab.tokenizer_model = val;
            }
            continue;
        }

        // ---- array-valued keys: only the token list is surfaced ----
        if (vtype == GT_ARRAY) {
            uint32_t elem_type = c.read_scalar<uint32_t>();
            uint64_t len       = c.read_scalar<uint64_t>();

            if (key == "tokenizer.ggml.tokens" && elem_type == GT_STRING) {
                region.vocab.id_to_text.reserve(len);
                for (uint64_t t = 0; t < len; ++t)
                    region.vocab.id_to_text.push_back(c.read_string());
            } else if (elem_type == GT_STRING) {
                for (uint64_t t = 0; t < len; ++t) (void)c.read_string();
            } else {
                size_t es = scalar_size(elem_type);
                if (es == 0)
                    throw std::runtime_error("GGUF: nested/unknown array element type");
                c.skip(es * len);
            }
            continue;
        }

        // ---- scalar numerics ----
        // read_uint_value consumes integral types and returns the value; for
        // float types it returns 0 and consumes nothing, so we skip them here.
        uint64_t num = read_uint_value(c, vtype);
        if (vtype == GT_FLOAT32) c.skip(4);
        if (vtype == GT_FLOAT64) c.skip(8);

        if (key == "tokenizer.ggml.eos_token_id") {
            // The stop token, surfaced by name — the generic prefix-strip
            // below would mangle this key into the arch table and lose it.
            // Presence is stated explicitly: 0 is a valid token id, so
            // absence is never represented by a default.
            man.eos_token_id      = num;
            man.eos_token_present = true;
        } else if (key == "general.alignment") {
            alignment = static_cast<uint32_t>(num ? num : GGUF_DEFAULT_ALIGNMENT);
        } else {
            // strip the arch prefix if present: "qwen2.block_count" -> "block_count"
            auto dot = key.find('.');
            if (dot != std::string::npos)
                arch_numeric[key.substr(dot + 1)] = num;
        }
    }

    // Resolve manifest numerics from the suffix table.
    auto pick = [&](const char* suffix) -> uint64_t {
        auto it = arch_numeric.find(suffix);
        return it == arch_numeric.end() ? 0 : it->second;
    };
    man.context_length      = pick("context_length");
    man.embedding_length     = pick("embedding_length");
    man.block_count          = pick("block_count");
    man.head_count           = pick("attention.head_count");
    man.head_count_kv        = pick("attention.head_count_kv");
    man.feed_forward_length  = pick("feed_forward_length");
    man.expert_count         = pick("expert_count");
    man.expert_used_count    = pick("expert_used_count");
    man.is_moe               = man.expert_count > 0;
    if (man.head_count_kv == 0) man.head_count_kv = man.head_count;
    man.vocab_size           = region.vocab.id_to_text.size();

    // ---- tensor table ----
    region.tensors.reserve(tensor_count);
    for (uint64_t i = 0; i < tensor_count; ++i) {
        TensorDesc t;
        t.name = c.read_string();
        uint32_t n_dims = c.read_scalar<uint32_t>();
        t.dims.reserve(n_dims);
        for (uint32_t d = 0; d < n_dims; ++d)
            t.dims.push_back(c.read_scalar<uint64_t>());
        t.ggml_type = c.read_scalar<uint32_t>();
        t.offset    = c.read_scalar<uint64_t>();
        region.tensors.push_back(std::move(t));
    }

    // Tensor data begins at the next alignment boundary after the table.
    uint64_t header_bytes = static_cast<uint64_t>(c.ptr() - begin);
    uint64_t pad = (alignment - (header_bytes % alignment)) % alignment;
    const uint8_t* data_base = c.ptr() + pad;
    if (data_base > end)
        throw std::runtime_error("GGUF: tensor data base past end of file");

    // Resolve each tensor to an absolute address inside the mapping.
    for (auto& t : region.tensors)
        t.data = data_base + t.offset;

    // The region spans from the data base to the end of the file — that is the
    // weight payload the allocator accounts for in the flat address space.
    region.base   = data_base;
    region.extent = static_cast<uint64_t>(end - data_base);
}

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------
GgufParser::GgufParser() = default;
GgufParser::~GgufParser() = default;

WeightRegion* GgufParser::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (auto it = entries_.find(path); it != entries_.end()) {
        it->second->region.ref_count++;
        return &it->second->region;
    }

    auto entry = std::make_unique<Entry>();
    entry->mapping = open_mapping(path);
    entry->region.source_path = path;
    entry->region.ref_count   = 1;
    parse(entry->region, *entry->mapping);

    WeightRegion* out = &entry->region;
    entries_.emplace(path, std::move(entry));
    return out;
}

void GgufParser::release(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(path);
    if (it == entries_.end()) return;
    if (--it->second->region.ref_count == 0)
        entries_.erase(it);  // Entry dtor unmaps the file
}

size_t GgufParser::mapped_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

} // namespace prime
