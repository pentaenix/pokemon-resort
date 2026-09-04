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
    std::size_t retiredResourceCount() const {
        std::size_t count = 0;
        for (const RetiredGeneration& generation : retired_) count += generation.resources.size();
        return count;
    }

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
    bool publishStaged(Destroy destroy, std::uint32_t retirement_delay_frames = 0) {
        if (!staged_) return false;
        std::vector<Resource> previous = std::move(active_);
        active_ = std::move(*staged_);
        staged_.reset();
        ++generation_;
        if (retirement_delay_frames > 0 && !previous.empty()) {
            retired_.push_back({std::move(previous), retirement_delay_frames});
        } else {
            destroyAll(previous, destroy);
        }
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
    void advanceRetirements(Destroy destroy) {
        for (auto it = retired_.begin(); it != retired_.end();) {
            if (it->frames_remaining > 0) --it->frames_remaining;
            if (it->frames_remaining == 0) {
                destroyAll(it->resources, destroy);
                it = retired_.erase(it);
            } else {
                ++it;
            }
        }
    }

    template <typename Destroy>
    void clear(Destroy destroy) {
        discardStaged(destroy);
        destroyAll(active_, destroy);
        active_.clear();
        for (RetiredGeneration& generation : retired_) {
            destroyAll(generation.resources, destroy);
        }
        retired_.clear();
    }

private:
    struct RetiredGeneration {
        std::vector<Resource> resources;
        std::uint32_t frames_remaining = 0;
    };

    template <typename Destroy>
    static void destroyAll(std::vector<Resource>& resources, Destroy destroy) {
        for (Resource& resource : resources) destroy(resource);
    }

    std::vector<Resource> active_;
    std::optional<std::vector<Resource>> staged_;
    std::vector<RetiredGeneration> retired_;
    std::uint64_t generation_ = 0;
};

} // namespace pr::gameplay::world3d::aquarium::rendering
