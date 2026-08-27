#include "gameplay/world3d/effects/ProceduralWaterParticleSystem.hpp"

#include "core/config/Json.hpp"
#include "gameplay/world3d/data/RtpksTilePackageLoader.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <unordered_set>

namespace pr::gameplay::world3d::effects {

namespace {

double numberOr(const JsonValue* value, double fallback) {
    return value && value->isNumber() ? value->asNumber() : fallback;
}

bool boolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

void readRange(const JsonValue* value, float& low, float& high) {
    if (!value || !value->isArray() || value->asArray().empty()) return;
    const auto& array = value->asArray();
    if (array[0].isNumber()) low = static_cast<float>(array[0].asNumber());
    high = array.size() > 1 && array[1].isNumber()
        ? static_cast<float>(array[1].asNumber())
        : low;
    if (low > high) std::swap(low, high);
}

void readRange(const JsonValue* value, int& low, int& high) {
    float lo = static_cast<float>(low);
    float hi = static_cast<float>(high);
    readRange(value, lo, hi);
    low = static_cast<int>(std::lround(lo));
    high = static_cast<int>(std::lround(hi));
}

void applyEmitter(ProceduralEmitterConfig& out, const JsonValue* value) {
    if (!value || !value->isObject()) return;
    out.enabled = boolOr(value->get("enabled"), out.enabled);
    readRange(value->get("count"), out.count_min, out.count_max);
    readRange(value->get("lifetimeSeconds"), out.lifetime_min, out.lifetime_max);
    readRange(value->get("sizePixels"), out.size_min_px, out.size_max_px);
    out.spawn_radius_px = static_cast<float>(numberOr(value->get("spawnRadiusPixels"), out.spawn_radius_px));
    out.spawn_height_px = static_cast<float>(numberOr(value->get("spawnHeightPixels"), out.spawn_height_px));
    readRange(value->get("horizontalSpeedPixelsPerSecond"), out.horizontal_speed_min_px, out.horizontal_speed_max_px);
    readRange(value->get("verticalSpeedPixelsPerSecond"), out.vertical_speed_min_px, out.vertical_speed_max_px);
    out.gravity_px = static_cast<float>(numberOr(value->get("gravityPixelsPerSecondSquared"), out.gravity_px));
    out.drag = static_cast<float>(numberOr(value->get("dragPerSecond"), out.drag));
    out.cooldown_seconds = static_cast<float>(numberOr(value->get("cooldownSeconds"), out.cooldown_seconds));
    if (const JsonValue* palette = value->get("palette"); palette && palette->isArray()) {
        out.palette.clear();
        for (const JsonValue& entry : palette->asArray()) {
            if (!entry.isArray() || entry.asArray().size() < 3) continue;
            const auto& c = entry.asArray();
            ParticleColor color{};
            color.r = static_cast<std::uint8_t>(std::clamp(static_cast<int>(numberOr(&c[0], 255)), 0, 255));
            color.g = static_cast<std::uint8_t>(std::clamp(static_cast<int>(numberOr(&c[1], 255)), 0, 255));
            color.b = static_cast<std::uint8_t>(std::clamp(static_cast<int>(numberOr(&c[2], 255)), 0, 255));
            color.a = c.size() > 3
                ? static_cast<std::uint8_t>(std::clamp(static_cast<int>(numberOr(&c[3], 255)), 0, 255))
                : 255;
            out.palette.push_back(color);
        }
    }
    out.count_min = std::max(0, out.count_min);
    out.count_max = std::max(out.count_min, out.count_max);
    out.lifetime_min = std::max(0.01f, out.lifetime_min);
    out.lifetime_max = std::max(out.lifetime_min, out.lifetime_max);
    out.size_min_px = std::max(1, out.size_min_px);
    out.size_max_px = std::max(out.size_min_px, out.size_max_px);
    out.cooldown_seconds = std::max(0.0f, out.cooldown_seconds);
}

float randomFloat(std::mt19937& rng, float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(rng);
}

} // namespace

ProceduralWaterParticleConfig loadProceduralWaterParticleConfig(const std::string& project_root) {
    ProceduralWaterParticleConfig out{};
    out.water_enter.palette = {{236, 251, 255, 255}, {115, 205, 238, 255}, {39, 145, 205, 255}};
    out.water_enter.count_min = 14;
    out.water_enter.count_max = 22;
    out.water_enter.spawn_radius_px = 9.0f;
    out.water_enter.horizontal_speed_min_px = 10.0f;
    out.water_enter.horizontal_speed_max_px = 25.0f;
    out.water_enter.vertical_speed_min_px = 16.0f;
    out.water_enter.vertical_speed_max_px = 31.0f;
    out.water_enter.gravity_px = 56.0f;
    out.water_exit = out.water_enter;
    out.water_exit.count_min = 16;
    out.water_exit.count_max = 25;
    out.water_exit.spawn_radius_px = 10.0f;
    out.water_exit.vertical_speed_min_px = 18.0f;
    out.water_exit.vertical_speed_max_px = 34.0f;
    out.enter_wave = out.water_enter;
    out.enter_wave.count_min = 4;
    out.enter_wave.count_max = 8;
    out.enter_wave.vertical_speed_min_px = 4.0f;
    out.enter_wave.vertical_speed_max_px = 11.0f;
    out.wave_crash = out.water_enter;
    out.wave_crash.count_min = 9;
    out.wave_crash.count_max = 15;
    out.wave_crash.spawn_radius_px = 7.0f;
    out.wave_idle = out.enter_wave;
    out.wave_idle.count_min = 2;
    out.wave_idle.count_max = 4;
    out.wave_idle.cooldown_seconds = 0.32f;
    out.wave_idle.palette = {{242, 253, 255, 170}, {171, 229, 244, 155}, {103, 195, 229, 145}};
    out.wave_moving = out.enter_wave;
    out.wave_moving.count_min = 5;
    out.wave_moving.count_max = 8;
    out.wave_moving.cooldown_seconds = 0.14f;
    out.wave_moving.palette = {{248, 255, 255, 255}, {183, 237, 250, 245}, {83, 185, 226, 235}};

    const std::filesystem::path path = std::filesystem::path(project_root) /
        "config/gameplay/world3d/particles.json";
    try {
        const JsonValue root = parseJsonFile(path.string());
        const JsonValue* water = root.get("waterInteraction");
        if (!water || !water->isObject()) return out;
        out.enabled = boolOr(water->get("enabled"), out.enabled);
        out.max_particles = static_cast<int>(numberOr(water->get("maxParticles"), out.max_particles));
        out.simulation_tick_hz = static_cast<float>(numberOr(water->get("simulationTickHz"), out.simulation_tick_hz));
        if (const JsonValue* wave = water->get("waveContact"); wave && wave->isObject()) {
            out.wave_reach_min = static_cast<float>(numberOr(wave->get("minimumShorelineProgress"), out.wave_reach_min));
            out.wave_reach_max = static_cast<float>(numberOr(wave->get("maximumShorelineProgress"), out.wave_reach_max));
            out.wave_advance_fraction = static_cast<float>(numberOr(wave->get("advanceFraction"), out.wave_advance_fraction));
            out.wave_crest_hold_fraction = static_cast<float>(numberOr(wave->get("crestHoldFraction"), out.wave_crest_hold_fraction));
            out.fallback_wave_cycle_seconds = static_cast<float>(numberOr(wave->get("fallbackCycleSeconds"), out.fallback_wave_cycle_seconds));
            out.spawn_north_offset_px = static_cast<float>(numberOr(
                wave->get("spawnNorthOffsetPixels"), out.spawn_north_offset_px));
        }
        if (const JsonValue* emitters = water->get("emitters"); emitters && emitters->isObject()) {
            applyEmitter(out.water_enter, emitters->get("waterEnter"));
            applyEmitter(out.water_exit, emitters->get("waterExit"));
            applyEmitter(out.enter_wave, emitters->get("waveEnter"));
            applyEmitter(out.wave_crash, emitters->get("waveCrash"));
            applyEmitter(out.wave_idle, emitters->get("waveIdle"));
            applyEmitter(out.wave_moving, emitters->get("waveMoving"));
        }
    } catch (...) {
        return out;
    }
    out.max_particles = std::clamp(out.max_particles, 1, 2048);
    out.simulation_tick_hz = std::clamp(out.simulation_tick_hz, 15.0f, 240.0f);
    out.wave_reach_min = std::clamp(out.wave_reach_min, 0.0f, 1.0f);
    out.wave_reach_max = std::clamp(out.wave_reach_max, out.wave_reach_min, 1.0f);
    out.wave_advance_fraction = std::clamp(out.wave_advance_fraction, 0.05f, 0.9f);
    out.wave_crest_hold_fraction = std::clamp(out.wave_crest_hold_fraction, 0.0f, 0.9f - out.wave_advance_fraction);
    return out;
}

ProceduralWaterParticleSystem::ProceduralWaterParticleSystem(
    const std::string& project_root,
    const SceneConfig& scene,
    ProceduralWaterParticleConfig config)
    : scene_(scene), config_(std::move(config)) {
    particles_.resize(static_cast<std::size_t>(std::max(1, config_.max_particles)));
    if (!scene_.tile_package.path.empty()) {
        std::vector<int> used_tile_ids;
        std::unordered_set<int> unique_tile_ids;
        for (const TileLayerConfig& layer : scene_.tile_layers.layers) {
            if (!layer.visible) continue;
            for (const auto& row : layer.cells) {
                for (const int tile_id : row) {
                    if (tile_id >= 0 && unique_tile_ids.insert(tile_id).second) {
                        used_tile_ids.push_back(tile_id);
                    }
                }
            }
        }
        std::string error;
        const data::RtpksTilePackage package = data::loadRtpksTileMetadataForTiles(
            scene_.tile_package.path, used_tile_ids, &error);
        std::unordered_set<int> used_material_ids;
        for (const TileLayerConfig& layer : scene_.tile_layers.layers) {
            if (!layer.visible) continue;
            for (const auto& row : layer.cells) {
                for (const int tile_id : row) {
                    if (tile_id < 0) continue;
                    const data::RtpksTileMesh* tile = package.tileById(tile_id);
                    if (!tile) continue;
                    for (const data::RtpksMaterialRange& range : tile->material_ranges) {
                        used_material_ids.insert(range.material_id);
                    }
                }
            }
        }
        int cycle_frames = 0;
        float timebase_hz = 0.0f;
        for (const data::RtpksMaterial& material : package.materials) {
            if (used_material_ids.find(material.material_id) == used_material_ids.end() ||
                material.layer_role.rfind("shoreline", 0) != 0) continue;
            cycle_frames = std::max(cycle_frames, material.animation_frame_count);
            if (timebase_hz <= 0.0f && material.animation_timebase_hz > 0.0f) {
                timebase_hz = material.animation_timebase_hz;
            }
        }
        if (cycle_frames > 0 && timebase_hz > 0.0f) {
            authored_wave_cycle_seconds_ = static_cast<float>(cycle_frames) / timebase_hz;
        }
    }
}

float ProceduralWaterParticleSystem::sourcePixelsToWorld(float pixels) const {
    return pixels * (std::max(1.0f, scene_.grid.tile_size) /
        static_cast<float>(std::max(1, scene_.pixel_scale.map_pixels_per_tile)));
}

float ProceduralWaterParticleSystem::currentWaveReach() const {
    const float authored_cycle = authored_wave_cycle_seconds_ > 0.0f
        ? authored_wave_cycle_seconds_
        : std::max(0.1f, config_.fallback_wave_cycle_seconds);
    const float environment_speed = std::max(0.001f, scene_.environment_animation_speed);
    const float wave_speed = std::max(0.001f, scene_.water_wave_speed);
    const double motion_seconds = static_cast<double>(authored_cycle / wave_speed) /
        static_cast<double>(environment_speed);
    const double wait_seconds = static_cast<double>(std::max(0.0f, scene_.water_wave_wait_seconds)) /
        static_cast<double>(environment_speed);
    const double total_seconds = motion_seconds + wait_seconds;
    const double clock_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    double phase_seconds = std::fmod(clock_seconds, std::max(0.001, total_seconds));
    if (phase_seconds >= motion_seconds) return config_.wave_reach_min;
    const float phase = static_cast<float>(phase_seconds / motion_seconds);
    const float advance_end = config_.wave_advance_fraction;
    const float hold_end = advance_end + config_.wave_crest_hold_fraction;
    float amount = 0.0f;
    if (phase < advance_end) amount = phase / advance_end;
    else if (phase < hold_end) amount = 1.0f;
    else amount = 1.0f - ((phase - hold_end) / std::max(0.001f, 1.0f - hold_end));
    return config_.wave_reach_min +
        ((config_.wave_reach_max - config_.wave_reach_min) * std::clamp(amount, 0.0f, 1.0f));
}

void ProceduralWaterParticleSystem::update(
    double dt,
    const std::vector<WaterParticleAgentObservation>& agents) {
    if (!config_.enabled) return;
    dt = std::clamp(dt, 0.0, 0.1);
    const float wave_reach = currentWaveReach();
    const float tile_size = std::max(1.0f, scene_.grid.tile_size);

    for (const WaterParticleAgentObservation& agent : agents) {
        if (agent.id.empty()) continue;
        AgentState& state = agent_states_[agent.id];
        state.cooldown_water_enter = std::max(0.0, state.cooldown_water_enter - dt);
        state.cooldown_water_exit = std::max(0.0, state.cooldown_water_exit - dt);
        state.cooldown_enter_wave = std::max(0.0, state.cooldown_enter_wave - dt);
        state.cooldown_wave_crash = std::max(0.0, state.cooldown_wave_crash - dt);
        state.cooldown_wave_sustain = std::max(0.0, state.cooldown_wave_sustain - dt);
        const int tx = static_cast<int>(std::floor(agent.world_pos.x / tile_size));
        const int ty = static_cast<int>(std::floor(agent.world_pos.z / tile_size));
        const float shore_progress = terrain::shorelineProgressAtWorldPosition(
            scene_, agent.world_pos.x, agent.world_pos.z, tx, ty);
        const bool shoreline = shore_progress >= 0.0f;
        const bool inside_wave = shoreline && shore_progress >= config_.wave_reach_min &&
            shore_progress <= wave_reach;
        const bool in_wave_zone = shoreline && shore_progress >= config_.wave_reach_min &&
            shore_progress <= config_.wave_reach_max;

        if (state.initialized) {
            if (!state.actual_water && agent.actual_water && state.cooldown_water_enter <= 0.0) {
                emit(config_.water_enter, agent.world_pos);
                state.cooldown_water_enter = config_.water_enter.cooldown_seconds;
            }
            if (state.actual_water && !agent.actual_water && state.cooldown_water_exit <= 0.0) {
                emit(config_.water_exit, agent.world_pos);
                state.cooldown_water_exit = config_.water_exit.cooldown_seconds;
            }
            if (!state.inside_wave && inside_wave) {
                const bool actor_crossed_into_wave = agent.moving &&
                    (!state.shoreline || shore_progress > state.shoreline_progress + 0.015f);
                if (actor_crossed_into_wave && state.cooldown_enter_wave <= 0.0) {
                    emit(config_.enter_wave, agent.world_pos);
                    state.cooldown_enter_wave = config_.enter_wave.cooldown_seconds;
                } else if (!actor_crossed_into_wave && state.cooldown_wave_crash <= 0.0) {
                    emit(config_.wave_crash, agent.world_pos);
                }
                // The initial burst and sustained contact are separate. Delay
                // the first sustained burst so entering a wave does not double
                // emit on the following frame.
                state.cooldown_wave_crash = config_.wave_crash.cooldown_seconds;
            }
            if (in_wave_zone && state.cooldown_wave_sustain <= 0.0) {
                const ProceduralEmitterConfig& sustain = agent.moving
                    ? config_.wave_moving
                    : config_.wave_idle;
                emit(sustain, agent.world_pos);
                state.cooldown_wave_sustain = sustain.cooldown_seconds;
            }
        }
        state.initialized = true;
        state.actual_water = agent.actual_water;
        state.inside_wave = inside_wave;
        state.shoreline = shoreline;
        state.shoreline_progress = shore_progress;
        state.world_pos = agent.world_pos;
    }

    const double fixed_dt = 1.0 / static_cast<double>(config_.simulation_tick_hz);
    simulation_accumulator_ = std::min(simulation_accumulator_ + dt, fixed_dt * 4.0);
    while (simulation_accumulator_ >= fixed_dt) {
        simulate(static_cast<float>(fixed_dt));
        simulation_accumulator_ -= fixed_dt;
    }
}

ProceduralWaterParticleSystem::Particle& ProceduralWaterParticleSystem::acquireParticle() {
    for (Particle& particle : particles_) {
        if (!particle.active) return particle;
    }
    return *std::max_element(particles_.begin(), particles_.end(), [](const Particle& a, const Particle& b) {
        return a.age < b.age;
    });
}

void ProceduralWaterParticleSystem::emit(
    const ProceduralEmitterConfig& emitter,
    const camera::Vec3& position) {
    if (!emitter.enabled || emitter.count_max <= 0 || emitter.palette.empty()) return;
    const int count = std::uniform_int_distribution<int>(emitter.count_min, emitter.count_max)(rng_);
    constexpr float kTau = 6.28318530718f;
    camera::Vec3 emitter_position = position;
    emitter_position.z -= sourcePixelsToWorld(config_.spawn_north_offset_px);
    for (int i = 0; i < count; ++i) {
        Particle& particle = acquireParticle();
        const float angle = randomFloat(rng_, 0.0f, kTau);
        const float radius = sourcePixelsToWorld(randomFloat(rng_, 0.0f, emitter.spawn_radius_px));
        const float horizontal_speed = sourcePixelsToWorld(randomFloat(
            rng_, emitter.horizontal_speed_min_px, emitter.horizontal_speed_max_px));
        particle = Particle{};
        particle.active = true;
        particle.position = camera::Vec3{
            emitter_position.x + std::cos(angle) * radius,
            emitter_position.y + sourcePixelsToWorld(emitter.spawn_height_px),
            emitter_position.z + std::sin(angle) * radius};
        particle.velocity = camera::Vec3{
            std::cos(angle) * horizontal_speed,
            sourcePixelsToWorld(randomFloat(rng_, emitter.vertical_speed_min_px, emitter.vertical_speed_max_px)),
            std::sin(angle) * horizontal_speed};
        particle.lifetime = randomFloat(rng_, emitter.lifetime_min, emitter.lifetime_max);
        particle.gravity_world = sourcePixelsToWorld(emitter.gravity_px);
        particle.drag = std::max(0.0f, emitter.drag);
        particle.size_px = std::uniform_int_distribution<int>(emitter.size_min_px, emitter.size_max_px)(rng_);
        particle.color = emitter.palette[std::uniform_int_distribution<std::size_t>(0, emitter.palette.size() - 1)(rng_)];
    }
}

void ProceduralWaterParticleSystem::simulate(float dt) {
    for (Particle& particle : particles_) {
        if (!particle.active) continue;
        particle.age += dt;
        if (particle.age >= particle.lifetime) {
            particle.active = false;
            continue;
        }
        const float damping = std::max(0.0f, 1.0f - particle.drag * dt);
        particle.velocity.x *= damping;
        particle.velocity.z *= damping;
        particle.velocity.y -= particle.gravity_world * dt;
        particle.position.x += particle.velocity.x * dt;
        particle.position.y += particle.velocity.y * dt;
        particle.position.z += particle.velocity.z * dt;
    }
}

void ProceduralWaterParticleSystem::render(
    SDL_Renderer* renderer,
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h) const {
    if (!renderer || !config_.enabled) return;
    for (const Particle& particle : particles_) {
        if (!particle.active) continue;
        float sx = 0.0f, sy = 0.0f, depth = 0.0f;
        if (!camera.worldToScreen(particle.position, viewport_w, viewport_h, sx, sy, depth)) continue;
        const float life = std::clamp(1.0f - particle.age / particle.lifetime, 0.0f, 1.0f);
        const int size = std::max(1, particle.size_px);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, particle.color.r, particle.color.g, particle.color.b,
            static_cast<Uint8>(static_cast<float>(particle.color.a) * life));
        SDL_Rect rect{
            static_cast<int>(std::lround(sx)) - size / 2,
            static_cast<int>(std::lround(sy)) - size / 2,
            size,
            size};
        SDL_RenderFillRect(renderer, &rect);
    }
}

void ProceduralWaterParticleSystem::collectTextureBillboardDraws(
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h,
    std::vector<rendering::TextureBillboardDraw>& out) const {
    if (!config_.enabled) return;
    const float nominal_distance = std::max(1.0f, scene_.camera_distance);
    for (const Particle& particle : particles_) {
        if (!particle.active) continue;
        float sx = 0.0f, sy = 0.0f, depth = 0.0f;
        if (!camera.worldToScreen(particle.position, viewport_w, viewport_h, sx, sy, depth)) continue;
        rendering::TextureBillboardDraw draw{};
        draw.texture_cache_key = "__procedural_white_pixel";
        draw.source_rect = SDL_Rect{0, 0, 1, 1};
        draw.tint_r = static_cast<float>(particle.color.r) / 255.0f;
        draw.tint_g = static_cast<float>(particle.color.g) / 255.0f;
        draw.tint_b = static_cast<float>(particle.color.b) / 255.0f;
        draw.alpha_multiplier = (static_cast<float>(particle.color.a) / 255.0f) *
            std::clamp(1.0f - particle.age / particle.lifetime, 0.0f, 1.0f);
        draw.placement.anchor = particle.position;
        draw.placement.feet = particle.position;
        draw.placement.shadow_ground = particle.position;
        draw.placement.depth = depth;
        draw.placement.world_h = static_cast<float>(particle.size_px) * depth / nominal_distance;
        draw.placement.world_w = draw.placement.world_h;
        draw.placement.visible = true;
        out.push_back(std::move(draw));
    }
}

} // namespace pr::gameplay::world3d::effects
