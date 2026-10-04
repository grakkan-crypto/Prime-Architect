// dispatch.cpp — Prime Engine model dispatch framework implementation

#include "dispatch.h"

#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <string>

namespace prime {

Contract contract_from_department(const std::string& department) {
    if (department == "Artist")    return Contract::TextToImage;
    if (department == "Artisan")   return Contract::TextToVideo;
    if (department == "Aperture")  return Contract::ImageToText;
    if (department == "Accord")    return Contract::AudioToText;
    if (department == "Announcer") return Contract::TextToAudio;

    return Contract::TextToText;
}

const char* contract_name(Contract c) {
    switch (c) {
        case Contract::TextToText:  return "text->text";
        case Contract::TextToImage: return "text->image";
        case Contract::TextToVideo: return "text->video";
        case Contract::ImageToText: return "image->text";
        case Contract::AudioToText: return "audio->text";
        case Contract::TextToAudio: return "text->audio";
        default:                    return "unknown";
    }
}

namespace {

bool has_suffix(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

bool is_directory(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool dir_contains(const std::string& path, const std::function<bool(const std::string&)>& pred) {
    DIR* d = ::opendir(path.c_str());
    if (!d) return false;
    bool found = false;
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (pred(name)) { found = true; break; }
    }
    ::closedir(d);
    return found;
}

}

ModelFormat detect_format(const std::string& model_path) {

    if (has_suffix(model_path, ".gguf")) return ModelFormat::Gguf;
    if (has_suffix(model_path, ".onnx")) return ModelFormat::Onnx;

    if (is_directory(model_path)) {

        const bool hybrid = dir_contains(model_path, [](const std::string& n) {
            return n == "hybrid_partition.json" || n == "hybrid.json";
        });
        if (hybrid) return ModelFormat::Hybrid;

        const bool pipeline = dir_contains(model_path, [](const std::string& n) {
            return n == "model_index.json"
                || n == "scheduler_config.json"
                || n == "preprocessor_config.json"
                || n == "tts_config.json"
                || n == "voices";
        });
        if (pipeline) return ModelFormat::Pipeline;

        const bool onnx = dir_contains(model_path, [](const std::string& n) {
            return has_suffix(n, ".onnx");
        });
        if (onnx) return ModelFormat::Onnx;
    }

    return ModelFormat::Unknown;
}

const char* format_name(ModelFormat f) {
    switch (f) {
        case ModelFormat::Gguf:     return "GGUF";
        case ModelFormat::Onnx:     return "ONNX";
        case ModelFormat::Hybrid:   return "Hybrid";
        case ModelFormat::Pipeline: return "Pipeline";
        default:                    return "Unknown";
    }
}

ComputeTarget target_from_string(const std::string& s) {
    if (s == "rdna" || s == "vram" || s == "gpu") return ComputeTarget::RDNA;
    if (s == "xdna" || s == "npu")                return ComputeTarget::XDNA;
    if (s == "hybrid")                            return ComputeTarget::Hybrid;
    return ComputeTarget::Unknown;
}

const char* target_name(ComputeTarget t) {
    switch (t) {
        case ComputeTarget::RDNA:   return "RDNA";
        case ComputeTarget::XDNA:   return "XDNA";
        case ComputeTarget::Hybrid: return "Hybrid";
        default:                    return "Unknown";
    }
}

const char* status_name(DispatchStatus s) {
    switch (s) {
        case DispatchStatus::Ok:                  return "ok";
        case DispatchStatus::KernelUnavailable:   return "kernel_unavailable";
        case DispatchStatus::UnsupportedContract: return "unsupported_contract";
        case DispatchStatus::BadInput:            return "bad_input";
        case DispatchStatus::ModelError:          return "model_error";
        default:                                  return "unknown";
    }
}

namespace {
DispatchResult unavailable(const DispatchRequest& req) {
    DispatchResult r;
    r.status = DispatchStatus::KernelUnavailable;
    r.detail = std::string("no kernel bound for ") +
               contract_name(req.contract) + " / " +
               format_name(req.format) + " / " +
               target_name(req.target) +
               " — awaiting Prime_Power";
    return r;
}
}

DispatchResult StubKernelBackend::generate_text(const DispatchRequest& req, const TokenSink&) {
    return unavailable(req);
}
DispatchResult StubKernelBackend::generate_media(const DispatchRequest& req) {
    return unavailable(req);
}
DispatchResult StubKernelBackend::understand(const DispatchRequest& req) {
    return unavailable(req);
}
DispatchResult StubKernelBackend::synthesize_speech(const DispatchRequest& req) {
    return unavailable(req);
}

namespace {
DispatchResult call_generate_text(KernelBackend& b, const DispatchRequest& r, const TokenSink& s) {
    return b.generate_text(r, s);
}
DispatchResult call_generate_media(KernelBackend& b, const DispatchRequest& r, const TokenSink&) {
    return b.generate_media(r);
}
DispatchResult call_understand(KernelBackend& b, const DispatchRequest& r, const TokenSink&) {
    return b.understand(r);
}
DispatchResult call_synthesize_speech(KernelBackend& b, const DispatchRequest& r, const TokenSink&) {
    return b.synthesize_speech(r);
}
}

KernelCall kernel_call_for(Contract c) {
    switch (c) {
        case Contract::TextToText:  return &call_generate_text;
        case Contract::TextToImage:
        case Contract::TextToVideo: return &call_generate_media;
        case Contract::ImageToText:
        case Contract::AudioToText: return &call_understand;
        case Contract::TextToAudio: return &call_synthesize_speech;
        default:                    return nullptr;
    }
}

}
