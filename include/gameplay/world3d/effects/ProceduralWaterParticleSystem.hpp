#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"

#include <SDL.h>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr::gameplay::world3d::effects {

struct ParticleColor {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct ProceduralEmitterConfig {
    bool enabled = true;
    int count_min = 5;
    int count_max = 9;
    float lifetime_min = 0.24f;
    float lifetime_max = 0.48f;
    int size_min_px = 1;
    int size_max_px = 2;
    float spawn_radius_px = 5.0f;
    float spawn_height_px = 1.0f;
    float horizontal_speed_min_px = 5.0f;
    float horizontal_speed_max_px = 15.0f;
    float vertical_speed_min_px = 8.0f;
    float vertical_speed_max_px = 18.0f;
    float gravity_px = 42.0f;
    float drag = 0.0f;
    float cooldown_seconds = 0.15f;
    std::vector<ParticleColor> palette;
};

struct ProceduralWaterParticleConfig {
    bool enabled = true;
    int max_particles = 192;
    float simulation_tick_hz = 60.0f;
    // Portion of a shoreline ramp covered at the wave's low/high points.
    float wave_reach_min = 0.42f;
    float wave_reach_max = 0.92f;
    // Normalized cycle layout: advance, optional crest hold, then retreat.
    float wave_advance_fraction = 0.42f;
    float wave_crest_hold_fraction = 0.08f;
    float fallback_wave_cycle_seconds = 2.0f;
    float spawn_north_offset_px = 2.0f;
    ProceduralEmitterConfig water_enter;
    ProceduralEmitterConfig water_exit;
    ProceduralEmitterConfig enter_wave;
    ProceduralEmitterConfig wave_crash;
    ProceduralEmitterConfig wave_idle;
    ProceduralEmitterConfig wave_moving;
};

ProceduralWaterParticleConfig loadProceduralWaterParticleConfig(
    const std::string& project_root);

struct WaterParticleAgentObservation {
    std::string id;
    camera::Vec3 world_pos{};
    bool actual_water = false;
    bool moving = false;
};

class ProceduralWaterParticleSystem {
public:
    ProceduralWaterParticleSystem(
        const std::string& project_root,
        const SceneConfig& scene,
        ProceduralWaterParticleConfig config);

    void update(double dt, const std::vector<WaterParticleAgentObservation>& agents);
    void render(
        SDL_Renderer* renderer,
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h) const;
    void collectTextureBillboardDraws(
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h,
        std::vector<rendering::TextureBillboardDraw>& out) const;

private:
    struct Particle {
        bool active = false;
        camera::Vec3 position{};
        camera::Vec3 velocity{};
        float age = 0.0f;
        float lifetime = 0.3f;
        float gravity_world = 0.0f;
        float drag = 0.0f;
        int size_px = 1;
        ParticleColor color{};
    };

    struct AgentState {
        bool initialized = false;
        bool actual_water = false;
        bool inside_wave = false;
        bool shoreline = false;
        float shoreline_progress = -1.0f;
        camera::Vec3 world_pos{};
        double cooldown_water_enter = 0.0;
        double cooldown_water_exit = 0.0;
        double cooldown_enter_wave = 0.0;
        double cooldown_wave_crash = 0.0;
        double cooldown_wave_sustain = 0.0;
    };

    void simulate(float dt);
    void emit(const ProceduralEmitterConfig& emitter, const camera::Vec3& position);
    float currentWaveReach() const;
    float sourcePixelsToWorld(float pixels) const;
    Particle& acquireParticle();

    const SceneConfig& scene_;
    ProceduralWaterParticleConfig config_{};
    std::vector<Particle> particles_;
    std::unordered_map<std::string, AgentState> agent_states_;
    std::mt19937 rng_{0x57415645U};
    double simulation_accumulator_ = 0.0;
    float authored_wave_cycle_seconds_ = 0.0f;
};

} // namespace pr::gameplay::world3d::effects
