#pragma once
#include "engine/portable/assets.h"
#include <functional>
#include <memory>
#include <string_view>
class Model;
// Cached models are weakly indexed. Callers and parents keep shared
// ownership, so cache teardown cannot invalidate their geometry or animation.
class ModelCache {
public:
    struct Source {
        nolf::AssetSource asset;
        std::uint16_t file_id = 0xffff;
    };
    using Resolver = std::function<Source(std::string_view)>;
    explicit ModelCache(Resolver resolver);
    explicit ModelCache(std::shared_ptr<const nolf::Assets> assets);
    ~ModelCache();
    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;
    std::shared_ptr<Model> Load(std::string_view name);
    void Prune();
    void Invalidate(std::string_view name);
    void Clear();
    std::size_t Entries() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
