#include "resort/domain/Ids.hpp"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace pr::resort {

namespace {

unsigned long long fnv1a64(const std::string& value) {
    unsigned long long hash = 14695981039346656037ull;
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

long long unixNow() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string generateId(const char* prefix) {
    static std::random_device random_device;
    static std::mt19937_64 rng(random_device());
    static std::uniform_int_distribution<unsigned long long> dist;

    const auto now = static_cast<unsigned long long>(unixNow());
    const unsigned long long a = dist(rng);
    const unsigned long long b = dist(rng);

    std::ostringstream out;
    out << prefix << '_'
        << std::hex << std::setfill('0')
        << std::setw(12) << now
        << std::setw(16) << a
        << std::setw(16) << b;
    return out.str();
}

std::string fingerprintForFirstSeenPokemon(
    unsigned int source_game,
    const std::string& format_name,
    const std::string& trainer_name,
    unsigned int species_id,
    const std::string& raw_hash) {
    std::ostringstream seed;
    seed << "origin-v1"
        << '\x1f'
        << "game=" << source_game
        << '\x1f'
        << ";format=" << format_name
        << '\x1f'
        << ";ot=" << trainer_name
        << '\x1f'
        << ";species=" << species_id
        << '\x1f'
        << ";raw=" << raw_hash;
    std::ostringstream out;
    out << "origin-v1:" << std::hex << std::setfill('0') << std::setw(16) << fnv1a64(seed.str());
    return out.str();
}

} // namespace pr::resort
