#include "gameplay/world3d/aquarium/rendering/AquariumConstructionBgfxRenderer.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

namespace {

constexpr std::uint32_t colorAbgr(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255) {
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8U) |
        (static_cast<std::uint32_t>(blue) << 16U) |
        (static_cast<std::uint32_t>(alpha) << 24U);
}

struct Vertex {
    float x, y, z;
    std::uint32_t abgr;
    float u, v;
    float nx, ny, nz;
};

void appendQuad(
    construction::ConstructionVisualMesh& mesh,
    float x0, float y0, float x1, float y1,
    std::uint32_t color) {
    if (mesh.vertices.size() > 65531U) return;
    const auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0, y0, 0.0f, color});
    mesh.vertices.push_back({x1, y0, 0.0f, color});
    mesh.vertices.push_back({x1, y1, 0.0f, color});
    mesh.vertices.push_back({x0, y1, 0.0f, color});
    mesh.indices.insert(mesh.indices.end(), {
        first, static_cast<std::uint16_t>(first + 1U), static_cast<std::uint16_t>(first + 2U),
        first, static_cast<std::uint16_t>(first + 2U), static_cast<std::uint16_t>(first + 3U)});
}

void appendDisc(
    construction::ConstructionVisualMesh& mesh,
    float center_x, float center_y, float radius,
    std::uint32_t color) {
    constexpr int kSegments = 24;
    if (mesh.vertices.size() > 65510U) return;
    const auto center = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({center_x, center_y, 0.0f, color});
    for (int segment = 0; segment <= kSegments; ++segment) {
        const float angle = bx::kPi2 * static_cast<float>(segment) /
            static_cast<float>(kSegments);
        mesh.vertices.push_back({
            center_x + std::cos(angle) * radius,
            center_y + std::sin(angle) * radius,
            0.0f,
            color});
    }
    for (int segment = 0; segment < kSegments; ++segment) {
        mesh.indices.insert(mesh.indices.end(), {
            center,
            static_cast<std::uint16_t>(center + segment + 1),
            static_cast<std::uint16_t>(center + segment + 2)});
    }
}

void appendLine2d(
    construction::ConstructionVisualMesh& mesh,
    float x0, float y0, float x1, float y1,
    float thickness, std::uint32_t color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::max(0.001f, std::hypot(dx, dy));
    const float px = -dy / length * thickness * 0.5f;
    const float py = dx / length * thickness * 0.5f;
    if (mesh.vertices.size() > 65531U) return;
    const auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0 + px, y0 + py, 0.0f, color});
    mesh.vertices.push_back({x1 + px, y1 + py, 0.0f, color});
    mesh.vertices.push_back({x1 - px, y1 - py, 0.0f, color});
    mesh.vertices.push_back({x0 - px, y0 - py, 0.0f, color});
    mesh.indices.insert(mesh.indices.end(), {
        first, static_cast<std::uint16_t>(first + 1U), static_cast<std::uint16_t>(first + 2U),
        first, static_cast<std::uint16_t>(first + 2U), static_cast<std::uint16_t>(first + 3U)});
}

void appendTriangle2d(
    construction::ConstructionVisualMesh& mesh,
    float x0, float y0, float x1, float y1, float x2, float y2,
    std::uint32_t color) {
    if (mesh.vertices.size() > 65532U) return;
    const auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0, y0, 0.0f, color});
    mesh.vertices.push_back({x1, y1, 0.0f, color});
    mesh.vertices.push_back({x2, y2, 0.0f, color});
    mesh.indices.insert(mesh.indices.end(), {
        first, static_cast<std::uint16_t>(first + 1U),
        static_cast<std::uint16_t>(first + 2U)});
}

} // namespace

class AquariumConstructionBgfxRenderer::Impl {
public:
    void initialize(
        const bgfx::VertexLayout& layout, bgfx::ProgramHandle program,
        bgfx::TextureHandle white_texture, bgfx::UniformHandle texture_uniform,
        bgfx::UniformHandle tint_cutoff_uniform, bgfx::UniformHandle color_adjust_uniform,
        bgfx::UniformHandle texture_blur_uniform, bgfx::UniformHandle uv_offset_uniform,
        bgfx::UniformHandle light_dir_uniform, bgfx::UniformHandle light_params_uniform) {
        layout_ = layout;
        program_ = program;
        white_texture_ = white_texture;
        texture_uniform_ = texture_uniform;
        tint_cutoff_uniform_ = tint_cutoff_uniform;
        color_adjust_uniform_ = color_adjust_uniform;
        texture_blur_uniform_ = texture_blur_uniform;
        uv_offset_uniform_ = uv_offset_uniform;
        light_dir_uniform_ = light_dir_uniform;
        light_params_uniform_ = light_params_uniform;
        initialized_ = true;
    }

    void shutdown() {
        initialized_ = false;
        visual_ = {};
    }

    void setVisual(construction::AquariumConstructionVisual visual) {
        visual_ = std::move(visual);
    }

    void setCommonState() const {
        const float tint[4]{1.0f, 1.0f, 1.0f, 0.0f};
        const float adjust[4]{1.0f, 1.0f, 1.0f, 0.0f};
        const float zero[4]{};
        const float light[4]{0.0f, 1.0f, 0.0f, 0.0f};
        const float params[4]{1.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setTexture(0, texture_uniform_, white_texture_);
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, zero);
        bgfx::setUniform(uv_offset_uniform_, zero);
        bgfx::setUniform(light_dir_uniform_, light);
        bgfx::setUniform(light_params_uniform_, params);
    }

    bool submitMesh(
        std::uint16_t view_id,
        const construction::ConstructionVisualMesh& mesh,
        std::uint64_t state) const {
        if (!initialized_ || !visual_.visible || mesh.vertices.empty() || mesh.indices.empty() ||
            !bgfx::isValid(program_) || !bgfx::isValid(white_texture_)) return false;
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        if (!bgfx::allocTransientBuffers(&tvb, layout_,
                static_cast<std::uint32_t>(mesh.vertices.size()), &tib,
                static_cast<std::uint32_t>(mesh.indices.size()))) return false;
        auto* vertices = reinterpret_cast<Vertex*>(tvb.data);
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            const auto& source = mesh.vertices[index];
            vertices[index] = Vertex{
                source.x, source.y, source.z, source.abgr,
                0.5f, 0.5f, 0.0f, 1.0f, 0.0f};
        }
        std::copy(mesh.indices.begin(), mesh.indices.end(),
            reinterpret_cast<std::uint16_t*>(tib.data));
        float model[16];
        bx::mtxIdentity(model);
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        setCommonState();
        bgfx::setState(state);
        bgfx::submit(view_id, program_);
        return true;
    }

    void submitWorld(std::uint16_t view_id) const {
        const auto mesh = construction::buildAquariumConstructionWorldMesh(visual_);
        submitMesh(view_id, mesh,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA);
        const auto height_preview =
            construction::buildAquariumConstructionHeightPreviewMesh(visual_);
        submitMesh(view_id, height_preview,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        const auto depth_preview =
            construction::buildAquariumConstructionDepthPreviewMesh(visual_);
        submitMesh(view_id, depth_preview,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    }

    void submitHud(
        std::uint16_t view_id,
        int framebuffer_width,
        int framebuffer_height,
        int logical_width,
        int logical_height,
        bool homogeneous_depth) const {
        if (!initialized_ || !visual_.visible || framebuffer_width <= 0 ||
            framebuffer_height <= 0 || logical_width <= 0 || logical_height <= 0) return;
        const int width = logical_width;
        const int height = logical_height;
        const auto layout = construction::aquariumConstructionHudLayout(
            width, height, visual_.state, visual_.property_draft);
        construction::ConstructionVisualMesh mesh;
        constexpr std::uint32_t kCream = colorAbgr(246, 242, 221);
        constexpr std::uint32_t kShadow = colorAbgr(16, 31, 54, 150);
        constexpr std::uint32_t kNavy = colorAbgr(34, 59, 91);
        constexpr std::uint32_t kBlue = colorAbgr(54, 132, 224);
        constexpr std::uint32_t kGreen = colorAbgr(66, 183, 126);
        constexpr std::uint32_t kAqua = colorAbgr(39, 177, 188);
        constexpr std::uint32_t kMagenta = colorAbgr(196, 89, 142);
        constexpr std::uint32_t kYellow = colorAbgr(246, 199, 62);
        constexpr std::uint32_t kRed = colorAbgr(225, 91, 87);
        constexpr std::uint32_t kDisabled = colorAbgr(148, 155, 161);
        constexpr std::uint32_t kWhite = colorAbgr(255, 255, 246);
        using Action = construction::ConstructionHudAction;
        const auto button = [&](const construction::ConstructionHudRect& rect,
                                Action action, std::uint32_t fill,
                                bool enabled = true, bool active = false) {
            if (rect.width <= 0) return;
            const bool focused = visual_.focused_action == action;
            const float center_x = static_cast<float>(rect.x) + rect.width * 0.5f;
            const float center_y = static_cast<float>(rect.y) + rect.height * 0.5f;
            const float radius = std::min(rect.width, rect.height) * 0.5f;
            appendDisc(mesh, center_x, center_y + 4.0f, radius, kShadow);
            appendDisc(mesh, center_x, center_y, radius,
                focused || active ? kYellow : kCream);
            appendDisc(mesh, center_x, center_y, radius - 4.0f, kNavy);
            appendDisc(mesh, center_x, center_y, radius - 9.0f,
                enabled ? fill : kDisabled);
            const float arm = radius * 0.38f;
            if (action == Action::Place || action == Action::Subtract) {
                appendLine2d(mesh, center_x - arm, center_y,
                    center_x + arm, center_y, 6.0f, kWhite);
                if (action == Action::Place) {
                    appendLine2d(mesh, center_x, center_y - arm,
                        center_x, center_y + arm, 6.0f, kWhite);
                }
            } else if (action == Action::Build || action == Action::Exit) {
                appendLine2d(mesh, center_x - arm, center_y,
                    center_x - arm * 0.2f, center_y + arm * 0.65f, 6.0f, kWhite);
                appendLine2d(mesh, center_x - arm * 0.2f, center_y + arm * 0.65f,
                    center_x + arm, center_y - arm * 0.65f, 6.0f, kWhite);
            } else if (action == Action::Cancel) {
                appendLine2d(mesh, center_x - arm * 0.75f, center_y - arm * 0.75f,
                    center_x + arm * 0.75f, center_y + arm * 0.75f, 7.0f, kWhite);
                appendLine2d(mesh, center_x + arm * 0.75f, center_y - arm * 0.75f,
                    center_x - arm * 0.75f, center_y + arm * 0.75f, 7.0f, kWhite);
            } else if (action == Action::Delete) {
                appendQuad(mesh, center_x - arm * 0.68f, center_y - arm * 0.05f,
                    center_x + arm * 0.68f, center_y + arm * 0.86f, kWhite);
                appendLine2d(mesh, center_x - arm, center_y - arm * 0.34f,
                    center_x + arm, center_y - arm * 0.34f, 8.0f, kWhite);
            } else if (action == Action::Undo || action == Action::Redo) {
                const bool redo = action == Action::Redo;
                const float direction = redo ? 1.0f : -1.0f;
                appendLine2d(mesh, center_x - direction * arm * 0.7f, center_y,
                    center_x + direction * arm * 0.65f, center_y, 9.0f, kWhite);
                appendTriangle2d(mesh,
                    center_x + direction * arm, center_y,
                    center_x + direction * arm * 0.3f, center_y - arm * 0.62f,
                    center_x + direction * arm * 0.3f, center_y + arm * 0.62f,
                    kWhite);
            }
        };
        button(layout.place, Action::Place, kBlue, true, !visual_.subtract_mode);
        button(layout.subtract, Action::Subtract, kMagenta, true, visual_.subtract_mode);
        button(layout.undo, Action::Undo, kBlue, visual_.undo_available);
        button(layout.redo, Action::Redo, kAqua, visual_.redo_available);
        button(layout.remove, Action::Delete, kRed);
        button(layout.build, Action::Build, kGreen);
        button(layout.cancel, Action::Cancel, kRed);
        button(layout.exit, Action::Exit, kGreen);
        if (mesh.vertices.empty()) return;
        float view[16], projection[16];
        bx::mtxIdentity(view);
        bx::mtxOrtho(projection, 0.0f, static_cast<float>(width),
            static_cast<float>(height), 0.0f, 0.0f, 100.0f, 0.0f, homogeneous_depth);
        bgfx::setViewTransform(view_id, view, projection);
        bgfx::setViewRect(view_id, 0, 0,
            static_cast<std::uint16_t>(framebuffer_width),
            static_cast<std::uint16_t>(framebuffer_height));
        bgfx::setViewFrameBuffer(view_id, BGFX_INVALID_HANDLE);
        bgfx::setViewClear(view_id, BGFX_CLEAR_NONE);
        bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);
        submitMesh(view_id, mesh,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    }

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_texture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uv_offset_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_dir_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_params_uniform_ = BGFX_INVALID_HANDLE;
    construction::AquariumConstructionVisual visual_{};
    bool initialized_ = false;
};

AquariumConstructionBgfxRenderer::AquariumConstructionBgfxRenderer()
    : impl_(std::make_unique<Impl>()) {}
AquariumConstructionBgfxRenderer::~AquariumConstructionBgfxRenderer() = default;
void AquariumConstructionBgfxRenderer::initialize(
    const bgfx::VertexLayout& layout, bgfx::ProgramHandle program,
    bgfx::TextureHandle white_texture, bgfx::UniformHandle texture_uniform,
    bgfx::UniformHandle tint_cutoff_uniform, bgfx::UniformHandle color_adjust_uniform,
    bgfx::UniformHandle texture_blur_uniform, bgfx::UniformHandle uv_offset_uniform,
    bgfx::UniformHandle light_dir_uniform, bgfx::UniformHandle light_params_uniform) {
    impl_->initialize(layout, program, white_texture, texture_uniform,
        tint_cutoff_uniform, color_adjust_uniform, texture_blur_uniform,
        uv_offset_uniform, light_dir_uniform, light_params_uniform);
}
void AquariumConstructionBgfxRenderer::shutdown() { impl_->shutdown(); }
void AquariumConstructionBgfxRenderer::setVisual(
    construction::AquariumConstructionVisual visual) {
    impl_->setVisual(std::move(visual));
}
void AquariumConstructionBgfxRenderer::submitWorld(std::uint16_t view_id) {
    impl_->submitWorld(view_id);
}
void AquariumConstructionBgfxRenderer::submitHud(
    std::uint16_t view_id,
    int framebuffer_width,
    int framebuffer_height,
    int logical_width,
    int logical_height,
    bool homogeneous_depth) {
    impl_->submitHud(view_id, framebuffer_width, framebuffer_height,
        logical_width, logical_height, homogeneous_depth);
}

} // namespace pr::gameplay::world3d::aquarium::rendering
