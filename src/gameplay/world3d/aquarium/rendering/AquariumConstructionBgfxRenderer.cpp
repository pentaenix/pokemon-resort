#include "gameplay/world3d/aquarium/rendering/AquariumConstructionBgfxRenderer.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
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
        constexpr std::uint32_t kPanel = colorAbgr(248, 245, 232, 242);
        constexpr std::uint32_t kPanelShadow = colorAbgr(16, 31, 54, 105);
        constexpr std::uint32_t kNavy = colorAbgr(24, 43, 73);
        constexpr std::uint32_t kBlue = colorAbgr(47, 111, 218);
        constexpr std::uint32_t kAqua = colorAbgr(49, 174, 190);
        constexpr std::uint32_t kYellow = colorAbgr(246, 199, 62);
        constexpr std::uint32_t kRed = colorAbgr(211, 67, 79);
        constexpr std::uint32_t kDisabled = colorAbgr(148, 155, 161);
        constexpr std::uint32_t kWhite = colorAbgr(255, 255, 255);
        const auto panel = [&](const construction::ConstructionHudRect& rect) {
            if (rect.width <= 0) return;
            appendQuad(mesh, static_cast<float>(rect.x + 3), static_cast<float>(rect.y + 4),
                static_cast<float>(rect.x + rect.width + 3),
                static_cast<float>(rect.y + rect.height + 4), kPanelShadow);
            appendQuad(mesh, static_cast<float>(rect.x), static_cast<float>(rect.y),
                static_cast<float>(rect.x + rect.width),
                static_cast<float>(rect.y + rect.height), kPanel);
            appendBorder(mesh, rect, 3.0f, kNavy);
        };
        if (layout.tool_panel.width > 0) {
            panel(layout.tool_panel);
        }
        if (layout.property_panel.width > 0) {
            panel(layout.property_panel);
        }
        const auto button = [&](const construction::ConstructionHudRect& rect,
                                std::uint32_t fill, std::string_view label,
                                construction::ConstructionHudAction action,
                                bool enabled = true) {
            if (rect.width <= 0) return;
            const bool focused = visual_.focused_action == action;
            appendQuad(mesh, static_cast<float>(rect.x + 2), static_cast<float>(rect.y + 3),
                static_cast<float>(rect.x + rect.width + 2),
                static_cast<float>(rect.y + rect.height + 3), kPanelShadow);
            appendQuad(mesh, static_cast<float>(rect.x), static_cast<float>(rect.y),
                static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y + rect.height),
                focused && enabled ? fill : kPanel);
            appendBorder(mesh, rect, focused ? 4.0f : 3.0f,
                !enabled ? kDisabled : focused ? kYellow : kNavy);
            const float scale = rect.width >= 88 && label.size() <= 6U ? 2.0f : 1.0f;
            appendCenteredText(mesh, rect, label, scale,
                !enabled ? kDisabled : focused ? kWhite : kNavy);
        };
        using Action = construction::ConstructionHudAction;
        button(layout.place, kBlue, "CREATE", Action::Place);
        button(layout.select, kAqua, "SELECT", Action::Select);
        button(layout.move, kBlue, "MOVE", Action::Move);
        button(layout.resize, kBlue, "RESIZE", Action::Resize);
        button(layout.subtract, kAqua, "CUT", Action::Subtract);
        button(layout.shape, kBlue, "SHAPE", Action::Shape);
        button(layout.height, kBlue, "HEIGHT", Action::Height);
        button(layout.roundness, kAqua, "CORNERS", Action::Roundness);
        button(layout.rotate, kBlue, "TURN", Action::Rotate);
        const auto shown_tank = visual_.preview_tank
            ? visual_.preview_tank : visual_.selected_tank;
        const bool notch_enabled = shown_tank &&
            shown_tank->footprint.shape != pr::aquarium::geometry::FootprintShape::Rectangle;
        button(layout.notch_width, kBlue, "WIDTH", Action::NotchWidth, notch_enabled);
        button(layout.notch_depth, kBlue, "DEPTH", Action::NotchDepth, notch_enabled);
        button(layout.review, kAqua, "APPLY", Action::Review);
        button(layout.build, kAqua, "BUILD", Action::Build);
        button(layout.adjust, kBlue, "ADJUST", Action::Adjust);
        button(layout.remove, kRed, "DELETE", Action::Delete);
        button(layout.undo, kBlue, "UNDO", Action::Undo, visual_.undo_available);
        button(layout.redo, kBlue, "REDO", Action::Redo, visual_.redo_available);
        button(layout.cancel, kRed, "CANCEL", Action::Cancel);
        button(layout.done, kAqua, "DONE", Action::Done);
        button(layout.exit, kRed, "EXIT", Action::Exit);
        const auto choices = construction::aquariumConstructionPropertyChoices(layout, visual_);
        for (std::size_t index = 0; index < choices.size(); ++index) {
            const auto& choice = choices[index];
            appendQuad(mesh, static_cast<float>(choice.rect.x), static_cast<float>(choice.rect.y),
                static_cast<float>(choice.rect.x + choice.rect.width),
                static_cast<float>(choice.rect.y + choice.rect.height), kPanel);
            appendBorder(mesh, choice.rect, choice.selected ? 3.0f : 2.0f,
                choice.selected ? kNavy : choice.enabled ? kBlue : kDisabled);
            std::string_view label;
            if (choice.action == Action::Shape) {
                constexpr std::string_view labels[]{"RECT", "L", "U"};
                label = labels[std::clamp(choice.value, 0, 2)];
            } else if (choice.action == Action::Rotate) {
                constexpr std::string_view labels[]{"N", "E", "S", "W"};
                label = labels[std::clamp(choice.value, 0, 3)];
            } else if (choice.action == Action::NotchWidth ||
                       choice.action == Action::NotchDepth) {
                const int current = choice.action == Action::NotchWidth
                    ? shown_tank->footprint.notch_width_cells
                    : shown_tank->footprint.notch_depth_cells;
                label = choice.value < current ? "BACK" :
                    (choice.value > current ? "NEXT" : "SET");
            }
            if (!choice.selected && (choice.action == Action::Height ||
                    choice.action == Action::Roundness)) {
                label = index == 0 ? "LESS" : "MORE";
            }
            if (!label.empty()) {
                appendCenteredText(mesh, choice.rect, label,
                    choice.rect.width >= 72 ? 2.0f : 1.0f,
                    choice.enabled || choice.selected ? kNavy : kDisabled);
            } else {
                const int minimum = choice.action == Action::Height ? 4 : 0;
                const int maximum = choice.action == Action::Height ? 12 :
                    pr::aquarium::geometry::fittedCornerRadiusSteps(shown_tank->footprint, 64);
                constexpr int ticks = 9;
                const float left = static_cast<float>(choice.rect.x + 12);
                const float right = static_cast<float>(choice.rect.x + choice.rect.width - 12);
                const float y = static_cast<float>(choice.rect.y + choice.rect.height / 2);
                appendQuad(mesh, left, y - 2.0f, right, y + 2.0f, kDisabled);
                for (int tick = 0; tick < ticks; ++tick) {
                    const float x = left + (right - left) * static_cast<float>(tick) /
                        static_cast<float>(ticks - 1);
                    appendQuad(mesh, x - 1.0f, y - 6.0f, x + 1.0f, y + 6.0f, kNavy);
                }
                const float ratio = maximum > minimum
                    ? static_cast<float>(choice.value - minimum) /
                        static_cast<float>(maximum - minimum) : 0.0f;
                const float knob = left + (right - left) * std::clamp(ratio, 0.0f, 1.0f);
                appendQuad(mesh, knob - 6.0f, y - 10.0f, knob + 6.0f, y + 10.0f, kYellow);
                const construction::ConstructionHudRect knob_border{
                    static_cast<int>(knob - 6.0f), static_cast<int>(y - 10.0f), 12, 20};
                appendBorder(mesh, knob_border, 2.0f, kNavy);
            }
        }
        const std::string& status = !visual_.status_hint.empty()
            ? visual_.status_hint : visual_.navigation_hint;
        if (!status.empty() && layout.status.width > 0) {
            const float scale = width >= 640 ? 2.0f : 1.0f;
            const construction::ConstructionHudRect banner{
                layout.status.x + std::max(0, (layout.status.width -
                    static_cast<int>(textWidth(status, scale)) - 24) / 2),
                layout.status.y,
                std::min(layout.status.width,
                    static_cast<int>(textWidth(status, scale)) + 24),
                layout.status.height};
            appendQuad(mesh, static_cast<float>(banner.x), static_cast<float>(banner.y),
                static_cast<float>(banner.x + banner.width),
                static_cast<float>(banner.y + banner.height), kPanel);
            appendBorder(mesh, banner, 2.0f,
                visual_.status_hint.empty() ? kBlue : kRed);
            appendCenteredText(mesh, banner, status, scale, kNavy);
        }
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
