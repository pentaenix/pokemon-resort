#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include "core/config/Json.hpp"
#include "gameplay/attend/rendering/AttendPokemonAsset.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace pr::gameplay::attend::rendering {

namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67u;
constexpr std::uint32_t kChunkJson = 0x4E4F534Au;
constexpr std::uint32_t kChunkBin = 0x004E4942u;
constexpr float kPi = 3.1415926535f;

std::uint32_t readU32Le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void fail(std::string* error, const std::string& message) {
    if (error) *error = message;
}

int intMember(const JsonValue* obj, const char* key, int fallback) {
    if (!obj) return fallback;
    const JsonValue* v = obj->get(key);
    return (v && v->isNumber()) ? static_cast<int>(v->asNumber()) : fallback;
}

bool boolMember(const JsonValue* obj, const char* key, bool fallback) {
    if (!obj) return fallback;
    const JsonValue* v = obj->get(key);
    return (v && v->isBool()) ? v->asBool() : fallback;
}

std::string stringMember(const JsonValue* obj, const char* key, const std::string& fallback = {}) {
    if (!obj) return fallback;
    const JsonValue* v = obj->get(key);
    return (v && v->isString()) ? v->asString() : fallback;
}

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool looksLikeEyeName(const std::string& name) {
    const std::string lower = lowerAscii(name);
    return lower.find("eye") != std::string::npos ||
           lower.find("l_eye") != std::string::npos ||
           lower.find("r_eye") != std::string::npos;
}

bool looksLikeMouthName(const std::string& name) {
    return lowerAscii(name).find("mouth") != std::string::npos;
}

int componentByteSize(int component_type) {
    switch (component_type) {
        case 5120:
        case 5121: return 1;
        case 5122:
        case 5123: return 2;
        case 5125:
        case 5126: return 4;
        default: return 0;
    }
}

int typeComponentCount(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 0;
}

struct AccessorView {
    const std::uint8_t* base = nullptr;
    int count = 0;
    int component_type = 0;
    int num_components = 0;
    std::size_t stride = 0;
    bool normalized = false;
    bool valid = false;
};

class GltfReader {
public:
    GltfReader(const JsonValue& root, const std::uint8_t* bin, std::size_t bin_len)
        : root_(root), bin_(bin), bin_len_(bin_len) {}

    AccessorView accessor(int index) const {
        AccessorView view;
        const JsonValue* accessors = root_.get("accessors");
        const JsonValue* buffer_views = root_.get("bufferViews");
        if (!accessors || !accessors->isArray() || !buffer_views || !buffer_views->isArray()) return view;
        const auto& acc_arr = accessors->asArray();
        if (index < 0 || index >= static_cast<int>(acc_arr.size())) return view;
        const JsonValue& acc = acc_arr[static_cast<std::size_t>(index)];
        const int bv_index = intMember(&acc, "bufferView", -1);
        const auto& bv_arr = buffer_views->asArray();
        if (bv_index < 0 || bv_index >= static_cast<int>(bv_arr.size())) return view;
        const JsonValue& bv = bv_arr[static_cast<std::size_t>(bv_index)];
        const int component_type = intMember(&acc, "componentType", 0);
        const int count = intMember(&acc, "count", 0);
        const std::string type = stringMember(&acc, "type");
        const int comp_size = componentByteSize(component_type);
        const int num_comp = typeComponentCount(type);
        if (comp_size == 0 || num_comp == 0 || count <= 0) return view;
        const std::size_t bv_offset = static_cast<std::size_t>(intMember(&bv, "byteOffset", 0));
        const std::size_t acc_offset = static_cast<std::size_t>(intMember(&acc, "byteOffset", 0));
        const int byte_stride = intMember(&bv, "byteStride", 0);
        const std::size_t stride = byte_stride > 0
            ? static_cast<std::size_t>(byte_stride)
            : static_cast<std::size_t>(comp_size * num_comp);
        const std::size_t start = bv_offset + acc_offset;
        const std::size_t span = static_cast<std::size_t>(count - 1) * stride +
            static_cast<std::size_t>(comp_size * num_comp);
        if (start + span > bin_len_) return view;
        view.base = bin_ + start;
        view.count = count;
        view.component_type = component_type;
        view.num_components = num_comp;
        view.stride = stride;
        if (const JsonValue* normalized = acc.get("normalized"); normalized && normalized->isBool()) {
            view.normalized = normalized->asBool();
        }
        view.valid = true;
        return view;
    }

    static float readFloat(const AccessorView& v, int element, int comp) {
        const std::uint8_t* p = v.base + static_cast<std::size_t>(element) * v.stride;
        if (v.component_type == 5126) {
            float out = 0.0f;
            std::memcpy(&out, p + static_cast<std::size_t>(comp) * sizeof(float), sizeof(float));
            return out;
        }
        return readNormalized(v, element, comp);
    }

    static float readNormalized(const AccessorView& v, int element, int comp) {
        const std::uint8_t* p = v.base + static_cast<std::size_t>(element) * v.stride;
        switch (v.component_type) {
            case 5120: return std::max(-1.0f, static_cast<float>(reinterpret_cast<const std::int8_t*>(p)[comp]) / 127.0f);
            case 5121: return static_cast<float>(p[comp]) / 255.0f;
            case 5122: {
                std::int16_t out = 0;
                std::memcpy(&out, p + static_cast<std::size_t>(comp) * sizeof(out), sizeof(out));
                return std::max(-1.0f, static_cast<float>(out) / 32767.0f);
            }
            case 5123: {
                std::uint16_t out = 0;
                std::memcpy(&out, p + static_cast<std::size_t>(comp) * sizeof(out), sizeof(out));
                return static_cast<float>(out) / 65535.0f;
            }
            case 5126: {
                float out = 0.0f;
                std::memcpy(&out, p + static_cast<std::size_t>(comp) * sizeof(out), sizeof(out));
                return out;
            }
            default: return 0.0f;
        }
    }

    static std::uint32_t readUint(const AccessorView& v, int element, int comp = 0) {
        const std::uint8_t* p = v.base + static_cast<std::size_t>(element) * v.stride;
        switch (v.component_type) {
            case 5121: return static_cast<std::uint32_t>(p[comp]);
            case 5123: {
                std::uint16_t out = 0;
                std::memcpy(&out, p + static_cast<std::size_t>(comp) * sizeof(out), sizeof(out));
                return out;
            }
            case 5125: {
                std::uint32_t out = 0;
                std::memcpy(&out, p + static_cast<std::size_t>(comp) * sizeof(out), sizeof(out));
                return out;
            }
            default: return 0;
        }
    }

private:
    const JsonValue& root_;
    const std::uint8_t* bin_ = nullptr;
    std::size_t bin_len_ = 0;
};

std::array<float, 16> identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

std::array<float, 16> multiply(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[static_cast<std::size_t>(k * 4 + row)] *
                    b[static_cast<std::size_t>(col * 4 + k)];
            }
            r[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return r;
}

void transformPoint(const std::array<float, 16>& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[4] * y + m[8] * z + m[12];
    oy = m[1] * x + m[5] * y + m[9] * z + m[13];
    oz = m[2] * x + m[6] * y + m[10] * z + m[14];
}

void transformVector(const std::array<float, 16>& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[4] * y + m[8] * z;
    oy = m[1] * x + m[5] * y + m[9] * z;
    oz = m[2] * x + m[6] * y + m[10] * z;
}

void normalizeVector(float& x, float& y, float& z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 0.000001f) {
        x = 0.0f;
        y = 1.0f;
        z = 0.0f;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

std::array<float, 4> normalizeQuat(std::array<float, 4> q) {
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len <= 0.000001f) return {0.0f, 0.0f, 0.0f, 1.0f};
    for (float& v : q) v /= len;
    return q;
}

std::array<float, 4> slerp(std::array<float, 4> a, std::array<float, 4> b, float t) {
    a = normalizeQuat(a);
    b = normalizeQuat(b);
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0f) {
        dot = -dot;
        for (float& v : b) v = -v;
    }
    if (dot > 0.9995f) {
        std::array<float, 4> out{};
        for (int i = 0; i < 4; ++i) out[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)] + (b[static_cast<std::size_t>(i)] - a[static_cast<std::size_t>(i)]) * t;
        return normalizeQuat(out);
    }
    const float theta_0 = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float theta = theta_0 * t;
    const float sin_theta = std::sin(theta);
    const float sin_theta_0 = std::sin(theta_0);
    const float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
    const float s1 = sin_theta / sin_theta_0;
    return {
        a[0] * s0 + b[0] * s1,
        a[1] * s0 + b[1] * s1,
        a[2] * s0 + b[2] * s1,
        a[3] * s0 + b[3] * s1};
}

std::array<float, 4> multiplyQuat(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    return normalizeQuat({
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]});
}

std::array<float, 4> axisAngle(float x, float y, float z, float radians) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 0.000001f) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float half = radians * 0.5f;
    const float s = std::sin(half) / len;
    return normalizeQuat({x * s, y * s, z * s, std::cos(half)});
}

std::array<float, 16> trsMatrix(
    const std::array<float, 3>& t,
    const std::array<float, 4>& quat,
    const std::array<float, 3>& s) {
    const std::array<float, 4> q = normalizeQuat(quat);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    std::array<float, 16> r = identity();
    r[0] = (1.0f - 2.0f * (yy + zz)) * s[0];
    r[1] = (2.0f * (xy + wz)) * s[0];
    r[2] = (2.0f * (xz - wy)) * s[0];
    r[4] = (2.0f * (xy - wz)) * s[1];
    r[5] = (1.0f - 2.0f * (xx + zz)) * s[1];
    r[6] = (2.0f * (yz + wx)) * s[1];
    r[8] = (2.0f * (xz + wy)) * s[2];
    r[9] = (2.0f * (yz - wx)) * s[2];
    r[10] = (1.0f - 2.0f * (xx + yy)) * s[2];
    r[12] = t[0];
    r[13] = t[1];
    r[14] = t[2];
    return r;
}

void readNodeTrs(
    const JsonValue& node,
    std::array<float, 3>& t,
    std::array<float, 4>& q,
    std::array<float, 3>& s) {
    if (const JsonValue* tr = node.get("translation"); tr && tr->isArray()) {
        const auto& arr = tr->asArray();
        for (std::size_t i = 0; i < 3 && i < arr.size(); ++i) if (arr[i].isNumber()) t[i] = static_cast<float>(arr[i].asNumber());
    }
    if (const JsonValue* rot = node.get("rotation"); rot && rot->isArray()) {
        const auto& arr = rot->asArray();
        for (std::size_t i = 0; i < 4 && i < arr.size(); ++i) if (arr[i].isNumber()) q[i] = static_cast<float>(arr[i].asNumber());
    }
    if (const JsonValue* sc = node.get("scale"); sc && sc->isArray()) {
        const auto& arr = sc->asArray();
        for (std::size_t i = 0; i < 3 && i < arr.size(); ++i) if (arr[i].isNumber()) s[i] = static_cast<float>(arr[i].asNumber());
    }
}

std::array<float, 16> nodeLocalMatrix(
    const JsonValue& node,
    std::array<float, 3>& t,
    std::array<float, 4>& q,
    std::array<float, 3>& s,
    bool& has_matrix) {
    if (const JsonValue* matrix = node.get("matrix"); matrix && matrix->isArray()) {
        const auto& arr = matrix->asArray();
        if (arr.size() == 16) {
            std::array<float, 16> out{};
            for (std::size_t i = 0; i < 16; ++i) {
                out[i] = arr[i].isNumber() ? static_cast<float>(arr[i].asNumber()) : 0.0f;
            }
            has_matrix = true;
            return out;
        }
    }
    readNodeTrs(node, t, q, s);
    return trsMatrix(t, q, s);
}

std::vector<std::uint8_t> imageBytes(const JsonValue& root, const std::uint8_t* bin, std::size_t bin_len, int texture_index) {
    const JsonValue* textures = root.get("textures");
    const JsonValue* images = root.get("images");
    const JsonValue* buffer_views = root.get("bufferViews");
    if (texture_index < 0 || !textures || !textures->isArray() || !images || !images->isArray() ||
        !buffer_views || !buffer_views->isArray()) {
        return {};
    }
    const auto& tex_arr = textures->asArray();
    if (texture_index >= static_cast<int>(tex_arr.size())) return {};
    const int src = intMember(&tex_arr[static_cast<std::size_t>(texture_index)], "source", -1);
    const auto& img_arr = images->asArray();
    if (src < 0 || src >= static_cast<int>(img_arr.size())) return {};
    const int bv_index = intMember(&img_arr[static_cast<std::size_t>(src)], "bufferView", -1);
    const auto& bv_arr = buffer_views->asArray();
    if (bv_index < 0 || bv_index >= static_cast<int>(bv_arr.size())) return {};
    const JsonValue& bv = bv_arr[static_cast<std::size_t>(bv_index)];
    const std::size_t off = static_cast<std::size_t>(intMember(&bv, "byteOffset", 0));
    const std::size_t len = static_cast<std::size_t>(intMember(&bv, "byteLength", 0));
    if (len == 0 || off + len > bin_len) return {};
    return std::vector<std::uint8_t>(bin + off, bin + off + len);
}

int textureSourceImage(const JsonValue& root, int texture_index) {
    const JsonValue* textures = root.get("textures");
    if (texture_index < 0 || !textures || !textures->isArray()) return -1;
    const auto& tex_arr = textures->asArray();
    if (texture_index >= static_cast<int>(tex_arr.size())) return -1;
    return intMember(&tex_arr[static_cast<std::size_t>(texture_index)], "source", -1);
}

AttendTextureSampler textureSampler(const JsonValue& root, int texture_index) {
    AttendTextureSampler out;
    const JsonValue* textures = root.get("textures");
    const JsonValue* samplers = root.get("samplers");
    if (texture_index < 0 || !textures || !textures->isArray() || !samplers || !samplers->isArray()) {
        return out;
    }
    const auto& tex_arr = textures->asArray();
    if (texture_index >= static_cast<int>(tex_arr.size())) return out;
    const int sampler_index = intMember(&tex_arr[static_cast<std::size_t>(texture_index)], "sampler", -1);
    const auto& sampler_arr = samplers->asArray();
    if (sampler_index < 0 || sampler_index >= static_cast<int>(sampler_arr.size())) return out;
    const JsonValue& sampler = sampler_arr[static_cast<std::size_t>(sampler_index)];
    out.wrap_s = intMember(&sampler, "wrapS", out.wrap_s);
    out.wrap_t = intMember(&sampler, "wrapT", out.wrap_t);
    out.mag_filter = intMember(&sampler, "magFilter", out.mag_filter);
    out.min_filter = intMember(&sampler, "minFilter", out.min_filter);
    return out;
}

std::vector<std::string> stringArrayMember(const JsonValue* obj, const char* key) {
    std::vector<std::string> out;
    if (!obj || !obj->isObject()) return out;
    const JsonValue* value = obj->get(key);
    if (!value || !value->isArray()) return out;
    for (const JsonValue& item : value->asArray()) {
        if (item.isString() && !item.asString().empty()) out.push_back(item.asString());
    }
    return out;
}

void readFloatArray(const JsonValue* value, float* out, std::size_t count) {
    if (!value || !value->isArray()) return;
    const auto& values = value->asArray();
    for (std::size_t i = 0; i < count && i < values.size(); ++i) {
        if (values[i].isNumber()) out[i] = static_cast<float>(values[i].asNumber());
    }
}

std::array<float, 2> readFloatPair(const JsonValue* value) {
    std::array<float, 2> out{0.0f, 0.0f};
    readFloatArray(value, out.data(), out.size());
    return out;
}

const JsonValue* raeExtras(const JsonValue& obj) {
    const JsonValue* extras = obj.get("extras");
    if (!extras || !extras->isObject()) return nullptr;
    const JsonValue* rae = extras->get("rae");
    return (rae && rae->isObject()) ? rae : nullptr;
}

void readTextureVariants(const JsonValue& root, AttendPokemonModel& out) {
    out.default_texture_variant = "normal";
    out.texture_variants.clear();
    out.default_form_variant.clear();
    out.form_variants.clear();
    const JsonValue* rae = raeExtras(root);
    if (!rae) return;
    if (const JsonValue* appearance = rae->get("appearanceVariants"); appearance && appearance->isObject()) {
        if (const JsonValue* defaults = appearance->get("default"); defaults && defaults->isObject()) {
            out.default_texture_variant = stringMember(defaults, "texture", out.default_texture_variant);
            out.default_form_variant = stringMember(defaults, "form", out.default_form_variant);
        }
        if (const JsonValue* axes = appearance->get("axes"); axes && axes->isArray()) {
            for (const JsonValue& axis : axes->asArray()) {
                if (!axis.isObject()) continue;
                const std::string axis_id = stringMember(&axis, "id");
                const JsonValue* options = axis.get("options");
                if (!options || !options->isArray()) continue;
                std::vector<AttendTextureVariantOption>* target = nullptr;
                if (axis_id == "texture") {
                    target = &out.texture_variants;
                    out.default_texture_variant = stringMember(&axis, "default", out.default_texture_variant);
                } else if (axis_id == "form") {
                    target = &out.form_variants;
                    out.default_form_variant = stringMember(&axis, "default", out.default_form_variant);
                }
                if (!target) continue;
                target->clear();
                for (const JsonValue& option : options->asArray()) {
                    if (!option.isObject()) continue;
                    AttendTextureVariantOption out_option;
                    out_option.id = stringMember(&option, "id");
                    out_option.label = stringMember(&option, "label", out_option.id);
                    if (!out_option.id.empty()) target->push_back(std::move(out_option));
                }
            }
        }
    }
    if (out.texture_variants.empty()) {
        if (const JsonValue* variants = rae->get("textureVariants"); variants && variants->isObject()) {
            out.default_texture_variant = stringMember(variants, "default", out.default_texture_variant);
            const JsonValue* options = variants->get("options");
            if (options && options->isArray()) {
                for (const JsonValue& option : options->asArray()) {
                    if (!option.isObject()) continue;
                    AttendTextureVariantOption out_option;
                    out_option.id = stringMember(&option, "id");
                    out_option.label = stringMember(&option, "label", out_option.id);
                    if (!out_option.id.empty()) out.texture_variants.push_back(std::move(out_option));
                }
            }
        }
    }
    if (out.form_variants.empty()) {
        if (const JsonValue* species = rae->get("speciesVariants"); species && species->isObject()) {
            if (const JsonValue* defaults = species->get("default"); defaults && defaults->isObject()) {
                out.default_form_variant = stringMember(defaults, "form", out.default_form_variant);
            }
            const JsonValue* forms = species->get("forms");
            if (forms && forms->isArray()) {
                for (const JsonValue& form : forms->asArray()) {
                    if (!form.isObject()) continue;
                    AttendTextureVariantOption option;
                    option.id = stringMember(&form, "id");
                    option.label = stringMember(&form, "label", option.id);
                    if (!option.id.empty()) out.form_variants.push_back(std::move(option));
                }
            }
        }
    }
    if (out.form_variants.size() == 1 && out.default_form_variant.empty()) {
        out.default_form_variant = out.form_variants.front().id;
    }
    if (!out.form_variants.empty() && out.default_form_variant.empty()) {
        out.default_form_variant = out.form_variants.front().id;
    }
    if (!out.texture_variants.empty()) return;
    const JsonValue* variants = rae->get("textureVariants");
    if (!variants || !variants->isObject()) return;
    out.default_texture_variant = stringMember(variants, "default", out.default_texture_variant);
    const JsonValue* options = variants->get("options");
    if (!options || !options->isArray()) return;
    for (const JsonValue& option : options->asArray()) {
        if (!option.isObject()) continue;
        AttendTextureVariantOption out_option;
        out_option.id = stringMember(&option, "id");
        out_option.label = stringMember(&option, "label", out_option.id);
        if (!out_option.id.empty()) out.texture_variants.push_back(std::move(out_option));
    }
}

void readNumberPair(const JsonValue& array, std::size_t index, float& x, float& y) {
    if (!array.isArray()) return;
    const auto& rows = array.asArray();
    if (index >= rows.size() || !rows[index].isArray()) return;
    const auto& pair = rows[index].asArray();
    if (!pair.empty() && pair[0].isNumber()) x = static_cast<float>(pair[0].asNumber());
    if (pair.size() > 1 && pair[1].isNumber()) y = static_cast<float>(pair[1].asNumber());
}

void readEyeSheet(const JsonValue* rae, AttendPokemonMaterial& material) {
    if (!rae) return;
    const std::string role = stringMember(rae, "materialRole");
    if (role == "eye_sclera") {
        material.material_role = AttendMaterialRole::EyeSclera;
    } else if (role == "eye_iris") {
        material.material_role = AttendMaterialRole::EyeIris;
    } else if (role == "mouth") {
        material.material_role = AttendMaterialRole::Mouth;
    }

    const JsonValue* sheet = rae->get("eyeSheet");
    const JsonValue* expression = rae->get("eyeExpression");
    if (!sheet || !sheet->isObject() || !expression || !expression->isObject()) return;

    material.eye_sheet.enabled = true;
    material.eye_sheet.cols = std::max(1, intMember(sheet, "cols", material.eye_sheet.cols));
    material.eye_sheet.rows = std::max(1, intMember(sheet, "rows", material.eye_sheet.rows));
    if (const JsonValue* scale = sheet->get("scale"); scale && scale->isArray()) {
        const auto& arr = scale->asArray();
        if (!arr.empty() && arr[0].isNumber()) material.eye_sheet.scale_x = static_cast<float>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) material.eye_sheet.scale_y = static_cast<float>(arr[1].asNumber());
    }
    if (const JsonValue* translation = sheet->get("translation"); translation && translation->isArray()) {
        const auto& arr = translation->asArray();
        if (!arr.empty() && arr[0].isNumber()) material.eye_sheet.translation_x = static_cast<float>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) material.eye_sheet.translation_y = static_cast<float>(arr[1].asNumber());
    }
    if (const JsonValue* wrap = sheet->get("wrap"); wrap && wrap->isArray()) {
        const auto& arr = wrap->asArray();
        if (!arr.empty() && arr[0].isNumber()) material.eye_sheet.wrap_s = static_cast<int>(arr[0].asNumber());
        if (arr.size() > 1 && arr[1].isNumber()) material.eye_sheet.wrap_t = static_cast<int>(arr[1].asNumber());
    }
    material.eye_sheet.default_frame = std::max(0, intMember(expression, "defaultFrame", 0));
    if (const JsonValue* offsets = expression->get("frameOffsets"); offsets && offsets->isArray()) {
        material.eye_sheet.frame_offsets.resize(offsets->asArray().size(), {0.0f, 0.0f});
        for (std::size_t i = 0; i < offsets->asArray().size(); ++i) {
            readNumberPair(*offsets, i, material.eye_sheet.frame_offsets[i][0], material.eye_sheet.frame_offsets[i][1]);
        }
    }
}

void applyDefaultGen7ExpressionSheet(AttendPokemonMaterial& material) {
    if (material.eye_sheet.enabled) return;
    material.eye_sheet.enabled = true;
    material.eye_sheet.cols = 2;
    material.eye_sheet.rows = 4;
    material.eye_sheet.scale_x = 2.0f;
    material.eye_sheet.scale_y = 1.0f;
    material.eye_sheet.translation_x = 1.0f;
    material.eye_sheet.translation_y = 0.0f;
    material.eye_sheet.wrap_s = 3;
    material.eye_sheet.wrap_t = 2;
    material.eye_sheet.default_frame = 0;
    material.eye_sheet.frame_offsets = {
        std::array<float, 2>{0.0f, 0.0f},
        std::array<float, 2>{1.0f, 0.0f},
        std::array<float, 2>{0.0f, 0.25f},
        std::array<float, 2>{1.0f, 0.25f},
        std::array<float, 2>{0.0f, 0.50f},
        std::array<float, 2>{1.0f, 0.50f},
        std::array<float, 2>{0.0f, 0.75f},
        std::array<float, 2>{1.0f, 0.75f}};
}

std::vector<float> readScalarSeries(const GltfReader& reader, int accessor_index) {
    const AccessorView view = reader.accessor(accessor_index);
    std::vector<float> out;
    if (!view.valid) return out;
    out.reserve(static_cast<std::size_t>(view.count));
    for (int i = 0; i < view.count; ++i) out.push_back(GltfReader::readFloat(view, i, 0));
    return out;
}

std::vector<std::array<float, 4>> readVecSeries(const GltfReader& reader, int accessor_index) {
    const AccessorView view = reader.accessor(accessor_index);
    std::vector<std::array<float, 4>> out;
    if (!view.valid) return out;
    out.reserve(static_cast<std::size_t>(view.count));
    for (int i = 0; i < view.count; ++i) {
        std::array<float, 4> value{0.0f, 0.0f, 0.0f, 1.0f};
        for (int c = 0; c < view.num_components && c < 4; ++c) {
            value[static_cast<std::size_t>(c)] = GltfReader::readFloat(view, i, c);
        }
        out.push_back(value);
    }
    return out;
}

void readMaterials(const JsonValue& root, const std::uint8_t* bin, std::size_t bin_len, AttendPokemonModel& out) {
    const JsonValue* materials = root.get("materials");
    if (!materials || !materials->isArray()) return;
    for (const JsonValue& mat : materials->asArray()) {
        AttendPokemonMaterial material;
        material.name = stringMember(&mat, "name");
        material.double_sided = boolMember(&mat, "doubleSided", false);
        if (const JsonValue* pbr = mat.get("pbrMetallicRoughness"); pbr && pbr->isObject()) {
            if (const JsonValue* bcf = pbr->get("baseColorFactor"); bcf && bcf->isArray()) {
                const auto& arr = bcf->asArray();
                for (std::size_t i = 0; i < 4 && i < arr.size(); ++i) {
                    if (arr[i].isNumber()) material.base_color[i] = static_cast<float>(arr[i].asNumber());
                }
            }
            if (const JsonValue* tex = pbr->get("baseColorTexture"); tex && tex->isObject()) {
                const int texture_index = intMember(tex, "index", -1);
                material.base_color_image = textureSourceImage(root, texture_index);
                material.base_color_bytes = imageBytes(root, bin, bin_len, texture_index);
                material.has_base_color_texture = !material.base_color_bytes.empty();
                material.base_color_sampler = textureSampler(root, texture_index);
            }
        }
        if (const JsonValue* emissive = mat.get("emissiveTexture"); emissive && emissive->isObject()) {
            const int texture_index = intMember(emissive, "index", -1);
            material.emissive_image = textureSourceImage(root, texture_index);
            material.emissive_bytes = imageBytes(root, bin, bin_len, texture_index);
            material.has_emissive_texture = !material.emissive_bytes.empty();
        }
        if (const JsonValue* cutoff = mat.get("alphaCutoff"); cutoff && cutoff->isNumber()) {
            material.alpha_cutoff = std::clamp(static_cast<float>(cutoff->asNumber()), 0.0f, 1.0f);
        }
        if (const JsonValue* alpha = mat.get("alphaMode"); alpha && alpha->isString()) {
            material.has_alpha_mode = true;
            material.alpha_mode = alpha->asString();
            if (material.alpha_mode == "MASK") {
                material.render_class = AttendRenderClass::Mask;
            } else if (material.alpha_mode == "BLEND") {
                material.render_class = AttendRenderClass::Blend;
            }
        }
        if (const JsonValue* rae = raeExtras(mat)) {
            if (const JsonValue* environment = rae->get("environmentMaterial");
                environment && environment->isObject()) {
                material.environment_role = stringMember(environment, "role");
            }
            if (const JsonValue* nitro = rae->get("nitro"); nitro && nitro->isObject()) {
                material.nitro_texture_alpha = stringMember(nitro, "textureAlpha");
            }
            if (const JsonValue* pica = rae->get("pica"); pica && pica->isObject()) {
                material.has_authoritative_pica = boolMember(pica, "authoritative", true);
                material.pica_vertex_alpha_blend =
                    boolMember(pica, "alphaBlendEnabled", false) &&
                    stringMember(pica, "sourceRgbFactor") == "source_alpha" &&
                    stringMember(pica, "destinationRgbFactor") == "one_minus_source_alpha" &&
                    !boolMember(pica, "depthWriteEnabled", true);
                material.pica_multiplicative_blend =
                    boolMember(pica, "alphaBlendEnabled", false) &&
                    stringMember(pica, "sourceRgbFactor") == "destination_color" &&
                    stringMember(pica, "destinationRgbFactor") == "zero" &&
                    !boolMember(pica, "depthWriteEnabled", true);
            }
            material.shiny_material_index = intMember(rae, "shinyMaterialIndex", material.shiny_material_index);
            if (const JsonValue* forms = rae->get("formMaterialIndices"); forms && forms->isObject()) {
                for (const auto& [form_id, value] : forms->asObject()) {
                    if (!value.isNumber()) continue;
                    material.form_material_indices.emplace_back(form_id, static_cast<int>(value.asNumber()));
                }
            }
            const std::string render_class = stringMember(rae, "renderClass");
            if (render_class == "mask") {
                material.has_rae_policy = true;
                material.render_class = AttendRenderClass::Mask;
            } else if (render_class == "blend") {
                material.has_rae_policy = true;
                material.render_class = AttendRenderClass::Blend;
            } else if (render_class == "additive") {
                material.has_rae_policy = true;
                material.render_class = AttendRenderClass::Additive;
            } else if (render_class == "uniform_decal") {
                material.has_rae_policy = true;
                material.render_class = AttendRenderClass::UniformDecal;
            } else if (render_class == "opaque") {
                material.has_rae_policy = true;
                material.render_class = AttendRenderClass::Opaque;
            }
            if (const JsonValue* mapping = rae->get("textureMapping");
                mapping && mapping->isObject()) {
                const std::string type = stringMember(mapping, "type");
                if (type == "camera_cube_environment") {
                    material.texture_mapping = AttendTextureMapping::CameraCubeEnvironment;
                } else if (type == "camera_sphere_environment") {
                    material.texture_mapping = AttendTextureMapping::CameraSphereEnvironment;
                } else if (type == "projection") {
                    material.texture_mapping = AttendTextureMapping::Projection;
                } else if (type == "shadow") {
                    material.texture_mapping = AttendTextureMapping::Shadow;
                } else if (type == "shadow_box") {
                    material.texture_mapping = AttendTextureMapping::ShadowBox;
                } else if (type != "uv" && !type.empty()) {
                    material.texture_mapping = AttendTextureMapping::Unknown;
                }
            }
            if (const JsonValue* tev = rae->get("picaTev"); tev && tev->isObject()) {
                material.pica_tev.enabled = true;
                material.pica_tev.outer_water_base = boolMember(tev, "outerWaterBase", false);
                material.pica_tev.standalone_black_key = boolMember(tev, "standaloneBlackKey", false);
                if (const JsonValue* scale = tev->get("effectColorScale"); scale && scale->isNumber()) {
                    material.pica_tev.effect_color_scale = std::clamp(
                        static_cast<float>(scale->asNumber()), 0.0f, 1.0f);
                }
                if (const JsonValue* indices = tev->get("textureIndices"); indices && indices->isObject()) {
                    for (int unit = 0; unit < 3; ++unit) {
                        const std::string key = std::to_string(unit);
                        material.pica_tev.texture_indices[static_cast<std::size_t>(unit)] =
                            intMember(indices, key.c_str(), -1);
                    }
                }
                if (const JsonValue* coord_sets = tev->get("textureCoordSets"); coord_sets && coord_sets->isObject()) {
                    for (int unit = 0; unit < 3; ++unit) {
                        const std::string key = std::to_string(unit);
                        material.pica_tev.texture_coord_sets[static_cast<std::size_t>(unit)] =
                            intMember(coord_sets, key.c_str(), unit);
                    }
                }
                if (const JsonValue* offsets = tev->get("initialOffsets"); offsets && offsets->isObject()) {
                    for (int unit = 0; unit < 3; ++unit) {
                        const std::string key = std::to_string(unit);
                        material.pica_tev.initial_offsets[static_cast<std::size_t>(unit)] =
                            readFloatPair(offsets->get(key));
                    }
                }
                readFloatArray(tev->get("bufferColor"), material.pica_tev.buffer_color.data(), 4);
                if (const JsonValue* assignments = tev->get("constantAssignments"); assignments && assignments->isArray()) {
                    for (std::size_t i = 0; i < 6 && i < assignments->asArray().size(); ++i) {
                        if (assignments->asArray()[i].isNumber()) {
                            material.pica_tev.constant_assignments[i] =
                                static_cast<int>(assignments->asArray()[i].asNumber());
                        }
                    }
                }
                if (const JsonValue* colors = tev->get("constantColors"); colors && colors->isArray()) {
                    for (std::size_t i = 0; i < 6 && i < colors->asArray().size(); ++i) {
                        readFloatArray(&colors->asArray()[i], material.pica_tev.constant_colors[i].data(), 4);
                    }
                }
                if (const JsonValue* stages = tev->get("stages"); stages && stages->isArray()) {
                    for (std::size_t i = 0; i < 6 && i < stages->asArray().size(); ++i) {
                        const JsonValue& stage = stages->asArray()[i];
                        AttendPicaTevStage& parsed = material.pica_tev.stages[i];
                        parsed.source = static_cast<std::uint32_t>(intMember(&stage, "source", 0));
                        parsed.operand = static_cast<std::uint32_t>(intMember(&stage, "operand", 0));
                        parsed.combiner = static_cast<std::uint32_t>(intMember(&stage, "combiner", 0));
                        parsed.scale = static_cast<std::uint32_t>(intMember(&stage, "scale", 0));
                        parsed.update_color_buffer = boolMember(&stage, "updateColorBuffer", false);
                        parsed.update_alpha_buffer = boolMember(&stage, "updateAlphaBuffer", false);
                    }
                }
                for (int unit = 0; unit < 3; ++unit) {
                    int texture_index = material.pica_tev.texture_indices[static_cast<std::size_t>(unit)];
                    if (texture_index < 0 && unit == 0) {
                        if (const JsonValue* pbr = mat.get("pbrMetallicRoughness"); pbr && pbr->isObject()) {
                            if (const JsonValue* tex = pbr->get("baseColorTexture"); tex && tex->isObject()) {
                                texture_index = intMember(tex, "index", -1);
                            }
                        }
                    }
                    if (texture_index < 0) continue;
                    material.texture_unit_bytes[static_cast<std::size_t>(unit)] =
                        imageBytes(root, bin, bin_len, texture_index);
                    material.texture_unit_samplers[static_cast<std::size_t>(unit)] =
                        textureSampler(root, texture_index);
                    material.has_texture_unit[static_cast<std::size_t>(unit)] =
                        !material.texture_unit_bytes[static_cast<std::size_t>(unit)].empty();
                }
            }
            readEyeSheet(rae, material);
        }
        if (!material.pica_tev.enabled && material.has_base_color_texture) {
            material.texture_unit_bytes[0] = material.base_color_bytes;
            material.texture_unit_samplers[0] = material.base_color_sampler;
            material.has_texture_unit[0] = true;
        }
        if (material.eye_sheet.enabled && looksLikeMouthName(material.name)) {
            material.material_role = AttendMaterialRole::Mouth;
        }
        material.pokemon_eye = material.has_emissive_texture &&
            looksLikeEyeName(material.name) &&
            material.material_role == AttendMaterialRole::None;
        out.materials.push_back(std::move(material));
    }
}

void readNodes(const JsonValue& root, AttendPokemonModel& out) {
    const JsonValue* nodes = root.get("nodes");
    if (!nodes || !nodes->isArray()) return;
    out.nodes.resize(nodes->asArray().size());
    for (std::size_t i = 0; i < nodes->asArray().size(); ++i) {
        const JsonValue& node = nodes->asArray()[i];
        out.nodes[i].name = stringMember(&node, "name");
        out.nodes[i].scene_order = static_cast<int>(i);
        if (const JsonValue* rae = raeExtras(node)) {
            out.nodes[i].default_visible = boolMember(rae, "defaultVisible", out.nodes[i].default_visible);
            out.nodes[i].render_order = intMember(rae, "renderOrder", out.nodes[i].render_order);
            out.nodes[i].visible_for_forms = stringArrayMember(rae, "visibleForForms");
            out.nodes[i].source_slot = intMember(rae, "sourceSlot", -1);
            out.nodes[i].composition_priority = intMember(rae, "compositionPriority", 0);
            out.nodes[i].composition_role = stringMember(rae, "compositionRole", "self_contained");
        }
        out.nodes[i].local_matrix = nodeLocalMatrix(
            node,
            out.nodes[i].base_translation,
            out.nodes[i].base_rotation,
            out.nodes[i].base_scale,
            out.nodes[i].has_matrix);
        if (const JsonValue* children = node.get("children"); children && children->isArray()) {
            for (const JsonValue& child : children->asArray()) {
                if (!child.isNumber()) continue;
                const int child_index = static_cast<int>(child.asNumber());
                if (child_index < 0 || child_index >= static_cast<int>(out.nodes.size())) continue;
                out.nodes[i].children.push_back(child_index);
                out.nodes[static_cast<std::size_t>(child_index)].parent = static_cast<int>(i);
            }
        }
    }
}

void readSkins(const JsonValue& root, const GltfReader& reader, AttendPokemonModel& out) {
    const JsonValue* skins = root.get("skins");
    if (!skins || !skins->isArray()) return;
    for (const JsonValue& skin_json : skins->asArray()) {
        AttendPokemonSkin skin;
        if (const JsonValue* joints = skin_json.get("joints"); joints && joints->isArray()) {
            for (const JsonValue& joint : joints->asArray()) {
                if (joint.isNumber()) skin.joints.push_back(static_cast<int>(joint.asNumber()));
            }
        }
        const int ibm_accessor = intMember(&skin_json, "inverseBindMatrices", -1);
        const AccessorView ibm = reader.accessor(ibm_accessor);
        if (ibm.valid && ibm.num_components == 16) {
            for (int i = 0; i < ibm.count; ++i) {
                std::array<float, 16> matrix{};
                for (int c = 0; c < 16; ++c) matrix[static_cast<std::size_t>(c)] = GltfReader::readFloat(ibm, i, c);
                skin.inverse_bind_matrices.push_back(matrix);
            }
        }
        if (skin.inverse_bind_matrices.size() < skin.joints.size()) {
            skin.inverse_bind_matrices.resize(skin.joints.size(), identity());
        }
        out.skins.push_back(std::move(skin));
    }
}

AttendPokemonVertex readVertex(
    const GltfReader& reader,
    const AccessorView& positions,
    const AccessorView& normals,
    const AccessorView& uvs,
    const AccessorView& uvs1,
    const AccessorView& uvs2,
    const AccessorView& colors,
    const AccessorView& joints,
    const AccessorView& weights,
    int index) {
    AttendPokemonVertex v;
    v.x = GltfReader::readFloat(positions, index, 0);
    v.y = GltfReader::readFloat(positions, index, 1);
    v.z = GltfReader::readFloat(positions, index, 2);
    if (normals.valid && index < normals.count && normals.num_components >= 3) {
        v.nx = GltfReader::readFloat(normals, index, 0);
        v.ny = GltfReader::readFloat(normals, index, 1);
        v.nz = GltfReader::readFloat(normals, index, 2);
    }
    if (uvs.valid && index < uvs.count && uvs.num_components >= 2) {
        v.u = GltfReader::readFloat(uvs, index, 0);
        v.v = GltfReader::readFloat(uvs, index, 1);
    }
    if (uvs1.valid && index < uvs1.count && uvs1.num_components >= 2) {
        v.u1 = GltfReader::readFloat(uvs1, index, 0);
        v.v1 = GltfReader::readFloat(uvs1, index, 1);
    } else {
        v.u1 = v.u;
        v.v1 = v.v;
    }
    if (uvs2.valid && index < uvs2.count && uvs2.num_components >= 2) {
        v.u2 = GltfReader::readFloat(uvs2, index, 0);
        v.v2 = GltfReader::readFloat(uvs2, index, 1);
    } else {
        v.u2 = v.u;
        v.v2 = v.v;
    }
    if (colors.valid && index < colors.count && colors.num_components >= 3) {
        v.r = GltfReader::readNormalized(colors, index, 0);
        v.g = GltfReader::readNormalized(colors, index, 1);
        v.b = GltfReader::readNormalized(colors, index, 2);
        v.a = colors.num_components >= 4 ? GltfReader::readNormalized(colors, index, 3) : 1.0f;
    }
    if (joints.valid && index < joints.count) {
        for (int c = 0; c < joints.num_components && c < 4; ++c) {
            v.joints[static_cast<std::size_t>(c)] = static_cast<std::uint16_t>(GltfReader::readUint(joints, index, c));
        }
    }
    if (weights.valid && index < weights.count) {
        float total = 0.0f;
        for (int c = 0; c < weights.num_components && c < 4; ++c) {
            v.weights[static_cast<std::size_t>(c)] = weights.normalized
                ? GltfReader::readNormalized(weights, index, c)
                : GltfReader::readFloat(weights, index, c);
            total += v.weights[static_cast<std::size_t>(c)];
        }
        if (total > 0.000001f) {
            for (float& weight : v.weights) weight /= total;
        }
    }
    (void)reader;
    return v;
}

void readPrimitives(const JsonValue& root, const GltfReader& reader, AttendPokemonModel& out) {
    const JsonValue* nodes = root.get("nodes");
    const JsonValue* meshes = root.get("meshes");
    if (!nodes || !nodes->isArray() || !meshes || !meshes->isArray()) return;
    for (std::size_t node_index = 0; node_index < nodes->asArray().size(); ++node_index) {
        const JsonValue& node = nodes->asArray()[node_index];
        const int mesh_index = intMember(&node, "mesh", -1);
        if (mesh_index < 0 || mesh_index >= static_cast<int>(meshes->asArray().size())) continue;
        const int skin_index = intMember(&node, "skin", -1);
        const JsonValue& mesh = meshes->asArray()[static_cast<std::size_t>(mesh_index)];
        const bool eye_mesh = looksLikeEyeName(stringMember(&node, "name")) ||
                              looksLikeEyeName(stringMember(&mesh, "name"));
        const bool mouth_mesh = looksLikeMouthName(stringMember(&node, "name")) ||
                                looksLikeMouthName(stringMember(&mesh, "name"));
        const JsonValue* primitives = mesh.get("primitives");
        if (!primitives || !primitives->isArray()) continue;
        for (const JsonValue& primitive_json : primitives->asArray()) {
            const JsonValue* attrs = primitive_json.get("attributes");
            if (!attrs || !attrs->isObject()) continue;
            const AccessorView positions = reader.accessor(intMember(attrs, "POSITION", -1));
            if (!positions.valid || positions.num_components < 3) continue;
            const AccessorView normals = reader.accessor(intMember(attrs, "NORMAL", -1));
            const AccessorView uvs = reader.accessor(intMember(attrs, "TEXCOORD_0", -1));
            const AccessorView uvs1 = reader.accessor(intMember(attrs, "TEXCOORD_1", -1));
            const AccessorView uvs2 = reader.accessor(intMember(attrs, "TEXCOORD_2", -1));
            const AccessorView colors = reader.accessor(intMember(attrs, "COLOR_0", -1));
            const AccessorView joints = reader.accessor(intMember(attrs, "JOINTS_0", -1));
            const AccessorView weights = reader.accessor(intMember(attrs, "WEIGHTS_0", -1));
            AttendPokemonPrimitive primitive;
            primitive.material = intMember(&primitive_json, "material", -1);
            if (eye_mesh && primitive.material >= 0 &&
                primitive.material < static_cast<int>(out.materials.size()) &&
                out.materials[static_cast<std::size_t>(primitive.material)].has_emissive_texture &&
                out.materials[static_cast<std::size_t>(primitive.material)].material_role == AttendMaterialRole::None) {
                out.materials[static_cast<std::size_t>(primitive.material)].pokemon_eye = true;
            }
            if (mouth_mesh && primitive.material >= 0 &&
                primitive.material < static_cast<int>(out.materials.size())) {
                AttendPokemonMaterial& material = out.materials[static_cast<std::size_t>(primitive.material)];
                material.material_role = AttendMaterialRole::Mouth;
                applyDefaultGen7ExpressionSheet(material);
            }
            primitive.mesh_node = static_cast<int>(node_index);
            primitive.skin = skin_index;
            primitive.scene_order = static_cast<int>(node_index);
            if (node_index < out.nodes.size()) {
                const AttendPokemonNode& loaded_node = out.nodes[node_index];
                primitive.render_order = loaded_node.render_order;
                primitive.default_visible = loaded_node.default_visible;
                primitive.visible_for_forms = loaded_node.visible_for_forms;
            }
            primitive.vertices.reserve(static_cast<std::size_t>(positions.count));
            for (int i = 0; i < positions.count; ++i) {
                primitive.vertices.push_back(readVertex(reader, positions, normals, uvs, uvs1, uvs2, colors, joints, weights, i));
            }
            const AccessorView indices = reader.accessor(intMember(&primitive_json, "indices", -1));
            if (indices.valid) {
                primitive.indices.reserve(static_cast<std::size_t>(indices.count));
                for (int i = 0; i < indices.count; ++i) primitive.indices.push_back(GltfReader::readUint(indices, i));
            } else {
                primitive.indices.reserve(primitive.vertices.size());
                for (std::uint32_t i = 0; i < primitive.vertices.size(); ++i) primitive.indices.push_back(i);
            }
            out.primitives.push_back(std::move(primitive));
        }
    }
}

void readAnimations(const JsonValue& root, const GltfReader& reader, AttendPokemonModel& out) {
    const JsonValue* animations = root.get("animations");
    if (!animations || !animations->isArray()) return;
    for (const JsonValue& anim_json : animations->asArray()) {
        AttendPokemonAnimation anim;
        anim.name = stringMember(&anim_json, "name");
        const JsonValue* samplers = anim_json.get("samplers");
        const JsonValue* channels = anim_json.get("channels");
        if (!samplers || !samplers->isArray() || !channels || !channels->isArray()) {
            out.animations.push_back(std::move(anim));
            continue;
        }
        for (const JsonValue& channel_json : channels->asArray()) {
            const int sampler_index = intMember(&channel_json, "sampler", -1);
            if (sampler_index < 0 || sampler_index >= static_cast<int>(samplers->asArray().size())) continue;
            const JsonValue& sampler = samplers->asArray()[static_cast<std::size_t>(sampler_index)];
            const JsonValue* target = channel_json.get("target");
            if (!target || !target->isObject()) continue;
            AttendPokemonAnimationChannel channel;
            channel.target_node = intMember(target, "node", -1);
            const std::string path = stringMember(target, "path");
            if (path == "rotation") {
                channel.path = AttendPokemonAnimationChannel::Path::Rotation;
            } else if (path == "scale") {
                channel.path = AttendPokemonAnimationChannel::Path::Scale;
            } else {
                channel.path = AttendPokemonAnimationChannel::Path::Translation;
            }
            channel.times = readScalarSeries(reader, intMember(&sampler, "input", -1));
            channel.values = readVecSeries(reader, intMember(&sampler, "output", -1));
            if (!channel.times.empty() && channel.times.size() == channel.values.size()) {
                anim.duration_seconds = std::max(anim.duration_seconds, channel.times.back());
                anim.channels.push_back(std::move(channel));
            }
        }
        out.animations.push_back(std::move(anim));
    }
}

std::vector<AttendEnvironmentState> readEnvironmentStates(const JsonValue* states, const char* key) {
    std::vector<AttendEnvironmentState> out;
    if (!states || !states->isObject()) return out;
    const JsonValue* entries = states->get(key);
    if (!entries || !entries->isArray()) return out;
    for (const JsonValue& value : entries->asArray()) {
        if (!value.isObject()) continue;
        AttendEnvironmentState state;
        state.id = stringMember(&value, "id");
        state.available = boolMember(&value, "available", false);
        if (const JsonValue* poses = value.get("poses"); poses && poses->isArray()) {
            for (const JsonValue& pose_value : poses->asArray()) {
                if (!pose_value.isObject()) continue;
                AttendEnvironmentFixedPose pose;
                pose.clip = stringMember(&pose_value, "clip");
                pose.frame = std::max(0, intMember(&pose_value, "frame", 0));
                if (!pose.clip.empty()) state.poses.push_back(std::move(pose));
            }
        }
        state.active_clips = stringArrayMember(&value, "activeClips");
        if (!state.id.empty()) out.push_back(std::move(state));
    }
    return out;
}

void readEnvironmentScene(const JsonValue& root, AttendPokemonModel& out) {
    const JsonValue* rae = raeExtras(root);
    if (!rae) return;
    const JsonValue* scene = rae->get("environmentScene");
    if (!scene || !scene->isObject()) return;
    AttendEnvironmentScene& parsed = out.environment_scene;
    parsed.enabled = true;
    parsed.schema_version = intMember(scene, "schemaVersion", 0);
    parsed.id = stringMember(scene, "id");
    parsed.label = stringMember(scene, "label", parsed.id);
    parsed.default_time = stringMember(scene, "defaultTime", "day");
    parsed.default_weather = stringMember(scene, "defaultWeather", "clear");
    if (const JsonValue* source = scene->get("source"); source && source->isObject()) {
        parsed.composition_id = stringMember(source, "compositionId");
        if (const JsonValue* slots = source->get("slots"); slots && slots->isArray()) {
            for (const JsonValue& slot : slots->asArray()) {
                if (slot.isNumber()) parsed.source_slots.push_back(static_cast<int>(slot.asNumber()));
            }
        }
    }
    if (const JsonValue* anchor = scene->get("surfaceAnchor"); anchor && anchor->isObject()) {
        if (const JsonValue* x = anchor->get("x"); x && x->isNumber()) parsed.surface_anchor[0] = static_cast<float>(x->asNumber());
        if (const JsonValue* y = anchor->get("y"); y && y->isNumber()) parsed.surface_anchor[1] = static_cast<float>(y->asNumber());
        if (const JsonValue* z = anchor->get("z"); z && z->isNumber()) parsed.surface_anchor[2] = static_cast<float>(z->asNumber());
    }
    if (const JsonValue* states = scene->get("states"); states && states->isObject()) {
        if (const JsonValue* rate = states->get("sourceFrameRate"); rate && rate->isNumber()) {
            parsed.source_frame_rate = std::max(1.0f, static_cast<float>(rate->asNumber()));
        }
        parsed.ambient_clips = stringArrayMember(states, "ambientClips");
        parsed.time_states = readEnvironmentStates(states, "timeStates");
        parsed.weather_states = readEnvironmentStates(states, "weatherStates");
    }
    const JsonValue* motion = rae->get("mapMaterialMotion");
    if (!motion || !motion->isObject()) return;
    if (const JsonValue* rate = motion->get("frameRate"); rate && rate->isNumber()) {
        parsed.source_frame_rate = std::max(1.0f, static_cast<float>(rate->asNumber()));
    }
    parsed.default_clip = stringMember(motion, "defaultClip");
    parsed.overlay_clips = stringArrayMember(motion, "overlayClips");
    const JsonValue* clips = motion->get("clips");
    if (!clips || !clips->isArray()) return;
    for (const JsonValue& clip_value : clips->asArray()) {
        if (!clip_value.isObject()) continue;
        AttendEnvironmentMotionClip clip;
        clip.id = stringMember(&clip_value, "id");
        clip.frame_count = std::max(0, intMember(&clip_value, "frameCount", 0));
        clip.loop = boolMember(&clip_value, "loop", true);
        if (const JsonValue* tracks = clip_value.get("tracks"); tracks && tracks->isArray()) {
            for (const JsonValue& track_value : tracks->asArray()) {
                if (!track_value.isObject()) continue;
                AttendEnvironmentMotionTrack track;
                track.material = stringMember(&track_value, "material");
                track.motion_kind = stringMember(&track_value, "motionKind");
                track.texture_unit = std::clamp(intMember(&track_value, "textureUnit", 0), 0, 2);
                if (const JsonValue* offsets = track_value.get("frameOffsets"); offsets && offsets->isArray()) {
                    track.frame_offsets.reserve(offsets->asArray().size());
                    for (const JsonValue& offset : offsets->asArray()) {
                        track.frame_offsets.push_back(readFloatPair(&offset));
                    }
                }
                if (!track.material.empty() && !track.frame_offsets.empty()) clip.tracks.push_back(std::move(track));
            }
        }
        if (const JsonValue* visibility = clip_value.get("meshVisibility"); visibility && visibility->isObject()) {
            for (const auto& [node_name, values] : visibility->asObject()) {
                if (!values.isArray()) continue;
                std::vector<bool> frames;
                frames.reserve(values.asArray().size());
                for (const JsonValue& frame : values.asArray()) {
                    frames.push_back(frame.isBool() ? frame.asBool() : true);
                }
                if (!frames.empty()) clip.mesh_visibility.emplace_back(node_name, std::move(frames));
            }
        }
        if (!clip.id.empty()) parsed.motion_clips.push_back(std::move(clip));
    }
}

bool supportedTevSource(std::uint32_t source) {
    switch (source & 15u) {
        case 0u:
        case 1u:
        case 2u:
        case 3u:
        case 4u:
        case 5u:
        case 13u:
        case 14u:
        case 15u:
            return true;
        default:
            return false;
    }
}

bool validateEnvironmentMaterials(const AttendPokemonModel& model, std::string& message) {
    if (!model.environment_scene.enabled) return true;
    for (const AttendPokemonMaterial& material : model.materials) {
        if (!material.pica_tev.enabled) continue;
        for (const AttendPicaTevStage& stage : material.pica_tev.stages) {
            for (int arg = 0; arg < 3; ++arg) {
                if (!supportedTevSource((stage.source >> (arg * 4)) & 15u) ||
                    !supportedTevSource((stage.source >> (16 + arg * 4)) & 15u)) {
                    message = "unsupported PICA TEV source in material " + material.name;
                    return false;
                }
            }
            if ((stage.combiner & 15u) > 9u || ((stage.combiner >> 16) & 15u) > 9u) {
                message = "unsupported PICA TEV combiner in material " + material.name;
                return false;
            }
        }
    }
    return true;
}

std::array<float, 4> sampleChannel(const AttendPokemonAnimationChannel& channel, float time_seconds) {
    if (channel.times.empty() || channel.values.empty()) return {0.0f, 0.0f, 0.0f, 1.0f};
    if (time_seconds <= channel.times.front()) return channel.values.front();
    if (time_seconds >= channel.times.back()) return channel.values.back();
    auto it = std::upper_bound(channel.times.begin(), channel.times.end(), time_seconds);
    const std::size_t next = static_cast<std::size_t>(std::distance(channel.times.begin(), it));
    const std::size_t prev = next > 0 ? next - 1 : 0;
    const float start = channel.times[prev];
    const float end = channel.times[next];
    const float t = (end > start) ? (time_seconds - start) / (end - start) : 0.0f;
    if (channel.path == AttendPokemonAnimationChannel::Path::Rotation) {
        return slerp(channel.values[prev], channel.values[next], t);
    }
    std::array<float, 4> out{};
    for (int i = 0; i < 4; ++i) {
        out[static_cast<std::size_t>(i)] =
            channel.values[prev][static_cast<std::size_t>(i)] +
            (channel.values[next][static_cast<std::size_t>(i)] - channel.values[prev][static_cast<std::size_t>(i)]) * t;
    }
    return out;
}

bool containsAny(const std::string& value, const std::vector<std::string>& needles) {
    const std::string lower_value = lowerAscii(value);
    for (const std::string& needle : needles) {
        if (!needle.empty() && lower_value.find(lowerAscii(needle)) != std::string::npos) return true;
    }
    return false;
}

bool equalsAny(const std::string& value, const std::vector<std::string>& candidates) {
    const std::string lower_value = lowerAscii(value);
    for (const std::string& candidate : candidates) {
        if (!candidate.empty() && lower_value == lowerAscii(candidate)) return true;
    }
    return false;
}

bool stringListContains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

std::string nodeFormSuffix(const std::string& node_name) {
    const std::string marker = "__form_";
    const std::size_t marker_pos = node_name.rfind(marker);
    if (marker_pos == std::string::npos) return {};
    std::string suffix;
    for (std::size_t i = marker_pos + marker.size(); i < node_name.size(); ++i) {
        const char c = node_name[i];
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        suffix.push_back(c);
    }
    return suffix;
}

int formSuffixIndex(const std::string& suffix) {
    if (suffix.empty()) return -1;
    int value = 0;
    for (const char c : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

int defaultFormVariantIndex(const AttendPokemonModel& model) {
    if (model.form_variants.empty()) return -1;
    for (std::size_t i = 0; i < model.form_variants.size(); ++i) {
        if (model.form_variants[i].id == model.default_form_variant) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

bool animationChannelMatchesForm(
    const AttendPokemonModel& model,
    const AttendPokemonAnimationChannel& channel,
    int active_form_index,
    const std::string& active_form_id,
    int default_form_index) {
    if (channel.target_node < 0 || channel.target_node >= static_cast<int>(model.nodes.size())) return false;
    const AttendPokemonNode& node = model.nodes[static_cast<std::size_t>(channel.target_node)];
    if (!node.visible_for_forms.empty()) {
        return !active_form_id.empty() && stringListContains(node.visible_for_forms, active_form_id);
    }
    const std::string suffix = nodeFormSuffix(node.name);
    if (!suffix.empty()) {
        if (!active_form_id.empty() && suffix == active_form_id) return true;
        return formSuffixIndex(suffix) == active_form_index;
    }
    return active_form_index == default_form_index;
}

bool animationNameContains(const AttendPokemonAnimation& animation, const std::vector<std::string>& needles) {
    const std::string lower = lowerAscii(animation.name);
    for (const std::string& needle : needles) {
        if (!needle.empty() && lower.find(lowerAscii(needle)) != std::string::npos) return true;
    }
    return false;
}

bool overlayTargetsNode(
    const AttendPokemonModel& model,
    const AttendPokemonAnimationChannel& channel,
    const AttendPokemonPoseOverlay& overlay) {
    if (channel.target_node < 0 || channel.target_node >= static_cast<int>(model.nodes.size())) return false;
    if (!overlay.eyelids_only) return true;
    const std::string& name = model.nodes[static_cast<std::size_t>(channel.target_node)].name;
    return containsAny(name, overlay.eyelid_node_substrings);
}

void computeGlobalNode(
    const AttendPokemonModel& model,
    const std::vector<std::array<float, 16>>& local,
    std::vector<std::array<float, 16>>& globals,
    std::vector<std::uint8_t>& state,
    std::size_t node_index) {
    if (node_index >= model.nodes.size()) return;
    if (state[node_index] == 2) return;
    if (state[node_index] == 1) {
        globals[node_index] = local[node_index];
        state[node_index] = 2;
        return;
    }
    state[node_index] = 1;
    const int parent = model.nodes[node_index].parent;
    if (parent >= 0 && parent < static_cast<int>(model.nodes.size())) {
        const std::size_t parent_index = static_cast<std::size_t>(parent);
        computeGlobalNode(model, local, globals, state, parent_index);
        globals[node_index] = multiply(globals[parent_index], local[node_index]);
    } else {
        globals[node_index] = local[node_index];
    }
    state[node_index] = 2;
}

std::vector<std::array<float, 16>> computeAnimatedGlobals(
    const AttendPokemonModel& model,
    const AttendPokemonAnimation* animation,
    double scene_time_seconds,
    float animation_loop_duration_seconds,
    AttendPokemonPoseOverlay overlay = {}) {
    std::vector<std::array<float, 3>> translations(model.nodes.size(), {0.0f, 0.0f, 0.0f});
    std::vector<std::array<float, 4>> rotations(model.nodes.size(), {0.0f, 0.0f, 0.0f, 1.0f});
    std::vector<std::array<float, 3>> scales(model.nodes.size(), {1.0f, 1.0f, 1.0f});
    std::vector<bool> has_tr(model.nodes.size(), false);
    std::vector<bool> has_rot(model.nodes.size(), false);
    std::vector<bool> has_sc(model.nodes.size(), false);
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        translations[i] = model.nodes[i].base_translation;
        rotations[i] = model.nodes[i].base_rotation;
        scales[i] = model.nodes[i].base_scale;
    }

    if (animation && animation->duration_seconds > 0.0f) {
        const float loop_duration = animation_loop_duration_seconds > 0.0f
            ? animation_loop_duration_seconds
            : animation->duration_seconds;
        const float t = std::fmod(static_cast<float>(scene_time_seconds), loop_duration);
        for (const AttendPokemonAnimationChannel& channel : animation->channels) {
            if (channel.target_node < 0 || channel.target_node >= static_cast<int>(model.nodes.size())) continue;
            const std::array<float, 4> value = sampleChannel(channel, t);
            const std::size_t node = static_cast<std::size_t>(channel.target_node);
            if (channel.path == AttendPokemonAnimationChannel::Path::Translation) {
                translations[node] = {value[0], value[1], value[2]};
                has_tr[node] = true;
            } else if (channel.path == AttendPokemonAnimationChannel::Path::Rotation) {
                rotations[node] = value;
                has_rot[node] = true;
            } else {
                scales[node] = {value[0], value[1], value[2]};
                has_sc[node] = true;
            }
        }
    }
    if (overlay.animation && overlay.animation->duration_seconds > 0.0f && overlay.weight > 0.0f) {
        const float duration = std::max(0.001f, overlay.animation->duration_seconds);
        float overlay_t = std::fmod(static_cast<float>(std::max(0.0, overlay.time_seconds)), duration);
        if (overlay.reverse) {
            overlay_t = std::max(0.0f, duration - overlay_t);
        }
        const float weight = std::clamp(overlay.weight, 0.0f, 1.0f);
        for (const AttendPokemonAnimationChannel& channel : overlay.animation->channels) {
            if (!overlayTargetsNode(model, channel, overlay)) continue;
            const std::array<float, 4> value = sampleChannel(channel, overlay_t);
            const std::size_t node = static_cast<std::size_t>(channel.target_node);
            if (channel.path == AttendPokemonAnimationChannel::Path::Translation) {
                for (int i = 0; i < 3; ++i) {
                    translations[node][static_cast<std::size_t>(i)] +=
                        (value[static_cast<std::size_t>(i)] - translations[node][static_cast<std::size_t>(i)]) * weight;
                }
                has_tr[node] = true;
            } else if (channel.path == AttendPokemonAnimationChannel::Path::Rotation) {
                rotations[node] = slerp(rotations[node], value, weight);
                has_rot[node] = true;
            } else {
                for (int i = 0; i < 3; ++i) {
                    scales[node][static_cast<std::size_t>(i)] +=
                        (value[static_cast<std::size_t>(i)] - scales[node][static_cast<std::size_t>(i)]) * weight;
                }
                has_sc[node] = true;
            }
        }
    }
    if (overlay.head_weight > 0.0f &&
        (std::abs(overlay.head_yaw_degrees) > 0.001f || std::abs(overlay.head_pitch_degrees) > 0.001f)) {
        const float weight = std::clamp(overlay.head_weight, 0.0f, 1.0f);
        const std::array<float, 4> yaw = axisAngle(
            overlay.head_yaw_axis[0],
            overlay.head_yaw_axis[1],
            overlay.head_yaw_axis[2],
            overlay.head_yaw_degrees * (kPi / 180.0f));
        const std::array<float, 4> pitch = axisAngle(
            overlay.head_pitch_axis[0],
            overlay.head_pitch_axis[1],
            overlay.head_pitch_axis[2],
            overlay.head_pitch_degrees * (kPi / 180.0f));
        const std::array<float, 4> look = multiplyQuat(yaw, pitch);
        for (std::size_t node = 0; node < model.nodes.size(); ++node) {
            if (!equalsAny(model.nodes[node].name, overlay.head_node_names)) continue;
            rotations[node] = slerp(rotations[node], multiplyQuat(look, rotations[node]), weight);
            has_rot[node] = true;
        }
    }

    std::vector<std::array<float, 16>> local(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        local[i] = (has_tr[i] || has_rot[i] || has_sc[i] || !model.nodes[i].has_matrix)
            ? trsMatrix(translations[i], rotations[i], scales[i])
            : model.nodes[i].local_matrix;
    }
    std::vector<std::array<float, 16>> globals(model.nodes.size(), identity());
    std::vector<std::uint8_t> state(model.nodes.size(), 0);
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        computeGlobalNode(model, local, globals, state, i);
    }
    return globals;
}

} // namespace

AttendPokemonModel loadAttendPokemonModel(const std::string& path, std::string* error) {
    AttendPokemonModel out;
    std::vector<std::uint8_t> bytes;
    if (!loadAttendPokemonAssetBytes(path, bytes, error)) return out;
    if (bytes.size() < 20) {
        fail(error, "Attend Pokemon GLB too small: " + path);
        return out;
    }
    if (readU32Le(bytes.data()) != kGlbMagic || readU32Le(bytes.data() + 4) != 2u) {
        fail(error, "Unsupported attend Pokemon GLB: " + path);
        return out;
    }

    std::string json_text;
    const std::uint8_t* bin = nullptr;
    std::size_t bin_len = 0;
    std::size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const std::uint32_t chunk_len = readU32Le(bytes.data() + offset);
        const std::uint32_t chunk_type = readU32Le(bytes.data() + offset + 4);
        const std::size_t chunk_start = offset + 8;
        if (chunk_start + chunk_len > bytes.size()) break;
        if (chunk_type == kChunkJson) {
            json_text.assign(reinterpret_cast<const char*>(bytes.data() + chunk_start), chunk_len);
        } else if (chunk_type == kChunkBin) {
            bin = bytes.data() + chunk_start;
            bin_len = chunk_len;
        }
        offset = chunk_start + chunk_len;
    }
    if (json_text.empty() || !bin) {
        fail(error, "Attend Pokemon GLB missing JSON or BIN chunk: " + path);
        return out;
    }

    const JsonValue root = parseJsonText(json_text);
    if (!root.isObject()) {
        fail(error, "Attend Pokemon GLB JSON is invalid: " + path);
        return out;
    }
    const GltfReader reader(root, bin, bin_len);
    readTextureVariants(root, out);
    readEnvironmentScene(root, out);
    readMaterials(root, bin, bin_len, out);
    readNodes(root, out);
    readSkins(root, reader, out);
    readPrimitives(root, reader, out);
    readAnimations(root, reader, out);
    std::string environment_error;
    if (!validateEnvironmentMaterials(out, environment_error)) {
        fail(error, "Attend environment import failed: " + environment_error + " (" + path + ")");
        return AttendPokemonModel{};
    }
    out.valid = !out.nodes.empty() && !out.materials.empty() && !out.primitives.empty();
    if (!out.valid) {
        fail(error, "Attend Pokemon GLB did not produce renderable model: " + path);
    }
    return out;
}

std::shared_ptr<const AttendPokemonModel> loadAttendPokemonModelShared(
    const std::string& path,
    std::string* error) {
    struct CacheEntry {
        std::filesystem::file_time_type write_time{};
        bool write_time_known = false;
        std::shared_ptr<const AttendPokemonModel> model;
    };
    static std::mutex mutex;
    static std::unordered_map<std::string, CacheEntry> cache;

    std::error_code time_error;
    const auto write_time = std::filesystem::last_write_time(path, time_error);
    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto found = cache.find(path);
        if (found != cache.end() && found->second.model &&
            ((!time_error && found->second.write_time_known &&
              found->second.write_time == write_time) ||
             (time_error && !found->second.write_time_known))) {
            if (error) error->clear();
            return found->second.model;
        }
    }

    std::string load_error;
    AttendPokemonModel decoded = loadAttendPokemonModel(path, &load_error);
    if (!decoded.valid) {
        if (error) *error = std::move(load_error);
        return {};
    }
    auto shared = std::make_shared<const AttendPokemonModel>(std::move(decoded));
    {
        const std::lock_guard<std::mutex> lock(mutex);
        cache[path] = CacheEntry{write_time, !time_error, shared};
    }
    if (error) error->clear();
    return shared;
}

const AttendPokemonAnimation* findAttendPokemonAnimation(
    const AttendPokemonModel& model,
    const std::string& animation_name) {
    if (model.animations.empty()) return nullptr;
    for (const AttendPokemonAnimation& animation : model.animations) {
        if (animation.name == animation_name) return &animation;
    }
    if (!animation_name.empty()) {
        for (const AttendPokemonAnimation& animation : model.animations) {
            if (animationNameContains(animation, {animation_name})) return &animation;
        }
    }
    for (const AttendPokemonAnimation& animation : model.animations) {
        if (animationNameContains(animation, {"defaultwait", "slot4_00", "slot5_00", "slot6_00", "wait", "idle", "stand", "slot0"})) return &animation;
    }
    return &model.animations.front();
}

float attendPokemonAnimationLoopDurationForForm(
    const AttendPokemonModel& model,
    const AttendPokemonAnimation* animation,
    int form_variant_index) {
    if (!animation || animation->duration_seconds <= 0.0f || model.form_variants.empty()) {
        return animation ? animation->duration_seconds : 0.0f;
    }
    const int form_count = static_cast<int>(model.form_variants.size());
    const int active_form_index = form_variant_index >= 0 && form_variant_index < form_count
        ? form_variant_index
        : defaultFormVariantIndex(model);
    if (active_form_index < 0 || active_form_index >= form_count) return animation->duration_seconds;
    const std::string& active_form_id = model.form_variants[static_cast<std::size_t>(active_form_index)].id;
    const int default_form_index = defaultFormVariantIndex(model);
    bool matched_channel = false;
    float duration_seconds = 0.0f;
    for (const AttendPokemonAnimationChannel& channel : animation->channels) {
        if (channel.times.empty()) continue;
        if (!animationChannelMatchesForm(
                model,
                channel,
                active_form_index,
                active_form_id,
                default_form_index)) {
            continue;
        }
        matched_channel = true;
        duration_seconds = std::max(duration_seconds, channel.times.back());
    }
    return matched_channel && duration_seconds > 0.0f ? duration_seconds : animation->duration_seconds;
}

std::vector<std::array<float, 16>> buildAttendPokemonGlobals(
    const AttendPokemonModel& model,
    const AttendPokemonAnimation* animation,
    double scene_time_seconds,
    AttendPokemonPoseOverlay overlay) {
    return buildAttendPokemonGlobals(model, animation, scene_time_seconds, 0.0f, std::move(overlay));
}

std::vector<std::array<float, 16>> buildAttendPokemonGlobals(
    const AttendPokemonModel& model,
    const AttendPokemonAnimation* animation,
    double scene_time_seconds,
    float animation_loop_duration_seconds,
    AttendPokemonPoseOverlay overlay) {
    return computeAnimatedGlobals(
        model,
        animation,
        scene_time_seconds,
        animation_loop_duration_seconds,
        std::move(overlay));
}

std::vector<std::vector<std::array<float, 16>>> buildAttendPokemonSkinMatrices(
    const AttendPokemonModel& model,
    const std::vector<std::array<float, 16>>& globals) {
    std::vector<std::vector<std::array<float, 16>>> out;
    out.resize(model.skins.size());
    for (std::size_t skin_index = 0; skin_index < model.skins.size(); ++skin_index) {
        const AttendPokemonSkin& skin = model.skins[skin_index];
        out[skin_index].resize(skin.joints.size(), identity());
        for (std::size_t joint_slot = 0; joint_slot < skin.joints.size(); ++joint_slot) {
            const int joint_node = skin.joints[joint_slot];
            if (joint_node < 0 || joint_node >= static_cast<int>(globals.size()) ||
                joint_slot >= skin.inverse_bind_matrices.size()) {
                continue;
            }
            out[skin_index][joint_slot] =
                multiply(globals[static_cast<std::size_t>(joint_node)], skin.inverse_bind_matrices[joint_slot]);
        }
    }
    return out;
}

void skinAttendPokemonPrimitiveWithPose(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive,
    const std::vector<std::array<float, 16>>& globals,
    const std::vector<std::vector<std::array<float, 16>>>& skin_matrices,
    std::vector<AttendPokemonVertex>& out_vertices) {
    out_vertices.resize(primitive.vertices.size());
    if (primitive.skin < 0 || primitive.skin >= static_cast<int>(model.skins.size())) {
        if (primitive.mesh_node >= 0 && primitive.mesh_node < static_cast<int>(globals.size())) {
            const auto& mesh_global = globals[static_cast<std::size_t>(primitive.mesh_node)];
            for (std::size_t i = 0; i < primitive.vertices.size(); ++i) {
                const AttendPokemonVertex& src = primitive.vertices[i];
                AttendPokemonVertex& dst = out_vertices[i];
                dst = src;
                transformPoint(mesh_global, src.x, src.y, src.z, dst.x, dst.y, dst.z);
                transformVector(mesh_global, src.nx, src.ny, src.nz, dst.nx, dst.ny, dst.nz);
                normalizeVector(dst.nx, dst.ny, dst.nz);
            }
        } else {
            out_vertices = primitive.vertices;
        }
        return;
    }
    const AttendPokemonSkin& skin = model.skins[static_cast<std::size_t>(primitive.skin)];
    if (skin.joints.empty()) {
        out_vertices = primitive.vertices;
        return;
    }
    const std::vector<std::array<float, 16>>* matrices = nullptr;
    if (primitive.skin >= 0 && primitive.skin < static_cast<int>(skin_matrices.size())) {
        matrices = &skin_matrices[static_cast<std::size_t>(primitive.skin)];
    }
    for (std::size_t i = 0; i < primitive.vertices.size(); ++i) {
        const AttendPokemonVertex& src = primitive.vertices[i];
        AttendPokemonVertex& dst = out_vertices[i];
        dst = src;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        float total_weight = 0.0f;
        for (int c = 0; c < 4; ++c) {
            const float weight = src.weights[static_cast<std::size_t>(c)];
            if (weight <= 0.0f) continue;
            const std::uint16_t joint_slot = src.joints[static_cast<std::size_t>(c)];
            if (!matrices || joint_slot >= matrices->size()) continue;
            const std::array<float, 16>& joint_matrix = (*matrices)[static_cast<std::size_t>(joint_slot)];
            float tx = 0.0f;
            float ty = 0.0f;
            float tz = 0.0f;
            transformPoint(joint_matrix, src.x, src.y, src.z, tx, ty, tz);
            float tnx = 0.0f;
            float tny = 0.0f;
            float tnz = 0.0f;
            transformVector(joint_matrix, src.nx, src.ny, src.nz, tnx, tny, tnz);
            x += tx * weight;
            y += ty * weight;
            z += tz * weight;
            nx += tnx * weight;
            ny += tny * weight;
            nz += tnz * weight;
            total_weight += weight;
        }
        if (total_weight > 0.0f) {
            dst.x = x;
            dst.y = y;
            dst.z = z;
            dst.nx = nx;
            dst.ny = ny;
            dst.nz = nz;
            normalizeVector(dst.nx, dst.ny, dst.nz);
        }
    }
}

void skinAttendPokemonPrimitive(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive,
    const AttendPokemonAnimation* animation,
    double scene_time_seconds,
    std::vector<AttendPokemonVertex>& out_vertices,
    AttendPokemonPoseOverlay overlay) {
    const std::vector<std::array<float, 16>> globals =
        buildAttendPokemonGlobals(model, animation, scene_time_seconds, std::move(overlay));
    const std::vector<std::vector<std::array<float, 16>>> skin_matrices =
        buildAttendPokemonSkinMatrices(model, globals);
    skinAttendPokemonPrimitiveWithPose(model, primitive, globals, skin_matrices, out_vertices);
}

} // namespace pr::gameplay::attend::rendering
