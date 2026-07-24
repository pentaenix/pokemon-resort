#include "gameplay/attend/rendering/AttendEyeSocketMask.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void setPixel(
    std::vector<std::uint8_t>& image,
    int width,
    int x,
    int y,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b) {
    const std::size_t offset = static_cast<std::size_t>((y * width + x) * 4);
    image[offset + 0] = r;
    image[offset + 1] = g;
    image[offset + 2] = b;
    image[offset + 3] = 255;
}

std::uint8_t alphaAt(const std::vector<std::uint8_t>& image, int width, int x, int y) {
    return image[static_cast<std::size_t>((y * width + x) * 4 + 3)];
}

std::vector<std::uint8_t> outlinedSocket(
    std::uint8_t background_r,
    std::uint8_t background_g,
    std::uint8_t background_b,
    std::uint8_t interior_r,
    std::uint8_t interior_g,
    std::uint8_t interior_b) {
    constexpr int kWidth = 12;
    constexpr int kHeight = 10;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(kWidth * kHeight * 4));
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            setPixel(image, kWidth, x, y, background_r, background_g, background_b);
        }
    }
    for (int x = 3; x <= 8; ++x) {
        setPixel(image, kWidth, x, 2, 20, 20, 20);
        setPixel(image, kWidth, x, 7, 20, 20, 20);
    }
    for (int y = 2; y <= 7; ++y) {
        setPixel(image, kWidth, 3, y, 20, 20, 20);
        setPixel(image, kWidth, 8, y, 20, 20, 20);
    }
    for (int y = 3; y <= 6; ++y) {
        for (int x = 4; x <= 7; ++x) {
            setPixel(image, kWidth, x, y, interior_r, interior_g, interior_b);
        }
    }
    return image;
}

} // namespace

int main() {
    constexpr int kWidth = 12;
    constexpr int kHeight = 10;

    const auto colored_socket = outlinedSocket(35, 155, 190, 35, 155, 190);
    const auto colored_mask =
        pr::gameplay::attend::rendering::buildAttendEyeSocketMaskRgba(
            colored_socket.data(), kWidth, kHeight, 1, 1);
    expect(alphaAt(colored_mask, kWidth, 5, 4) == 255,
           "a Dewpider-style socket should be detected even when interior and exterior colors match");
    expect(alphaAt(colored_mask, kWidth, 1, 1) == 0,
           "a colored eye aperture must not include the surrounding face texture");
    expect(alphaAt(colored_mask, kWidth, 3, 4) == 0,
           "the authored dark eyelid outline should remain outside the iris aperture");

    const auto bright_socket = outlinedSocket(95, 180, 140, 235, 235, 230);
    const auto bright_mask =
        pr::gameplay::attend::rendering::buildAttendEyeSocketMaskRgba(
            bright_socket.data(), kWidth, kHeight, 1, 1);
    expect(alphaAt(bright_mask, kWidth, 6, 5) == 255,
           "a Bulbasaur-style bright sclera should remain inside the aperture");
    expect(alphaAt(bright_mask, kWidth, 10, 5) == 0,
           "a bright sclera aperture must not leak into its surrounding face texture");

    const auto gray_outline_socket = outlinedSocket(181, 197, 230, 245, 245, 245);
    auto gray_outline_image = gray_outline_socket;
    for (int x = 3; x <= 8; ++x) {
        setPixel(gray_outline_image, kWidth, x, 2, 125, 125, 125);
        setPixel(gray_outline_image, kWidth, x, 7, 125, 125, 125);
    }
    for (int y = 2; y <= 7; ++y) {
        setPixel(gray_outline_image, kWidth, 3, y, 125, 125, 125);
        setPixel(gray_outline_image, kWidth, 8, y, 125, 125, 125);
    }
    const auto gray_outline_mask =
        pr::gameplay::attend::rendering::buildAttendEyeSocketMaskRgba(
            gray_outline_image.data(), kWidth, kHeight, 1, 1);
    expect(alphaAt(gray_outline_mask, kWidth, 5, 4) == 255,
           "a Wartortle-style medium-gray outline should still produce a closed socket");
    expect(alphaAt(gray_outline_mask, kWidth, 1, 1) == 0,
           "adaptive outline detection must keep a pale face background outside the socket");

    auto open_bright_socket = outlinedSocket(240, 220, 20, 250, 250, 250);
    for (int y = 3; y <= 6; ++y) {
        setPixel(open_bright_socket, kWidth, 8, y, 240, 220, 20);
    }
    const auto open_bright_mask =
        pr::gameplay::attend::rendering::buildAttendEyeSocketMaskRgba(
            open_bright_socket.data(), kWidth, kHeight, 1, 1);
    expect(alphaAt(open_bright_mask, kWidth, 5, 4) == 255,
           "an open legacy eye drawing should retain its bright sclera aperture");
    expect(alphaAt(open_bright_mask, kWidth, 1, 1) == 0,
           "the open-eye fallback must not admit a chromatic face background");

    std::vector<std::uint8_t> dark_face(static_cast<std::size_t>(kWidth * kHeight * 4));
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            setPixel(dark_face, kWidth, x, y, 55, 55, 55);
        }
    }
    for (int y = 3; y <= 6; ++y) {
        for (int x = 4; x <= 7; ++x) {
            setPixel(dark_face, kWidth, x, y, 235, 180, 45);
        }
    }
    const auto contrasting_mask =
        pr::gameplay::attend::rendering::buildAttendEyeSocketMaskRgba(
            dark_face.data(), kWidth, kHeight, 1, 1);
    expect(alphaAt(contrasting_mask, kWidth, 5, 4) == 255,
           "a Hawlucha-style colored open eye should use its contrast against the face");
    expect(alphaAt(contrasting_mask, kWidth, 1, 1) == 0,
           "contrast fallback must keep the dominant face color outside the eye aperture");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
