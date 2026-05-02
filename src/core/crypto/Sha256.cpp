#include "core/crypto/Sha256.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace pr {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::array<std::uint32_t, 64> kInitialHash{
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

inline std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (~x & z); }

inline std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }

inline std::uint32_t sigma0(std::uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }

inline std::uint32_t sigma1(std::uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }

inline std::uint32_t gamma0(std::uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }

inline std::uint32_t gamma1(std::uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

} // namespace

std::string sha256HexLowercase(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> buf = data;
    const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8u;
    buf.push_back(0x80);
    while ((buf.size() % 64) != 56) {
        buf.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        buf.push_back(static_cast<unsigned char>((bit_len >> (i * 8)) & 0xff));
    }

    std::array<std::uint32_t, 8> h{};
    for (std::size_t i = 0; i < h.size(); ++i) {
        h[i] = kInitialHash[i];
    }

    std::array<std::uint32_t, 64> w{};
    for (std::size_t chunk = 0; chunk < buf.size(); chunk += 64) {
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t j = chunk + i * 4;
            w[i] = (static_cast<std::uint32_t>(buf[j]) << 24) | (static_cast<std::uint32_t>(buf[j + 1]) << 16) |
                   (static_cast<std::uint32_t>(buf[j + 2]) << 8) | static_cast<std::uint32_t>(buf[j + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        std::uint32_t f = h[5];
        std::uint32_t g = h[6];
        std::uint32_t hh = h[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + sigma1(e) + ch(e, f, g) + kRoundConstants[i] + w[i];
            const std::uint32_t t2 = sigma0(a) + maj(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const std::uint32_t word : h) {
        for (int i = 3; i >= 0; --i) {
            out << std::setw(2) << ((word >> (i * 8)) & 0xff);
        }
    }
    return out.str();
}

} // namespace pr
