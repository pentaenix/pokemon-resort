#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

std::uint32_t packAbgr(float r, float g, float b, float a) {
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (c(a) << 24U) | (c(b) << 16U) | (c(g) << 8U) | c(r);
}

std::uint8_t byteChannel(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool insideRoundedRect(int x, int y, int w, int h, int radius) {
    if (w <= 0 || h <= 0) return false;
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    if (radius <= 0) return x >= 0 && x < w && y >= 0 && y < h;
    const int left = radius;
    const int right = w - radius - 1;
    const int top = radius;
    const int bottom = h - radius - 1;
    if ((x >= left && x <= right) || (y >= top && y <= bottom)) return true;
    const int cx = x < left ? left : right;
    const int cy = y < top ? top : bottom;
    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void setRgba(std::vector<std::uint8_t>& pixels, int w, int x, int y, const Color4& color) {
    const std::size_t offset = static_cast<std::size_t>((y * w + x) * 4);
    pixels[offset + 0] = byteChannel(color.r);
    pixels[offset + 1] = byteChannel(color.g);
    pixels[offset + 2] = byteChannel(color.b);
    pixels[offset + 3] = byteChannel(color.a);
}

Color4 mixColor(Color4 a, Color4 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color4{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t};
}

void blendRgba(
    std::vector<std::uint8_t>& pixels,
    int w,
    int x,
    int y,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a) {
    if (a == 0) return;
    const std::size_t offset = static_cast<std::size_t>((y * w + x) * 4);
    const float src_a = static_cast<float>(a) / 255.0f;
    const float dst_a = static_cast<float>(pixels[offset + 3]) / 255.0f;
    const float out_a = src_a + dst_a * (1.0f - src_a);
    if (out_a <= 0.0f) return;
    pixels[offset + 0] = static_cast<std::uint8_t>(
        (static_cast<float>(r) * src_a + static_cast<float>(pixels[offset + 0]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
    pixels[offset + 1] = static_cast<std::uint8_t>(
        (static_cast<float>(g) * src_a + static_cast<float>(pixels[offset + 1]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
    pixels[offset + 2] = static_cast<std::uint8_t>(
        (static_cast<float>(b) * src_a + static_cast<float>(pixels[offset + 2]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
    pixels[offset + 3] = static_cast<std::uint8_t>(out_a * 255.0f + 0.5f);
}

std::string overlayStyleKey(const AttendOverlayButtonConfig& style, const std::string& label, int w, int h) {
    std::ostringstream out;
    out << label << '|' << w << 'x' << h << '|'
        << style.corner_radius << '|' << style.stroke_width << '|' << style.font_size << '|'
        << style.fill.r << ',' << style.fill.g << ',' << style.fill.b << ',' << style.fill.a << '|'
        << style.stroke.r << ',' << style.stroke.g << ',' << style.stroke.b << ',' << style.stroke.a << '|'
        << style.text.r << ',' << style.text.g << ',' << style.text.b << ',' << style.text.a;
    return out.str();
}

std::string cornerButtonStyleKey(const AttendBgfxCornerButton& button) {
    const AttendCornerButtonConfig& style = button.style;
    std::ostringstream out;
    out << (button.left ? "L" : "R") << (button.top ? "T" : "B") << '|'
        << button.w << 'x' << button.h << '|'
        << style.icon_path << '|'
        << style.icon_scale << '|' << style.icon_offset_x << ',' << style.icon_offset_y << '|'
        << style.side_extension_ratio << '|'
        << style.outer_border.r << ',' << style.outer_border.g << ',' << style.outer_border.b << ',' << style.outer_border.a << '|'
        << style.inner_border.r << ',' << style.inner_border.g << ',' << style.inner_border.b << ',' << style.inner_border.a << '|'
        << style.fill_top.r << ',' << style.fill_top.g << ',' << style.fill_top.b << ',' << style.fill_top.a << '|'
        << style.fill_bottom.r << ',' << style.fill_bottom.g << ',' << style.fill_bottom.b << ',' << style.fill_bottom.a;
    return out.str();
}

bool insideCornerButtonShape(int x, int y, int w, int h, bool left, bool top, int extension) {
    if (w <= 0 || h <= 0 || x < 0 || x >= w || y < 0 || y >= h) return false;
    const int radius = std::max(1, h - 1);
    const int corner_y = top ? 0 : h - 1;
    extension = std::clamp(extension, 0, std::max(0, w - 1));
    const int dy = y - corner_y;
    if (left) {
        const int dx = x - extension;
        return dx * dx + dy * dy <= radius * radius;
    }
    const int local_x = w - 1 - x;
    const int dx = local_x - extension;
    return dx * dx + dy * dy <= radius * radius;
}

int inwardBoundaryDistance(int x, int y, int w, int h, bool left, bool top, int extension, int max_distance) {
    if (!insideCornerButtonShape(x, y, w, h, left, top, extension)) return 0;
    for (int distance = 1; distance <= max_distance; ++distance) {
        const int vertical_y = top ? y + distance : y - distance;
        if (!insideCornerButtonShape(x, vertical_y, w, h, left, top, extension)) return distance;
        const int side_x = left ? x + distance : x - distance;
        if (!insideCornerButtonShape(side_x, y, w, h, left, top, extension)) return distance;
    }
    return max_distance + 1;
}

Color3 mix(Color3 a, Color3 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color3{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t};
}

Color3 sampleGradient(const std::vector<GradientStop>& stops, float at) {
    if (stops.empty()) return Color3{};
    at = std::clamp(at, 0.0f, 1.0f);
    if (at <= stops.front().at) return stops.front().color;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (at <= stops[i].at) {
            const float span = std::max(0.0001f, stops[i].at - stops[i - 1].at);
            return mix(stops[i - 1].color, stops[i].color, (at - stops[i - 1].at) / span);
        }
    }
    return stops.back().color;
}

bool containsAnySubstring(const std::string& value, const std::vector<std::string>& needles) {
    for (const std::string& needle : needles) {
        if (!needle.empty() && value.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string lowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAscii(std::string value, const char* needle) {
    value = lowercaseAscii(std::move(value));
    return value.find(needle) != std::string::npos;
}

int jsonIntOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

bool jsonBoolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

pr::gameplay::world3d::WorldViewportConfig loadSharedWorldViewportConfig(const std::string& project_root) {
    pr::gameplay::world3d::WorldViewportConfig out{};
    const fs::path path = fs::path(project_root) / "config" / "gameplay" / "world3d" / "render.json";
    try {
        const JsonValue root = parseJsonFile(path.string());
        const JsonValue* world_viewport = root.isObject() ? root.get("worldViewport") : nullptr;
        if (world_viewport && world_viewport->isObject()) {
            out.enabled = jsonBoolOr(world_viewport->get("enabled"), out.enabled);
            out.base_width = jsonIntOr(world_viewport->get("baseWidth"), out.base_width);
            out.base_height = jsonIntOr(world_viewport->get("baseHeight"), out.base_height);
            out.internal_scale = jsonIntOr(
                world_viewport->get("upscale"),
                jsonIntOr(world_viewport->get("internalScale"), out.internal_scale));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[AttendBgfx] Could not load shared worldViewport config: "
                  << ex.what() << '\n';
    }
    out.base_width = std::clamp(out.base_width, 160, 1920);
    out.base_height = std::clamp(out.base_height, 120, 1080);
    out.internal_scale = std::clamp(out.internal_scale, 1, 4);
    return out;
}

void normalize3(float& x, float& y, float& z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 0.00001f) {
        x = 0.0f;
        y = 1.0f;
        z = 0.0f;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

void identity(float (&m)[16]) {
    std::fill(std::begin(m), std::end(m), 0.0f);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void placementMatrix(float x, float y, float z, float yaw_deg, float pitch_deg, float scale, float (&m)[16]) {
    identity(m);
    const float yaw = yaw_deg * (kPi / 180.0f);
    const float pitch = pitch_deg * (kPi / 180.0f);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    m[0] = cy * scale;
    m[1] = 0.0f;
    m[2] = -sy * scale;
    m[4] = sy * sp * scale;
    m[5] = cp * scale;
    m[6] = cy * sp * scale;
    m[8] = sy * cp * scale;
    m[9] = -sp * scale;
    m[10] = cy * cp * scale;
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

Vertex transformStaticVertex(
    const pr::gameplay::world3d::data::GlbVertex& src,
    float x,
    float y,
    float z,
    float yaw_deg,
    float scale) {
    const float yaw = yaw_deg * (kPi / 180.0f);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float sx = src.x * scale;
    const float syv = src.y * scale;
    const float sz = src.z * scale;
    return Vertex{
        x + sx * cy + sz * sy,
        y + syv,
        z - sx * sy + sz * cy,
        0.0f,
        1.0f,
        0.0f,
        packAbgr(src.r, src.g, src.b, src.a),
        src.u,
        src.v};
}

Vertex transformAttendVertex(
    const AttendPokemonVertex& src,
    float x,
    float y,
    float z,
    float yaw_deg,
    float scale) {
    const float yaw = yaw_deg * (kPi / 180.0f);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float sx = src.x * scale;
    const float syv = src.y * scale;
    const float sz = src.z * scale;
    float nx = src.nx * cy + src.nz * sy;
    float ny = src.ny;
    float nz = -src.nx * sy + src.nz * cy;
    normalize3(nx, ny, nz);
    return Vertex{
        x + sx * cy + sz * sy,
        y + syv,
        z - sx * sy + sz * cy,
        nx,
        ny,
        nz,
        packAbgr(src.r, src.g, src.b, src.a),
        src.u,
        src.v};
}

bgfx::ShaderHandle loadShader(const fs::path& shader_root, const std::string& shader_subdir, const char* name) {
    const fs::path path = shader_root / shader_subdir / (std::string(name) + ".sc.bin");
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "[AttendBgfx] Missing shader: " << path << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) return BGFX_INVALID_HANDLE;
    const bgfx::Memory* mem = bgfx::alloc(static_cast<std::uint32_t>(size + 1));
    in.read(reinterpret_cast<char*>(mem->data), size);
    mem->data[size] = '\0';
    bgfx::ShaderHandle shader = bgfx::createShader(mem);
    if (bgfx::isValid(shader)) bgfx::setName(shader, name);
    return shader;
}

std::uint64_t samplerFlags(int wrap_s, int wrap_t) {
    std::uint64_t flags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    if (wrap_s == 33071) {
        flags |= BGFX_SAMPLER_U_CLAMP;
    } else if (wrap_s == 33648) {
        flags |= BGFX_SAMPLER_U_MIRROR;
    }
    if (wrap_t == 33071) {
        flags |= BGFX_SAMPLER_V_CLAMP;
    } else if (wrap_t == 33648) {
        flags |= BGFX_SAMPLER_V_MIRROR;
    }
    return flags;
}

std::uint64_t smoothSamplerFlags(int wrap_s, int wrap_t) {
    std::uint64_t flags = 0;
    if (wrap_s == 33071) {
        flags |= BGFX_SAMPLER_U_CLAMP;
    } else if (wrap_s == 33648) {
        flags |= BGFX_SAMPLER_U_MIRROR;
    }
    if (wrap_t == 33071) {
        flags |= BGFX_SAMPLER_V_CLAMP;
    } else if (wrap_t == 33648) {
        flags |= BGFX_SAMPLER_V_MIRROR;
    }
    return flags;
}

std::uint64_t samplerFlagsFromGfWrap(int wrap_s, int wrap_t) {
    const auto toGltfWrap = [](int wrap) {
        if (wrap == 0 || wrap == 1) return 33071;
        if (wrap == 3) return 33648;
        return 10497;
    };
    return samplerFlags(toGltfWrap(wrap_s), toGltfWrap(wrap_t));
}

std::uint64_t opaqueState() {
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
}

std::uint64_t blendState() {
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA;
}

std::uint64_t overlayState() {
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA;
}

std::uint64_t eyeScleraStencilState() {
    return BGFX_STATE_DEPTH_TEST_LEQUAL;
}

std::uint64_t backdropState(bool blend) {
    std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (blend) state |= BGFX_STATE_BLEND_ALPHA;
    return state;
}

int expressionFrameOr(const AttendSceneConfig& config, const char* key, int fallback) {
    const auto it = config.interaction_adapter.eye_expression_frames.find(key);
    return it == config.interaction_adapter.eye_expression_frames.end() ? fallback : std::max(0, it->second);
}

int mouthExpressionFrameOr(const AttendSceneConfig& config, const char* key, int fallback) {
    const auto it = config.interaction_adapter.mouth_expression_frames.find(key);
    return it == config.interaction_adapter.mouth_expression_frames.end() ? fallback : std::max(0, it->second);
}

bool materialUsesEyeExpressionFrames(const AttendPokemonMaterial* material) {
    return material &&
        material->material_role == AttendMaterialRole::EyeSclera &&
        material->eye_sheet.enabled &&
        containsAscii(material->name, "eye");
}

bool materialUsesMouthExpressionFrames(const AttendPokemonMaterial* material) {
    return material &&
        material->material_role == AttendMaterialRole::Mouth &&
        material->eye_sheet.enabled;
}

bool materialIsEyeScleraMask(const AttendPokemonMaterial* material) {
    return materialUsesEyeExpressionFrames(material);
}

bool materialIsSeparateEyeIris(const AttendPokemonMaterial* material) {
    return material && material->material_role == AttendMaterialRole::EyeIris;
}

bool pokemonPrimitivePreviewVisible(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive) {
    if (primitive.default_visible) return true;
    if (!primitive.visible_for_forms.empty()) return true;
    if (primitive.mesh_node >= 0 && primitive.mesh_node < static_cast<int>(model.nodes.size()) &&
        containsAscii(model.nodes[static_cast<std::size_t>(primitive.mesh_node)].name, "vco")) {
        return true;
    }
    if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size()) &&
        containsAscii(model.materials[static_cast<std::size_t>(primitive.material)].name, "vco")) {
        return true;
    }
    return false;
}

bool stringListContains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

Vertex pokemonVertexForMaterial(
    const AttendPokemonVertex& src,
    const AttendPokemonMaterial* material,
    int eye_expression_frame,
    int mouth_expression_frame) {
    float u = src.u;
    float v = src.v;
    if (material &&
        (material->material_role == AttendMaterialRole::EyeSclera ||
         material->material_role == AttendMaterialRole::Mouth) &&
        material->eye_sheet.enabled) {
        const AttendEyeSheet& eye = material->eye_sheet;
        int desired_frame = eye.default_frame;
        if (materialUsesEyeExpressionFrames(material)) {
            desired_frame = eye_expression_frame;
        } else if (materialUsesMouthExpressionFrames(material)) {
            desired_frame = mouth_expression_frame;
        }
        const int frame = std::clamp(
            desired_frame,
            0,
            std::max(0, static_cast<int>(eye.frame_offsets.size()) - 1));
        const float offset_x = eye.frame_offsets.empty() ? 0.0f : eye.frame_offsets[static_cast<std::size_t>(frame)][0];
        const float offset_y = eye.frame_offsets.empty() ? 0.0f : eye.frame_offsets[static_cast<std::size_t>(frame)][1];
        u = src.u * std::abs(eye.scale_x) + offset_x;
        v = src.v + offset_y;
    }
    return Vertex{src.x, src.y, src.z, src.nx, src.ny, src.nz, packAbgr(src.r, src.g, src.b, src.a), u, v};
}

} // namespace pr::gameplay::attend::rendering
