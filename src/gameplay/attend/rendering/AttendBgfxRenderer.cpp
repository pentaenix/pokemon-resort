#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

AttendBgfxRenderer::AttendBgfxRenderer(std::string project_root, AttendSceneConfig config)
    : impl_(std::make_unique<Impl>(std::move(project_root), std::move(config))) {}

AttendBgfxRenderer::~AttendBgfxRenderer() = default;

bool AttendBgfxRenderer::initialize(
    SDL_Window* window,
    int width,
    int height,
    const std::string& bgfx_preference,
    void* sdl_metal_view) {
    return impl_->initialize(window, width, height, bgfx_preference, sdl_metal_view);
}

void AttendBgfxRenderer::shutdown() {
    impl_->shutdown();
}

bool AttendBgfxRenderer::valid() const {
    return impl_->valid();
}

std::string AttendBgfxRenderer::lastError() const {
    return impl_->lastError();
}

void AttendBgfxRenderer::setPetting(bool petting) {
    impl_->setPetting(petting);
}

void AttendBgfxRenderer::setPetContact(float vertical_bias) {
    impl_->setPetContact(vertical_bias);
}

void AttendBgfxRenderer::setFaceLook(float x, float y) {
    impl_->setFaceLook(x, y);
}

void AttendBgfxRenderer::setViewportLook(float x, float y) {
    impl_->setViewportLook(x, y);
}

void AttendBgfxRenderer::setFreeCamera(bool enabled, AttendFreeCameraPose pose) {
    impl_->setFreeCamera(enabled, pose);
}

bool AttendBgfxRenderer::currentCameraPose(AttendFreeCameraPose& out) const {
    return impl_->currentCameraPose(out);
}

void AttendBgfxRenderer::setWeatherMode(int index) {
    impl_->setWeatherMode(index);
}

void AttendBgfxRenderer::setTextureVariant(int index) {
    impl_->setTextureVariant(index);
}

void AttendBgfxRenderer::setFormVariant(int index) {
    impl_->setFormVariant(index);
}

bool AttendBgfxRenderer::canTriggerReaction(const std::string& reaction_id, double interaction_seconds) const {
    return impl_->canTriggerReaction(reaction_id, interaction_seconds);
}

bool AttendBgfxRenderer::canTriggerSemanticAnimation(const std::string& animation_semantic) const {
    return impl_->canTriggerSemanticAnimation(animation_semantic);
}

void AttendBgfxRenderer::triggerReaction(const std::string& reaction_id, double interaction_seconds) {
    impl_->triggerReaction(reaction_id, interaction_seconds);
}

void AttendBgfxRenderer::triggerSemanticAnimation(
    const std::string& animation_semantic,
    double duration_seconds,
    double fade_in_seconds,
    double fade_out_seconds,
    const std::string& eye_expression,
    const std::string& mouth_expression) {
    impl_->triggerSemanticAnimation(
        animation_semantic,
        duration_seconds,
        fade_in_seconds,
        fade_out_seconds,
        eye_expression,
        mouth_expression);
}

double AttendBgfxRenderer::wakeFromIdleAnimation() {
    return impl_->wakeFromIdleAnimation();
}

bool AttendBgfxRenderer::isIdleSleeping() const {
    return impl_->isIdleSleeping();
}

void AttendBgfxRenderer::blendOutIdleAnimation(double scene_time_seconds, double fade_out_seconds) {
    impl_->blendOutIdleAnimation(scene_time_seconds, fade_out_seconds);
}

void AttendBgfxRenderer::setOverlayButtons(
    std::vector<AttendBgfxOverlayButton> buttons,
    int logical_w,
    int logical_h) {
    impl_->setOverlayButtons(std::move(buttons), logical_w, logical_h);
}

void AttendBgfxRenderer::setCornerButtons(
    std::vector<AttendBgfxCornerButton> buttons,
    int logical_w,
    int logical_h) {
    impl_->setCornerButtons(std::move(buttons), logical_w, logical_h);
}

void AttendBgfxRenderer::setProfilePlate(
    AttendBgfxProfilePlate plate,
    int logical_w,
    int logical_h) {
    impl_->setProfilePlate(std::move(plate), logical_w, logical_h);
}

void AttendBgfxRenderer::setFaceView(bool face_view) {
    impl_->setFaceView(face_view);
}

bool AttendBgfxRenderer::faceViewTransitionActive() const {
    return impl_->faceViewTransitionActive();
}

bool AttendBgfxRenderer::faceViewAvailable() const {
    return impl_->faceViewAvailable();
}

SDL_Rect AttendBgfxRenderer::pokemonPointerRect() const {
    return impl_->pokemonPointerRect();
}

int AttendBgfxRenderer::weatherModeCount() const {
    return impl_->weatherModeCount();
}

int AttendBgfxRenderer::textureVariantIndex() const {
    return impl_->textureVariantIndex();
}

int AttendBgfxRenderer::textureVariantCount() const {
    return impl_->textureVariantCount();
}

std::string AttendBgfxRenderer::textureVariantLabel(int index) const {
    return impl_->textureVariantLabel(index);
}

int AttendBgfxRenderer::formVariantIndex() const {
    return impl_->formVariantIndex();
}

int AttendBgfxRenderer::formVariantCount() const {
    return impl_->formVariantCount();
}

std::string AttendBgfxRenderer::formVariantId(int index) const {
    return impl_->formVariantId(index);
}

std::string AttendBgfxRenderer::formVariantLabel(int index) const {
    return impl_->formVariantLabel(index);
}

void AttendBgfxRenderer::render(double scene_time_seconds, int width, int height) {
    impl_->render(scene_time_seconds, width, height);
}

void AttendBgfxRenderer::queueScreenshot(const std::string& output_path) {
    impl_->queueScreenshot(output_path);
}

} // namespace pr::gameplay::attend::rendering
