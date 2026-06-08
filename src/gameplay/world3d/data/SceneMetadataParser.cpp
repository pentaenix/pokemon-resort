#include "gameplay/world3d/data/SceneMetadataParser.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace pr::gameplay::world3d::data {

namespace fs = std::filesystem;

namespace {

std::string strOr(const JsonValue* v, const std::string& fallback) {
    return (v && v->isString()) ? v->asString() : fallback;
}

double numOr(const JsonValue* v, double fallback) {
    return (v && v->isNumber()) ? v->asNumber() : fallback;
}

int intOr(const JsonValue* v, int fallback) {
    return (v && v->isNumber()) ? static_cast<int>(v->asNumber()) : fallback;
}

bool boolOr(const JsonValue* v, bool fallback) {
    return (v && v->isBool()) ? v->asBool() : fallback;
}

void applyColor(TerrainColor& out, const JsonValue* color) {
    if (!color || !color->isArray()) return;
    const auto& arr = color->asArray();
    if (arr.size() > 0 && arr[0].isNumber()) out.r = static_cast<std::uint8_t>(std::clamp(intOr(&arr[0], out.r), 0, 255));
    if (arr.size() > 1 && arr[1].isNumber()) out.g = static_cast<std::uint8_t>(std::clamp(intOr(&arr[1], out.g), 0, 255));
    if (arr.size() > 2 && arr[2].isNumber()) out.b = static_cast<std::uint8_t>(std::clamp(intOr(&arr[2], out.b), 0, 255));
    if (arr.size() > 3 && arr[3].isNumber()) out.a = static_cast<std::uint8_t>(std::clamp(intOr(&arr[3], out.a), 0, 255));
}

FacingDirection parseFacing(const std::string& value) {
    if (value == "north") return FacingDirection::North;
    if (value == "west") return FacingDirection::West;
    if (value == "east") return FacingDirection::East;
    return FacingDirection::South;
}

NpcMovementProfile parseMovementProfile(const std::string& value) {
    if (value == "random_move") return NpcMovementProfile::RandomMove;
    if (value == "line_move") return NpcMovementProfile::LineMove;
    if (value == "rotate_in_place") return NpcMovementProfile::RotateInPlace;
    return NpcMovementProfile::StandStill;
}

std::string resolvePath(const std::string& root, const std::string& raw) {
    const fs::path p(raw);
    if (p.is_absolute()) return raw;
    return (fs::path(root) / p).string();
}

void applyGridConfig(GridConfig& out, const JsonValue* grid) {
    if (!grid || !grid->isObject()) return;
    out.enabled = grid->get("enabled") ? grid->get("enabled")->asBool() : out.enabled;
    out.tile_size = static_cast<float>(numOr(grid->get("tileSize"), out.tile_size));
    out.width = intOr(grid->get("width"), out.width);
    out.height = intOr(grid->get("height"), out.height);
}

void applyPlayerConfig(PlayerSpawnConfig& out, const JsonValue* player, const std::string& project_root) {
    if (!player || !player->isObject()) return;
    const std::string character = strOr(player->get("character"), "");
    if (!character.empty()) {
        out.character_path = resolvePath(project_root, character);
    }
    if (const JsonValue* tile = player->get("spawnTile"); tile && tile->isArray()) {
        const auto& arr = tile->asArray();
        if (arr.size() > 0 && arr[0].isNumber()) out.spawn_tile_x = static_cast<int>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) out.spawn_tile_y = static_cast<int>(arr[1].asNumber());
    }
    out.spawn_height = static_cast<float>(numOr(player->get("spawnHeight"), out.spawn_height));
    out.facing = parseFacing(strOr(player->get("facing"), "south"));
}

void applyCameraConfig(SceneConfig& out, const JsonValue* camera) {
    if (!camera || !camera->isObject()) return;
    out.camera_preset = strOr(camera->get("preset"), out.camera_preset);
    out.camera_distance = static_cast<float>(numOr(camera->get("distance"), out.camera_distance));
    out.camera_pitch_deg = static_cast<float>(numOr(camera->get("pitchDeg"), out.camera_pitch_deg));
    out.camera_yaw_deg = static_cast<float>(numOr(camera->get("yawDeg"), out.camera_yaw_deg));
    out.camera_roll_deg = static_cast<float>(numOr(camera->get("rollDeg"), out.camera_roll_deg));
    out.camera_near_clip = static_cast<float>(numOr(camera->get("nearClip"), out.camera_near_clip));
    out.camera_far_clip = static_cast<float>(numOr(camera->get("farClip"), out.camera_far_clip));
    out.camera_fov_y_deg = static_cast<float>(numOr(camera->get("fovYDeg"), out.camera_fov_y_deg));
    if (const JsonValue* aspect = camera->get("aspect"); aspect && aspect->isArray()) {
        const auto& arr = aspect->asArray();
        if (arr.size() > 0 && arr[0].isNumber()) out.camera_aspect_width = static_cast<float>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) out.camera_aspect_height = static_cast<float>(arr[1].asNumber());
    }
}

void applyFreecamConfig(SceneConfig& out, const JsonValue* freecam) {
    if (!freecam || !freecam->isObject()) return;
    out.freecam_move_speed = static_cast<float>(numOr(freecam->get("moveSpeed"), out.freecam_move_speed));
    out.freecam_mouse_sensitivity =
        static_cast<float>(numOr(freecam->get("mouseSensitivity"), out.freecam_mouse_sensitivity));
    if (const JsonValue* offset = freecam->get("initialOffset"); offset && offset->isArray()) {
        const auto& arr = offset->asArray();
        if (arr.size() > 0 && arr[0].isNumber()) out.freecam_initial_offset_x = static_cast<float>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) out.freecam_initial_offset_y = static_cast<float>(arr[1].asNumber());
        if (arr.size() > 2 && arr[2].isNumber()) out.freecam_initial_offset_z = static_cast<float>(arr[2].asNumber());
    }
    out.freecam_initial_yaw_deg =
        static_cast<float>(numOr(freecam->get("initialYawDeg"), out.freecam_initial_yaw_deg));
    out.freecam_initial_pitch_deg =
        static_cast<float>(numOr(freecam->get("initialPitchDeg"), out.freecam_initial_pitch_deg));
    out.freecam_pitch_min_deg =
        static_cast<float>(numOr(freecam->get("pitchClampMinDeg"), out.freecam_pitch_min_deg));
    out.freecam_pitch_max_deg =
        static_cast<float>(numOr(freecam->get("pitchClampMaxDeg"), out.freecam_pitch_max_deg));
}

void applyLightingConfig(SceneConfig& out, const JsonValue* lighting) {
    if (!lighting || !lighting->isObject()) return;
    out.lighting_preset = strOr(lighting->get("preset"), out.lighting_preset);
    out.lighting_brightness = static_cast<float>(numOr(lighting->get("brightness"), out.lighting_brightness));
    if (const JsonValue* tint = lighting->get("tint"); tint && tint->isArray()) {
        const auto& arr = tint->asArray();
        if (arr.size() > 0 && arr[0].isNumber()) out.lighting_tint_r = static_cast<float>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) out.lighting_tint_g = static_cast<float>(arr[1].asNumber());
        if (arr.size() > 2 && arr[2].isNumber()) out.lighting_tint_b = static_cast<float>(arr[2].asNumber());
    }
}

void applyTerrainRenderConfig(TerrainConfig& out, const JsonValue* terrain) {
    if (!terrain || !terrain->isObject()) return;
    out.height_per_floor = static_cast<float>(numOr(terrain->get("heightPerFloor"), out.height_per_floor));

    if (const JsonValue* floor = terrain->get("floorColors"); floor && floor->isObject()) {
        applyColor(out.floor_color_a, floor->get("checkerA"));
        applyColor(out.floor_color_b, floor->get("checkerB"));
    }
    applyColor(out.floor_color_a, terrain->get("floorColorA"));
    applyColor(out.floor_color_b, terrain->get("floorColorB"));

    if (const JsonValue* floor_heights = terrain->get("floorHeightColors"); floor_heights && floor_heights->isObject()) {
        out.floor_height_recolor_enabled = boolOr(floor_heights->get("enabled"), out.floor_height_recolor_enabled);
        if (const JsonValue* first = floor_heights->get("firstNonBase"); first && first->isObject()) {
            applyColor(out.first_non_base_floor_color_a, first->get("checkerA"));
            applyColor(out.first_non_base_floor_color_b, first->get("checkerB"));
        }
    }
    out.floor_height_recolor_enabled =
        boolOr(terrain->get("floorHeightRecolorEnabled"), out.floor_height_recolor_enabled);
    applyColor(out.first_non_base_floor_color_a, terrain->get("firstNonBaseFloorColorA"));
    applyColor(out.first_non_base_floor_color_b, terrain->get("firstNonBaseFloorColorB"));

    if (const JsonValue* ramps = terrain->get("rampColors"); ramps && ramps->isObject()) {
        out.ramp_recolor_enabled = boolOr(ramps->get("enabled"), out.ramp_recolor_enabled);
        applyColor(out.ramp_color_a, ramps->get("checkerA"));
        applyColor(out.ramp_color_b, ramps->get("checkerB"));
    }
    out.ramp_recolor_enabled = boolOr(terrain->get("rampRecolorEnabled"), out.ramp_recolor_enabled);
    applyColor(out.ramp_color_a, terrain->get("rampColorA"));
    applyColor(out.ramp_color_b, terrain->get("rampColorB"));

    if (const JsonValue* walls = terrain->get("wallColors"); walls && walls->isObject()) {
        applyColor(out.wall_color_ns, walls->get("northSouth"));
        applyColor(out.wall_color_ew, walls->get("eastWest"));
    }
    applyColor(out.wall_color_ns, terrain->get("wallColorNS"));
    applyColor(out.wall_color_ew, terrain->get("wallColorEW"));
    applyColor(out.wire_color, terrain->get("wireColor"));
}

} // namespace

SceneConfig parseSceneMetadata(
    const JsonValue& root,
    const std::string& project_root) {
    if (!root.isObject()) {
        throw std::runtime_error("Scene config root must be an object");
    }

    SceneConfig out;

    const std::string defaults_path =
        (fs::path(project_root) / "config" / "gameplay" / "world3d" / "defaults.json").string();
    const JsonValue defaults_root = parseJsonFile(defaults_path);
    if (defaults_root.isObject()) {
        if (const JsonValue* scene_defaults = defaults_root.get("sceneDefaults");
            scene_defaults && scene_defaults->isObject()) {
            out.id = strOr(scene_defaults->get("id"), out.id);
            applyGridConfig(out.grid, scene_defaults->get("grid"));
            applyPlayerConfig(out.player, scene_defaults->get("player"), project_root);
            applyCameraConfig(out, scene_defaults->get("camera"));
            applyFreecamConfig(out, scene_defaults->get("freecam"));
            applyLightingConfig(out, scene_defaults->get("lighting"));
        }
    }

    out.id = strOr(root.get("id"), out.id);

    const JsonValue* visual = root.get("visual");
    if (!visual || !visual->isObject()) {
        throw std::runtime_error("Scene visual section missing");
    }
    out.visual.mesh_path = resolvePath(project_root, strOr(visual->get("mesh"), ""));
    out.visual.material_path = resolvePath(project_root, strOr(visual->get("material"), ""));
    out.visual.texture_directory = resolvePath(project_root, strOr(visual->get("textureDirectory"), ""));
    out.visual.scale = static_cast<float>(numOr(visual->get("scale"), 1.0));
    if (const JsonValue* origin = visual->get("origin"); origin && origin->isArray()) {
        const auto& arr = origin->asArray();
        if (arr.size() > 0 && arr[0].isNumber()) out.visual.origin_x = static_cast<float>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) out.visual.origin_y = static_cast<float>(arr[1].asNumber());
        if (arr.size() > 2 && arr[2].isNumber()) out.visual.origin_z = static_cast<float>(arr[2].asNumber());
    }

    applyGridConfig(out.grid, root.get("grid"));

    const JsonValue* player = root.get("player");
    if (!player || !player->isObject()) {
        throw std::runtime_error("Scene player section missing");
    }
    applyPlayerConfig(out.player, player, project_root);
    applyCameraConfig(out, root.get("camera"));

    const std::string presets_path =
        (fs::path(project_root) / "config" / "gameplay" / "camera" / "presets.json").string();
    const JsonValue camera_root = parseJsonFile(presets_path);
    if (camera_root.isObject()) {
        if (const JsonValue* presets = camera_root.get("presets"); presets && presets->isObject()) {
            if (const JsonValue* preset = presets->get(out.camera_preset); preset && preset->isObject()) {
                out.camera_distance = static_cast<float>(numOr(preset->get("distance"), out.camera_distance));
                out.camera_pitch_deg = static_cast<float>(numOr(preset->get("pitchDeg"), out.camera_pitch_deg));
                out.camera_yaw_deg = static_cast<float>(numOr(preset->get("yawDeg"), out.camera_yaw_deg));
                out.camera_roll_deg = static_cast<float>(numOr(preset->get("rollDeg"), out.camera_roll_deg));
                out.camera_near_clip = static_cast<float>(numOr(preset->get("nearClip"), out.camera_near_clip));
                out.camera_far_clip = static_cast<float>(numOr(preset->get("farClip"), out.camera_far_clip));
                out.camera_fov_y_deg = static_cast<float>(numOr(preset->get("fovYDeg"), out.camera_fov_y_deg));
                if (const JsonValue* aspect = preset->get("aspect"); aspect && aspect->isArray()) {
                    const auto& arr = aspect->asArray();
                    if (arr.size() > 0 && arr[0].isNumber()) out.camera_aspect_width = static_cast<float>(arr[0].asNumber());
                    if (arr.size() > 1 && arr[1].isNumber()) out.camera_aspect_height = static_cast<float>(arr[1].asNumber());
                }
            }
        }
    }

    const std::string sprite_fx_path =
        (fs::path(project_root) / "config" / "gameplay" / "sprites" / "shadow.json").string();
    const JsonValue sprite_fx_root = parseJsonFile(sprite_fx_path);
    if (sprite_fx_root.isObject()) {
        if (const JsonValue* shadow = sprite_fx_root.get("shadow"); shadow && shadow->isObject()) {
            out.sprite_shadow.enabled = shadow->get("enabled") ? shadow->get("enabled")->asBool() : out.sprite_shadow.enabled;
            out.sprite_shadow.opacity = static_cast<float>(numOr(shadow->get("opacity"), out.sprite_shadow.opacity));
            out.sprite_shadow.pixel_coherent = shadow->get("pixelCoherent")
                ? shadow->get("pixelCoherent")->asBool()
                : out.sprite_shadow.pixel_coherent;
            out.sprite_shadow.feet_to_shadow_bottom_px =
                intOr(shadow->get("feetToShadowBottomPx"), out.sprite_shadow.feet_to_shadow_bottom_px);
            if (const JsonValue* screen_offset = shadow->get("screenOffsetPx"); screen_offset && screen_offset->isArray()) {
                const auto& arr = screen_offset->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) out.sprite_shadow.screen_offset_x_px = static_cast<int>(arr[0].asNumber());
                if (arr.size() > 1 && arr[1].isNumber()) out.sprite_shadow.screen_offset_y_px = static_cast<int>(arr[1].asNumber());
            }
            if (const JsonValue* color = shadow->get("colorRgba"); color && color->isArray()) {
                const auto& arr = color->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) out.sprite_shadow.color_r = static_cast<std::uint8_t>(std::clamp(intOr(&arr[0], out.sprite_shadow.color_r), 0, 255));
                if (arr.size() > 1 && arr[1].isNumber()) out.sprite_shadow.color_g = static_cast<std::uint8_t>(std::clamp(intOr(&arr[1], out.sprite_shadow.color_g), 0, 255));
                if (arr.size() > 2 && arr[2].isNumber()) out.sprite_shadow.color_b = static_cast<std::uint8_t>(std::clamp(intOr(&arr[2], out.sprite_shadow.color_b), 0, 255));
                if (arr.size() > 3 && arr[3].isNumber()) out.sprite_shadow.color_a = static_cast<std::uint8_t>(std::clamp(intOr(&arr[3], out.sprite_shadow.color_a), 0, 255));
            }
            out.sprite_shadow.radius_x_tiles = static_cast<float>(numOr(shadow->get("radiusXTiles"), out.sprite_shadow.radius_x_tiles));
            out.sprite_shadow.radius_z_tiles = static_cast<float>(numOr(shadow->get("radiusZTiles"), out.sprite_shadow.radius_z_tiles));
            out.sprite_shadow.world_y_lift = static_cast<float>(numOr(shadow->get("worldYLift"), out.sprite_shadow.world_y_lift));
            out.sprite_shadow.texture_width_px = intOr(shadow->get("textureWidthPx"), out.sprite_shadow.texture_width_px);
            out.sprite_shadow.texture_height_px = intOr(shadow->get("textureHeightPx"), out.sprite_shadow.texture_height_px);
            out.sprite_shadow.mask_rows.clear();
            if (const JsonValue* mask = shadow->get("mask"); mask && mask->isArray()) {
                for (const JsonValue& row : mask->asArray()) {
                    if (row.isString()) {
                        out.sprite_shadow.mask_rows.push_back(row.asString());
                    }
                }
            }

            out.sprite_shadow.opacity = std::clamp(out.sprite_shadow.opacity, 0.0f, 1.0f);
            out.sprite_shadow.feet_to_shadow_bottom_px = std::clamp(out.sprite_shadow.feet_to_shadow_bottom_px, -32, 64);
            out.sprite_shadow.radius_x_tiles = std::max(0.01f, out.sprite_shadow.radius_x_tiles);
            out.sprite_shadow.radius_z_tiles = std::max(0.01f, out.sprite_shadow.radius_z_tiles);
            out.sprite_shadow.texture_width_px = std::clamp(out.sprite_shadow.texture_width_px, 8, 256);
            out.sprite_shadow.texture_height_px = std::clamp(out.sprite_shadow.texture_height_px, 8, 256);
        }
    }

    const std::string render_cfg_path =
        (fs::path(project_root) / "config" / "gameplay" / "world3d" / "render.json").string();
    const JsonValue render_root = parseJsonFile(render_cfg_path);
    if (render_root.isObject()) {
        applyTerrainRenderConfig(out.terrain, render_root.get("terrain"));
        if (const JsonValue* occ = render_root.get("occlusion"); occ && occ->isObject()) {
            out.model_behind_bias_tiles =
                static_cast<float>(numOr(occ->get("modelBehindBiasTiles"), out.model_behind_bias_tiles));
        }
        if (const JsonValue* billboard = render_root.get("billboard"); billboard && billboard->isObject()) {
            if (const JsonValue* tile_offset = billboard->get("tileAnchorOffsetTiles");
                tile_offset && tile_offset->isObject()) {
                out.billboard_tile_anchor_forward = static_cast<float>(
                    numOr(tile_offset->get("forward"), out.billboard_tile_anchor_forward));
                out.billboard_tile_anchor_right = static_cast<float>(
                    numOr(tile_offset->get("right"), out.billboard_tile_anchor_right));
            }
        }
    }

    applyLightingConfig(out, root.get("lighting"));

    if (const JsonValue* models = root.get("models"); models && models->isArray()) {
        out.models.clear();
        for (const JsonValue& model : models->asArray()) {
            if (!model.isObject()) continue;
            ModelPlacementConfig item;
            item.id = strOr(model.get("id"), "");
            // Prefer "glb"; fall back to "mesh" for older metadata that referenced the asset there.
            std::string glb_rel = strOr(model.get("glb"), "");
            if (glb_rel.empty()) glb_rel = strOr(model.get("mesh"), "");
            item.glb_path = glb_rel.empty() ? "" : resolvePath(project_root, glb_rel);
            if (const JsonValue* pos = model.get("position"); pos && pos->isArray()) {
                const auto& arr = pos->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) item.x = static_cast<float>(arr[0].asNumber());
                if (arr.size() > 1 && arr[1].isNumber()) item.y = static_cast<float>(arr[1].asNumber());
                if (arr.size() > 2 && arr[2].isNumber()) item.z = static_cast<float>(arr[2].asNumber());
            }
            item.yaw_deg = static_cast<float>(numOr(model.get("yawDeg"), 0.0));
            item.scale = static_cast<float>(numOr(model.get("scale"), 1.0));
            out.models.push_back(std::move(item));
        }
    }

    if (const JsonValue* chars = root.get("characters"); chars && chars->isArray()) {
        out.characters.clear();
        for (const JsonValue& character : chars->asArray()) {
            if (!character.isObject()) continue;
            NpcConfig npc;
            npc.id = strOr(character.get("id"), "");
            npc.character_id = strOr(character.get("characterId"), "");
            if (const JsonValue* tile = character.get("tile"); tile && tile->isArray()) {
                const auto& arr = tile->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) npc.tile_x = static_cast<int>(arr[0].asNumber());
                if (arr.size() > 1 && arr[1].isNumber()) npc.tile_y = static_cast<int>(arr[1].asNumber());
            }
            npc.facing = parseFacing(strOr(character.get("facing"), "south"));
            npc.movement_profile = parseMovementProfile(strOr(character.get("movementProfile"), "stand_still"));
            npc.line_distance = intOr(character.get("lineDistance"), 1);
            out.characters.push_back(std::move(npc));
        }
    }

    return out;
}

} // namespace pr::gameplay::world3d::data
