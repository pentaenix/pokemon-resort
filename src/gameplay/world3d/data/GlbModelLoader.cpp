#include "gameplay/world3d/data/GlbModelLoader.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>

namespace pr::gameplay::world3d::data {

namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67u;   // "glTF"
constexpr std::uint32_t kChunkJson = 0x4E4F534Au;  // "JSON"
constexpr std::uint32_t kChunkBin = 0x004E4942u;   // "BIN\0"

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

// Column-major 4x4, matching glTF's node.matrix layout.
struct Mat4 {
    std::array<float, 16> m{};
    static Mat4 identity() {
        Mat4 r;
        r.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        return r;
    }
};

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[static_cast<std::size_t>(k * 4 + row)] *
                       b.m[static_cast<std::size_t>(col * 4 + k)];
            }
            r.m[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return r;
}

void transformPoint(const Mat4& mat, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = mat.m[0] * x + mat.m[4] * y + mat.m[8] * z + mat.m[12];
    oy = mat.m[1] * x + mat.m[5] * y + mat.m[9] * z + mat.m[13];
    oz = mat.m[2] * x + mat.m[6] * y + mat.m[10] * z + mat.m[14];
}

Mat4 trsMatrix(const std::array<float, 3>& t, const std::array<float, 4>& q, const std::array<float, 3>& s) {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 r = Mat4::identity();
    r.m[0] = (1.0f - 2.0f * (yy + zz)) * s[0];
    r.m[1] = (2.0f * (xy + wz)) * s[0];
    r.m[2] = (2.0f * (xz - wy)) * s[0];
    r.m[4] = (2.0f * (xy - wz)) * s[1];
    r.m[5] = (1.0f - 2.0f * (xx + zz)) * s[1];
    r.m[6] = (2.0f * (yz + wx)) * s[1];
    r.m[8] = (2.0f * (xz + wy)) * s[2];
    r.m[9] = (2.0f * (yz - wx)) * s[2];
    r.m[10] = (1.0f - 2.0f * (xx + yy)) * s[2];
    r.m[12] = t[0];
    r.m[13] = t[1];
    r.m[14] = t[2];
    return r;
}

Mat4 nodeLocalMatrix(const JsonValue& node) {
    if (const JsonValue* matrix = node.get("matrix"); matrix && matrix->isArray()) {
        const auto& arr = matrix->asArray();
        if (arr.size() == 16) {
            Mat4 r;
            for (std::size_t i = 0; i < 16; ++i) {
                r.m[i] = arr[i].isNumber() ? static_cast<float>(arr[i].asNumber()) : 0.0f;
            }
            return r;
        }
    }
    std::array<float, 3> t{0.0f, 0.0f, 0.0f};
    std::array<float, 4> q{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> s{1.0f, 1.0f, 1.0f};
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
    return trsMatrix(t, q, s);
}

int componentByteSize(int component_type) {
    switch (component_type) {
        case 5120: case 5121: return 1; // byte / unsigned byte
        case 5122: case 5123: return 2; // short / unsigned short
        case 5125: case 5126: return 4; // unsigned int / float
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

// View onto an accessor's raw bytes within the BIN chunk.
struct AccessorView {
    const std::uint8_t* base = nullptr;
    int count = 0;
    int component_type = 0;
    int num_components = 0;
    std::size_t stride = 0;
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
        const std::string type = acc.get("type") && acc.get("type")->isString() ? acc.get("type")->asString() : "";
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
        const std::size_t span = (count > 0)
            ? (static_cast<std::size_t>(count - 1) * stride + static_cast<std::size_t>(comp_size * num_comp))
            : 0;
        if (start + span > bin_len_) return view;

        view.base = bin_ + start;
        view.count = count;
        view.component_type = component_type;
        view.num_components = num_comp;
        view.stride = stride;
        view.valid = true;
        return view;
    }

    static float readFloat(const AccessorView& v, int element, int comp) {
        const std::uint8_t* p = v.base + static_cast<std::size_t>(element) * v.stride +
                                static_cast<std::size_t>(comp) * 4u;
        float out = 0.0f;
        std::memcpy(&out, p, sizeof(float));
        return out;
    }

    static std::uint32_t readIndex(const AccessorView& v, int element) {
        const std::uint8_t* p = v.base + static_cast<std::size_t>(element) * v.stride;
        switch (v.component_type) {
            case 5121: return static_cast<std::uint32_t>(p[0]);
            case 5123: {
                std::uint16_t out = 0;
                std::memcpy(&out, p, sizeof(out));
                return out;
            }
            case 5125: {
                std::uint32_t out = 0;
                std::memcpy(&out, p, sizeof(out));
                return out;
            }
            default: return 0;
        }
    }

private:
    const JsonValue& root_;
    const std::uint8_t* bin_;
    std::size_t bin_len_;
};

// Resolve a primitive's material into a GlbMaterial, deduplicating by glTF material index.
int resolveMaterial(
    const JsonValue& root,
    const std::uint8_t* bin,
    std::size_t bin_len,
    int material_index,
    std::vector<int>& material_remap,
    GlbMesh& out) {
    if (material_index < 0) return -1;
    if (material_index < static_cast<int>(material_remap.size()) && material_remap[static_cast<std::size_t>(material_index)] >= 0) {
        return material_remap[static_cast<std::size_t>(material_index)];
    }

    const JsonValue* materials = root.get("materials");
    if (!materials || !materials->isArray()) return -1;
    const auto& mat_arr = materials->asArray();
    if (material_index >= static_cast<int>(mat_arr.size())) return -1;
    const JsonValue& mat = mat_arr[static_cast<std::size_t>(material_index)];

    GlbMaterial gmat;
    if (const JsonValue* pbr = mat.get("pbrMetallicRoughness"); pbr && pbr->isObject()) {
        if (const JsonValue* bcf = pbr->get("baseColorFactor"); bcf && bcf->isArray()) {
            const auto& arr = bcf->asArray();
            for (std::size_t i = 0; i < 4 && i < arr.size(); ++i) {
                if (arr[i].isNumber()) gmat.base_color[i] = static_cast<float>(arr[i].asNumber());
            }
        }
        if (const JsonValue* bct = pbr->get("baseColorTexture"); bct && bct->isObject()) {
            const int tex_index = intMember(bct, "index", -1);
            const JsonValue* textures = root.get("textures");
            const JsonValue* images = root.get("images");
            const JsonValue* buffer_views = root.get("bufferViews");
            if (tex_index >= 0 && textures && textures->isArray() && images && images->isArray() &&
                buffer_views && buffer_views->isArray()) {
                const auto& tex_arr = textures->asArray();
                if (tex_index < static_cast<int>(tex_arr.size())) {
                    const int src = intMember(&tex_arr[static_cast<std::size_t>(tex_index)], "source", -1);
                    const auto& img_arr = images->asArray();
                    if (src >= 0 && src < static_cast<int>(img_arr.size())) {
                        const int bv_index = intMember(&img_arr[static_cast<std::size_t>(src)], "bufferView", -1);
                        const auto& bv_arr = buffer_views->asArray();
                        if (bv_index >= 0 && bv_index < static_cast<int>(bv_arr.size())) {
                            const JsonValue& bv = bv_arr[static_cast<std::size_t>(bv_index)];
                            const std::size_t off = static_cast<std::size_t>(intMember(&bv, "byteOffset", 0));
                            const std::size_t len = static_cast<std::size_t>(intMember(&bv, "byteLength", 0));
                            if (len > 0 && off + len <= bin_len) {
                                gmat.image_bytes.assign(bin + off, bin + off + len);
                                gmat.has_texture = true;
                            }
                        }
                    }
                }
            }
        }
    }
    if (const JsonValue* alpha = mat.get("alphaMode"); alpha && alpha->isString()) {
        const std::string mode = alpha->asString();
        gmat.alpha_blend = (mode == "MASK" || mode == "BLEND");
    }

    const int local_index = static_cast<int>(out.materials.size());
    out.materials.push_back(std::move(gmat));
    if (material_index >= static_cast<int>(material_remap.size())) {
        material_remap.resize(static_cast<std::size_t>(material_index) + 1, -1);
    }
    material_remap[static_cast<std::size_t>(material_index)] = local_index;
    return local_index;
}

void appendPrimitive(
    const JsonValue& root,
    const GltfReader& reader,
    const JsonValue& primitive,
    const Mat4& world,
    int local_material,
    GlbMesh& out) {
    const JsonValue* attributes = primitive.get("attributes");
    if (!attributes || !attributes->isObject()) return;
    const int pos_acc = intMember(attributes, "POSITION", -1);
    const int uv_acc = intMember(attributes, "TEXCOORD_0", -1);
    if (pos_acc < 0) return;

    const AccessorView positions = reader.accessor(pos_acc);
    if (!positions.valid || positions.num_components < 3) return;
    const AccessorView uvs = uv_acc >= 0 ? reader.accessor(uv_acc) : AccessorView{};
    const bool has_uv = uvs.valid && uvs.num_components >= 2;

    const auto build_vertex = [&](int vertex_index) {
        GlbVertex v;
        const float lx = GltfReader::readFloat(positions, vertex_index, 0);
        const float ly = GltfReader::readFloat(positions, vertex_index, 1);
        const float lz = GltfReader::readFloat(positions, vertex_index, 2);
        transformPoint(world, lx, ly, lz, v.x, v.y, v.z);
        if (has_uv && vertex_index < uvs.count) {
            v.u = GltfReader::readFloat(uvs, vertex_index, 0);
            v.v = GltfReader::readFloat(uvs, vertex_index, 1);
        }
        out.aabb_min[0] = std::min(out.aabb_min[0], v.x);
        out.aabb_min[1] = std::min(out.aabb_min[1], v.y);
        out.aabb_min[2] = std::min(out.aabb_min[2], v.z);
        out.aabb_max[0] = std::max(out.aabb_max[0], v.x);
        out.aabb_max[1] = std::max(out.aabb_max[1], v.y);
        out.aabb_max[2] = std::max(out.aabb_max[2], v.z);
        return v;
    };

    const int mode = intMember(&primitive, "mode", 4); // default TRIANGLES
    if (mode != 4) return; // only triangle lists are produced by our exporter

    const int indices_acc = intMember(&primitive, "indices", -1);
    if (indices_acc >= 0) {
        const AccessorView indices = reader.accessor(indices_acc);
        if (!indices.valid) return;
        for (int i = 0; i + 2 < indices.count; i += 3) {
            const std::uint32_t ia = GltfReader::readIndex(indices, i);
            const std::uint32_t ib = GltfReader::readIndex(indices, i + 1);
            const std::uint32_t ic = GltfReader::readIndex(indices, i + 2);
            if (static_cast<int>(ia) >= positions.count ||
                static_cast<int>(ib) >= positions.count ||
                static_cast<int>(ic) >= positions.count) {
                continue;
            }
            out.triangles.push_back(GlbTriangle{
                build_vertex(static_cast<int>(ia)),
                build_vertex(static_cast<int>(ib)),
                build_vertex(static_cast<int>(ic)),
                local_material});
        }
    } else {
        for (int i = 0; i + 2 < positions.count; i += 3) {
            out.triangles.push_back(GlbTriangle{
                build_vertex(i),
                build_vertex(i + 1),
                build_vertex(i + 2),
                local_material});
        }
    }
    (void)root;
}

} // namespace

GlbMesh loadGlbModel(const std::string& path, std::string* error) {
    GlbMesh out;
    out.aabb_min[0] = out.aabb_min[1] = out.aabb_min[2] = std::numeric_limits<float>::max();
    out.aabb_max[0] = out.aabb_max[1] = out.aabb_max[2] = std::numeric_limits<float>::lowest();

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        fail(error, "Could not open GLB file: " + path);
        return out;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 20) {
        fail(error, "GLB file too small: " + path);
        return out;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);

    if (readU32Le(bytes.data()) != kGlbMagic) {
        fail(error, "Not a GLB file (bad magic): " + path);
        return out;
    }
    if (readU32Le(bytes.data() + 4) != 2u) {
        fail(error, "Unsupported GLB version: " + path);
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

    if (json_text.empty()) {
        fail(error, "GLB missing JSON chunk: " + path);
        return out;
    }

    const JsonValue root = parseJsonText(json_text);
    if (!root.isObject()) {
        fail(error, "GLB JSON chunk is not an object: " + path);
        return out;
    }

    if (const JsonValue* buffers = root.get("buffers"); buffers && buffers->isArray()) {
        for (const JsonValue& buffer : buffers->asArray()) {
            if (buffer.get("uri")) {
                fail(error, "External GLB buffers are not supported; re-export self-contained .glb: " + path);
                return out;
            }
        }
    }
    if (bin == nullptr) {
        fail(error, "GLB missing binary chunk: " + path);
        return out;
    }

    const JsonValue* nodes_json = root.get("nodes");
    if (!nodes_json || !nodes_json->isArray()) {
        fail(error, "GLB has no nodes: " + path);
        return out;
    }
    std::vector<const JsonValue*> nodes;
    nodes.reserve(nodes_json->asArray().size());
    for (const JsonValue& node : nodes_json->asArray()) {
        nodes.push_back(&node);
    }

    GltfReader reader(root, bin, bin_len);
    std::vector<int> material_remap;

    // Determine root nodes: scenes[scene].nodes, else every node.
    std::vector<int> root_nodes;
    const int scene_index = intMember(&root, "scene", -1);
    if (const JsonValue* scenes = root.get("scenes"); scenes && scenes->isArray() && scene_index >= 0 &&
        scene_index < static_cast<int>(scenes->asArray().size())) {
        const JsonValue& scene = scenes->asArray()[static_cast<std::size_t>(scene_index)];
        if (const JsonValue* scene_nodes = scene.get("nodes"); scene_nodes && scene_nodes->isArray()) {
            for (const JsonValue& n : scene_nodes->asArray()) {
                if (n.isNumber()) root_nodes.push_back(static_cast<int>(n.asNumber()));
            }
        }
    }
    if (root_nodes.empty()) {
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) root_nodes.push_back(i);
    }

    // Walk the node graph once, baking world transforms into vertices and resolving each
    // primitive's material (deduplicated by glTF material index) as it is encountered.
    std::function<void(int, const Mat4&, int)> walk = [&](int node_index, const Mat4& parent, int depth) {
        if (depth > 256 || node_index < 0 || node_index >= static_cast<int>(nodes.size())) return;
        const JsonValue& node = *nodes[static_cast<std::size_t>(node_index)];
        const Mat4 world = multiply(parent, nodeLocalMatrix(node));
        const int mesh_index = intMember(&node, "mesh", -1);
        if (mesh_index >= 0) {
            if (const JsonValue* meshes = root.get("meshes"); meshes && meshes->isArray() &&
                mesh_index < static_cast<int>(meshes->asArray().size())) {
                const JsonValue& mesh = meshes->asArray()[static_cast<std::size_t>(mesh_index)];
                if (const JsonValue* prims = mesh.get("primitives"); prims && prims->isArray()) {
                    for (const JsonValue& primitive : prims->asArray()) {
                        const int gltf_mat = intMember(&primitive, "material", -1);
                        const int local_mat = resolveMaterial(root, bin, bin_len, gltf_mat, material_remap, out);
                        appendPrimitive(root, reader, primitive, world, local_mat, out);
                    }
                }
            }
        }
        if (const JsonValue* children = node.get("children"); children && children->isArray()) {
            for (const JsonValue& child : children->asArray()) {
                if (child.isNumber()) walk(static_cast<int>(child.asNumber()), world, depth + 1);
            }
        }
    };
    for (int root_node : root_nodes) {
        walk(root_node, Mat4::identity(), 0);
    }

    if (out.triangles.empty()) {
        fail(error, "GLB produced no triangles: " + path);
        return out;
    }

    out.valid = true;
    return out;
}

} // namespace pr::gameplay::world3d::data
