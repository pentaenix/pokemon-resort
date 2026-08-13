#include "core/config/Json.hpp"
#include "mapmaker/document/OwmapDocument.hpp"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) throw TestFailure(message);
}

template <typename Callable>
void expectThrows(Callable&& callable, const std::string& message) {
    try {
        callable();
    } catch (const std::exception&) {
        return;
    }
    throw TestFailure(message);
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes.at(offset)) |
           (static_cast<std::uint32_t>(bytes.at(offset + 1U)) << 8U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 2U)) << 16U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 3U)) << 24U);
}

void appendF32(std::vector<std::uint8_t>& bytes, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "test requires 32-bit float");
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(bytes, bits);
}

std::string fixtureMetadata(int grid_width = 3) {
    return std::string(R"json({"id":"fixture","grid":{"enabled":true,"tileSize":16,"width":)json") +
           std::to_string(grid_width) +
           R"json(,"height":2},"player":{"spawnTile":null},"unknown":{"nil":null,"array":[1,true,"café"]}})json";
}

std::vector<std::uint8_t> fixtureBytes(int metadata_width = 3) {
    const std::string metadata = fixtureMetadata(metadata_width);
    std::vector<std::uint8_t> bytes;
    appendU32(bytes, pr::mapmaker::OwmapDocument::kMagic);
    appendU16(bytes, pr::mapmaker::OwmapDocument::kVersion);
    appendU16(bytes, 3U);
    appendU16(bytes, 2U);
    appendF32(bytes, 16.0F);
    appendU32(bytes, static_cast<std::uint32_t>(metadata.size()));
    bytes.insert(bytes.end(), metadata.begin(), metadata.end());
    bytes.insert(bytes.end(), {10U, 11U, 12U, 20U, 21U, 22U});
    bytes.insert(bytes.end(), {0U, 2U, 3U, 4U, 5U, 6U});
    bytes.push_back(0xC9U); // valid cells 0,3 plus two set spare bits.
    bytes.insert(bytes.end(), {0xDEU, 0xADU}); // unknown extension bytes.
    return bytes;
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw TestFailure("Could not read test file: " + path.string());
    const auto length = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), length);
    return bytes;
}

void writeFile(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw TestFailure("Could not write test file: " + path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "config" / "app.json") &&
            fs::exists(current / "assets" / "overworld" / "maps")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate pokemon-resort repository root");
}

void testJsonMutationAndSerialization() {
    pr::JsonValue root = pr::parseJsonText(R"json({"nested":{"value":"line\ntext"},"nullValue":null})json");
    root["nested"]["value"].asString() += "!";
    root["added"] = pr::JsonValue(std::string("control\x01", 8));

    const std::string compact = pr::serializeJsonValue(root);
    expect(compact.find("\\u0001") != std::string::npos,
           "compact JSON must escape otherwise-invalid control bytes");
    expect(pr::parseJsonText(compact) == root, "compact JSON must parse back without semantic loss");

    const std::string pretty = pr::serializeJsonValue(root, pr::JsonStyle::Pretty, 4U);
    expect(pretty.find("\n    \"added\"") != std::string::npos,
           "pretty JSON must honor its indentation width");
    expect(pr::parseJsonText(pretty) == root, "pretty JSON must parse back without semantic loss");

    expectThrows([] { (void)pr::parseJsonText("1e"); }, "incomplete exponent must be rejected safely");
    expectThrows([] { (void)pr::parseJsonText("01"); }, "leading-zero number must be rejected");
    expectThrows([] { (void)pr::parseJsonText(std::string("\"bad\x01\"", 6)); },
                 "unescaped string control byte must be rejected");
}

void testByteIdenticalNoOpAndUnknownMetadata() {
    const std::vector<std::uint8_t> original = fixtureBytes();
    pr::mapmaker::OwmapDocument document = pr::mapmaker::OwmapDocument::fromBytes(original);
    expect(document.validate().empty(), "valid fixture should have no document diagnostics");
    expect(document.serialize() == original, "untouched document must serialize byte-identically");
    expect(document.metadata().get("player")->get("spawnTile")->isNull(),
           "explicit metadata null must survive decode");
    expect(document.trailingBytes() == std::vector<std::uint8_t>({0xDEU, 0xADU}),
           "unknown trailing bytes must be retained");
    expect(document.heightAt(2U, 1U) == 22U, "height plane must use row-major coordinates");
    expect(document.collisionAt(0U, 0U) == 1U && document.collisionAt(0U, 1U) == 1U,
           "collision bits must decode in row-major bit order");
}

void testStructuralDecodeFailures() {
    std::vector<std::uint8_t> bytes = fixtureBytes();
    bytes[0] = 0U;
    expectThrows([&bytes] { (void)pr::mapmaker::OwmapDocument::fromBytes(bytes); },
                 "bad OWMAP magic must be rejected");

    bytes = fixtureBytes();
    bytes[4] = 2U;
    expectThrows([&bytes] { (void)pr::mapmaker::OwmapDocument::fromBytes(bytes); },
                 "unsupported OWMAP version must be rejected");

    bytes = fixtureBytes();
    bytes[6] = 0U;
    bytes[7] = 0U;
    expectThrows([&bytes] { (void)pr::mapmaker::OwmapDocument::fromBytes(bytes); },
                 "zero OWMAP width must be rejected before payload indexing");

    bytes = fixtureBytes();
    bytes.resize(bytes.size() - 4U);
    expectThrows([&bytes] { (void)pr::mapmaker::OwmapDocument::fromBytes(bytes); },
                 "truncated OWMAP terrain must be rejected");

    bytes = fixtureBytes();
    bytes[14] = 0xFFU;
    bytes[15] = 0xFFU;
    bytes[16] = 0xFFU;
    bytes[17] = 0x7FU;
    expectThrows([&bytes] { (void)pr::mapmaker::OwmapDocument::fromBytes(bytes); },
                 "oversized metadata length must be rejected without overflow");
}

void testEditedEncodingPreservesUnknownsAndSpareBits() {
    pr::mapmaker::OwmapDocument document = pr::mapmaker::OwmapDocument::fromBytes(fixtureBytes());
    document.metadata()["id"] = pr::JsonValue(std::string("changed"));
    document.collisionAt(0U, 0U) = 0U;
    const std::vector<std::uint8_t> encoded = document.serialize(pr::JsonStyle::Pretty);
    expect(encoded != fixtureBytes(), "edited document must be re-encoded");

    const std::size_t metadata_size = readU32(encoded, 14U);
    const std::size_t collision_offset = 18U + metadata_size + 12U;
    expect((encoded.at(collision_offset) & 0xC0U) == 0xC0U,
           "re-encoding must retain collision spare bits");
    expect((encoded.at(collision_offset) & 0x01U) == 0U,
           "edited collision cell must update its packed bit");

    const auto decoded = pr::mapmaker::OwmapDocument::fromBytes(encoded);
    expect(decoded.metadata().get("id")->asString() == "changed", "metadata edit must persist");
    const auto* unknown = decoded.metadata().get("unknown");
    expect(unknown && unknown->get("nil") && unknown->get("nil")->isNull(),
           "unknown nested object and null must survive edited serialization");
    expect(decoded.heights() == std::vector<std::uint8_t>({10U, 11U, 12U, 20U, 21U, 22U}),
           "metadata/collision edits must not alter height bytes");
    expect(decoded.trailingBytes() == std::vector<std::uint8_t>({0xDEU, 0xADU}),
           "edited serialization must retain unknown extension bytes");
}

void testValidationAndSynchronizedResize() {
    auto mismatched = pr::mapmaker::OwmapDocument::fromBytes(fixtureBytes(4));
    expect(!mismatched.validate().empty(), "header/metadata grid mismatch must be diagnosed");
    expect(mismatched.serialize() == fixtureBytes(4),
           "invalid but untouched input must remain byte-preservable for recovery");
    mismatched.resize(4U, 3U);
    mismatched.setTileSize(8.0F);
    expect(mismatched.metadata().get("grid")->get("width")->asNumber() == 4.0,
           "resize must synchronize metadata width");
    expect(mismatched.metadata().get("grid")->get("height")->asNumber() == 3.0,
           "resize must synchronize metadata height");
    expect(mismatched.metadata().get("grid")->get("tileSize")->asNumber() == 8.0,
           "tile size edit must synchronize metadata tileSize");
    expect(mismatched.validate().empty(), "synchronized resize should restore a valid document");

    mismatched.specialAt(0U, 0U) = 1U;
    expectThrows([&mismatched] { mismatched.validateOrThrow(); },
                 "unbaked editor-only special=1 must block a save");

    auto created = pr::mapmaker::OwmapDocument::create(2U, 2U, 0.1F);
    created.heightAt(1U, 1U) = 9U;
    const auto created_round_trip = pr::mapmaker::OwmapDocument::fromBytes(created.serialize());
    expect(created_round_trip.heightAt(1U, 1U) == 9U,
           "new document factory must produce an encodable terrain document");
    expect(created_round_trip.validate().empty(),
           "fractional tileSize must compare using its binary float representation");
}

void testAtomicSaveBackupAndRecovery() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory = fs::temp_directory_path() /
        ("pokemon_resort_owmap_document_" + std::to_string(stamp));
    fs::create_directories(directory);
    const fs::path target = directory / "map.owmap";
    const fs::path backup = fs::path(target.string() + ".bak");
    const std::vector<std::uint8_t> original = fixtureBytes();
    writeFile(target, original);

    try {
        auto document = pr::mapmaker::OwmapDocument::load(target);
        document.metadata()["id"] = pr::JsonValue(std::string("saved"));
        document.saveAtomic(target);
        expect(document.isPristine(), "successful atomic save must establish a new pristine snapshot");
        expect(readFile(backup) == original, "atomic save backup must retain exact previous bytes");
        expect(!fs::exists(fs::path(target.string() + ".tmp")), "atomic save must clean its sibling temp");
        expect(pr::mapmaker::OwmapDocument::load(target).metadata().get("id")->asString() == "saved",
               "atomic save target must pass decoded read-back");

        const std::vector<std::uint8_t> first_save = readFile(target);
        document.metadata()["id"] = pr::JsonValue(std::string("saved_again"));
        document.saveAtomic(target);
        expect(readFile(backup) == first_save,
               "a later atomic save must replace backup with the immediately previous version");

        writeFile(target, {0U, 1U, 2U});
        auto recovered = pr::mapmaker::OwmapDocument::loadWithRecovery(target);
        expect(recovered.recoveredFromBackup(), "corrupt primary must fall back to the verified backup");
        expect(recovered.loadedPath() == backup, "recovered document must expose its backup source path");
        expect(recovered.serialize() == first_save, "recovered backup must remain byte-identical");

        recovered.metadata()["id"] = pr::JsonValue(std::string("repaired_after_recovery"));
        recovered.saveAtomic(target);
        expect(readFile(backup) == first_save,
               "first save after recovery must not replace the only valid backup with corrupt primary bytes");
        expect(pr::mapmaker::OwmapDocument::load(target).metadata().get("id")->asString() ==
                   "repaired_after_recovery",
               "first save after recovery must repair the primary with the edited document");
        expect(!recovered.recoveredFromBackup(),
               "a verified repaired save should clear the document's recovery state");
    } catch (...) {
        fs::remove_all(directory);
        throw;
    }
    fs::remove_all(directory);
}

void testCommittedMapsNoOpRoundTrip() {
    const fs::path maps = repositoryRoot() / "assets" / "overworld" / "maps";
    std::size_t tested = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".owmap") continue;
        const std::vector<std::uint8_t> original = readFile(entry.path());
        const auto document = pr::mapmaker::OwmapDocument::load(entry.path());
        expect(document.serialize() == original,
               entry.path().filename().string() + " must pass byte-identical no-op serialization");
        ++tested;
    }
    expect(tested > 0U, "committed OWMAP fixture directory must contain at least one map");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"JSON mutation and serialization", testJsonMutationAndSerialization},
        {"byte-identical no-op and unknown metadata", testByteIdenticalNoOpAndUnknownMetadata},
        {"structural decode failures", testStructuralDecodeFailures},
        {"edited encoding preserves unknowns and spare bits", testEditedEncodingPreservesUnknownsAndSpareBits},
        {"validation and synchronized resize", testValidationAndSynchronizedResize},
        {"atomic save backup and recovery", testAtomicSaveBackupAndRecovery},
        {"committed maps no-op round trip", testCommittedMapsNoOpRoundTrip},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& exception) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << exception.what() << '\n';
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
