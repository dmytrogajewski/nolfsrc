#pragma once
#include "model_cache.h"
#include <cstdint>
#include <vector>
#include <string>
class CFileMgr;
class ModelService {
public:
    using Ticket = std::uint64_t;
    struct Load { Ticket ticket = 0; std::shared_ptr<Model> model; std::uint32_t status = 0; };
    struct Completion { bool accepted = false; std::uint32_t status = 0; std::shared_ptr<Model> model; };
    explicit ModelService(CFileMgr& files);
    explicit ModelService(ModelCache::Resolver resolve);
    ~ModelService();
    ModelService(const ModelService&) = delete;
    ModelService& operator=(const ModelService&) = delete;
    // Loading may run on a worker. Its ticket retains the result until the
    // owner publishes it, discards it, or terminates the service.
    Ticket Reserve(std::string_view name);
    Load LoadReserved(Ticket ticket);
    Load BeginLoad(std::string_view name);
    std::vector<std::pair<Ticket,std::string>> Ready() const;
    std::shared_ptr<Model> Complete(Ticket ticket);
    Completion Finish(Ticket ticket);
    void Discard(Ticket ticket);
    std::shared_ptr<Model> Find(std::string_view name) const;
    std::shared_ptr<Model> Retain(Model* model) const;
    bool HasPending(std::string_view name) const;
    std::shared_ptr<Model> Unload(std::string_view name);
    std::vector<std::string> CollectUnused(const std::function<void(std::string_view)>& retire = {});
    void CancelAll();
    void Term();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
