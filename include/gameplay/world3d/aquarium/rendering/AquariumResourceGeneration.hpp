#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

template <typename Resource>
class AquariumResourceGeneration {
public:
    const std::vector<Resource>& active() const { return active_; }
    std::vector<Resource>& active() { return active_; }
    std::uint64_t generation() const { return generation_; }

    template <typename Destroy>
    bool stage(std::vector<Resource> candidate, bool candidate_valid, Destroy destroy) {
        discardStaged(destroy);
        if (!candidate_valid) {
            destroyAll(candidate, destroy);
            return false;
        }
        staged_ = std::move(candidate);
        return true;
    }

    template <typename Destroy>
    bool publishStaged(Destroy destroy) {
        if (!staged_) return false;
        std::vector<Resource> previous = std::move(active_);
        active_ = std::move(*staged_);
        staged_.reset();
        ++generation_;
        destroyAll(previous, destroy);
        return true;
    }

    template <typename Destroy>
    bool publish(std::vector<Resource> candidate, bool candidate_valid, Destroy destroy) {
        return stage(std::move(candidate), candidate_valid, destroy) && publishStaged(destroy);
    }

    template <typename Destroy>
    void discardStaged(Destroy destroy) {
        if (!staged_) return;
        destroyAll(*staged_, destroy);
        staged_.reset();
    }

    template <typename Destroy>
    void clear(Destroy destroy) {
        discardStaged(destroy);
        destroyAll(active_, destroy);
        active_.clear();
    }

private:
    template <typename Destroy>
    static void destroyAll(std::vector<Resource>& resources, Destroy destroy) {
        for (Resource& resource : resources) destroy(resource);
    }

    std::vector<Resource> active_;
    std::optional<std::vector<Resource>> staged_;
    std::uint64_t generation_ = 0;
};

} // namespace pr::gameplay::world3d::aquarium::rendering
