#include "gameplay/world3d/aquarium/rendering/AquariumConstructionBgfxRenderer.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

namespace {

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

void appendBorder(
    construction::ConstructionVisualMesh& mesh,
    const construction::ConstructionHudRect& rect,
    float thickness,
    std::uint32_t color) {
    const float x0 = static_cast<float>(rect.x);
    const float y0 = static_cast<float>(rect.y);
    const float x1 = static_cast<float>(rect.x + rect.width);
    const float y1 = static_cast<float>(rect.y + rect.height);
    appendQuad(mesh, x0, y0, x1, y0 + thickness, color);
    appendQuad(mesh, x0, y1 - thickness, x1, y1, color);
    appendQuad(mesh, x0, y0 + thickness, x0 + thickness, y1 - thickness, color);
    appendQuad(mesh, x1 - thickness, y0 + thickness, x1, y1 - thickness, color);
}

std::array<std::uint8_t, 7> glyphRows(char glyph) {
    switch (glyph) {
        case 'A': return {14, 17, 17, 31, 17, 17, 17};
        case 'B': return {30, 17, 17, 30, 17, 17, 30};
        case 'C': return {15, 16, 16, 16, 16, 16, 15};
        case 'D': return {30, 17, 17, 17, 17, 17, 30};
        case 'E': return {31, 16, 16, 30, 16, 16, 31};
        case 'F': return {31, 16, 16, 30, 16, 16, 16};
        case 'G': return {15, 16, 16, 23, 17, 17, 15};
        case 'H': return {17, 17, 17, 31, 17, 17, 17};
        case 'I': return {31, 4, 4, 4, 4, 4, 31};
        case 'J': return {7, 2, 2, 2, 18, 18, 12};
        case 'K': return {17, 18, 20, 24, 20, 18, 17};
        case 'L': return {16, 16, 16, 16, 16, 16, 31};
        case 'M': return {17, 27, 21, 21, 17, 17, 17};
        case 'N': return {17, 25, 21, 19, 17, 17, 17};
        case 'O': return {14, 17, 17, 17, 17, 17, 14};
        case 'P': return {30, 17, 17, 30, 16, 16, 16};
        case 'Q': return {14, 17, 17, 17, 21, 18, 13};
        case 'R': return {30, 17, 17, 30, 20, 18, 17};
        case 'S': return {15, 16, 16, 14, 1, 1, 30};
        case 'T': return {31, 4, 4, 4, 4, 4, 4};
        case 'U': return {17, 17, 17, 17, 17, 17, 14};
        case 'V': return {17, 17, 17, 17, 17, 10, 4};
        case 'W': return {17, 17, 17, 17, 21, 27, 17};
        case 'X': return {17, 17, 10, 4, 10, 17, 17};
        case 'Y': return {17, 17, 10, 4, 4, 4, 4};
        case 'Z': return {31, 1, 2, 4, 8, 16, 31};
        default: return {};
    }
}

float textWidth(std::string_view label, float scale) {
    if (label.empty()) return 0.0f;
    return static_cast<float>(label.size() * 6U - 1U) * scale;
}

void appendText(
    construction::ConstructionVisualMesh& mesh,
    std::string_view label, float x, float y, float scale,
    std::uint32_t color) {
    for (char glyph : label) {
        const auto rows = glyphRows(glyph);
        for (std::size_t row = 0; row < rows.size(); ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[row] & (1U << (4 - column))) == 0U) continue;
                const float left = x + static_cast<float>(column) * scale;
                const float top = y + static_cast<float>(row) * scale;
                appendQuad(mesh, left, top, left + scale, top + scale, color);
            }
        }
        x += 6.0f * scale;
    }
}

void appendCenteredText(
    construction::ConstructionVisualMesh& mesh,
    const construction::ConstructionHudRect& rect,
    std::string_view label, float scale, std::uint32_t color) {
    const float width = textWidth(label, scale);
    appendText(mesh, label,
        static_cast<float>(rect.x) + (static_cast<float>(rect.width) - width) * 0.5f,
        static_cast<float>(rect.y) + (static_cast<float>(rect.height) - 7.0f * scale) * 0.5f,
        scale, color);
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
    }

    void submitHud(
        std::uint16_t view_id, int width, int height, bool homogeneous_depth) const {
        if (!initialized_ || !visual_.visible || width <= 0 || height <= 0) return;
        const auto layout = construction::aquariumConstructionHudLayout(
            width, height, visual_.state);
        construction::ConstructionVisualMesh mesh;
        constexpr std::uint32_t kPanel = 0xd92b2b26U;
        constexpr std::uint32_t kBuild = 0xf065bd5aU;
        constexpr std::uint32_t kAdjust = 0xf0d3a83eU;
        constexpr std::uint32_t kCancel = 0xf0524cceU;
        constexpr std::uint32_t kPlace = 0xf065bd5aU;
        constexpr std::uint32_t kReview = 0xf04fc4e8U;
        constexpr std::uint32_t kExit = 0xf0524cceU;
        constexpr std::uint32_t kGlyph = 0xffffffffU;
        const auto button = [&](const construction::ConstructionHudRect& rect,
                                std::uint32_t fill, std::string_view label) {
            if (rect.width <= 0) return;
            appendQuad(mesh, static_cast<float>(rect.x), static_cast<float>(rect.y),
                static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y + rect.height), kPanel);
            appendBorder(mesh, rect, 4.0f, fill);
            const float scale = rect.width >= 72 ? 2.0f : 1.0f;
            appendCenteredText(mesh, rect, label, scale, kGlyph);
        };
        button(layout.place, kPlace, "PLACE");
        button(layout.review, kReview, "REVIEW");
        button(layout.build, kBuild, "BUILD");
        button(layout.adjust, kAdjust, "ADJUST");
        button(layout.cancel, kCancel, "CANCEL");
        button(layout.exit, kExit, "EXIT");
        if (!visual_.status_hint.empty() &&
            (visual_.state == construction::ConstructionState::ResizeFootprint ||
             visual_.state == construction::ConstructionState::DraftReview)) {
            const float scale = width >= 640 ? 2.0f : 1.0f;
            const int banner_width = static_cast<int>(textWidth(visual_.status_hint, scale)) + 24;
            const int banner_height = static_cast<int>(7.0f * scale) + 16;
            int button_top = layout.cancel.y;
            if (button_top <= 0) button_top = height - 18;
            const construction::ConstructionHudRect banner{
                (width - banner_width) / 2,
                std::max(10, button_top - banner_height - 10),
                banner_width,
                banner_height};
            appendQuad(mesh, static_cast<float>(banner.x), static_cast<float>(banner.y),
                static_cast<float>(banner.x + banner.width),
                static_cast<float>(banner.y + banner.height), kPanel);
            appendBorder(mesh, banner, 3.0f, kCancel);
            appendCenteredText(mesh, banner, visual_.status_hint, scale, kGlyph);
        }
        if (mesh.vertices.empty()) return;
        float view[16], projection[16];
        bx::mtxIdentity(view);
        bx::mtxOrtho(projection, 0.0f, static_cast<float>(width),
            static_cast<float>(height), 0.0f, 0.0f, 100.0f, 0.0f, homogeneous_depth);
        bgfx::setViewTransform(view_id, view, projection);
        bgfx::setViewRect(view_id, 0, 0,
            static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
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
    std::uint16_t view_id, int width, int height, bool homogeneous_depth) {
    impl_->submitHud(view_id, width, height, homogeneous_depth);
}

} // namespace pr::gameplay::world3d::aquarium::rendering
