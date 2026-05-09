#include "resort/openhome/OpenHomeStorageBridge.hpp"

#include "core/config/Json.hpp"

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace pr::resort::openhome {

namespace {

constexpr int kDefaultBoxCount = 30;

std::string deterministicUuid(int value) {
    std::ostringstream out;
    out << "00000000-0000-4000-8000-";
    out.width(12);
    out.fill('0');
    out << value;
    return out.str();
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::optional<std::string> optionalString(const pr::JsonValue* value) {
    if (!value || value->isNull()) {
        return std::nullopt;
    }
    if (!value->isString()) {
        throw std::runtime_error("Expected JSON string");
    }
    return value->asString();
}

int intField(const pr::JsonValue& object, const std::string& key, int fallback = 0) {
    const pr::JsonValue* value = object.get(key);
    if (!value || value->isNull()) {
        return fallback;
    }
    if (!value->isNumber()) {
        throw std::runtime_error("Expected numeric JSON field: " + key);
    }
    return static_cast<int>(value->asNumber());
}

std::string stringField(const pr::JsonValue& object, const std::string& key, const std::string& fallback) {
    const pr::JsonValue* value = object.get(key);
    if (!value || value->isNull()) {
        return fallback;
    }
    if (!value->isString()) {
        throw std::runtime_error("Expected string JSON field: " + key);
    }
    return value->asString();
}

OpenHomeBankData bankDataFromJson(const pr::JsonValue& root) {
    OpenHomeBankData data;
    data.current_bank = intField(root, "current_bank", 0);
    const pr::JsonValue* banks = root.get("banks");
    if (!banks || !banks->isArray() || banks->asArray().empty()) {
        return defaultOpenHomeBankData();
    }

    for (const pr::JsonValue& bank_value : banks->asArray()) {
        OpenHomeBank bank;
        bank.id = stringField(bank_value, "id", {});
        bank.name = optionalString(bank_value.get("name"));
        bank.index = intField(bank_value, "index", static_cast<int>(data.banks.size()));
        bank.current_box = intField(bank_value, "current_box", 0);
        if (const pr::JsonValue* boxes = bank_value.get("boxes"); boxes && boxes->isArray()) {
            for (const pr::JsonValue& box_value : boxes->asArray()) {
                OpenHomeBox box;
                box.id = stringField(box_value, "id", {});
                box.name = optionalString(box_value.get("name"));
                box.index = intField(box_value, "index", static_cast<int>(bank.boxes.size()));
                if (const pr::JsonValue* identifiers = box_value.get("identifiers"); identifiers && identifiers->isObject()) {
                    for (const auto& entry : identifiers->asObject()) {
                        if (entry.second.isString()) {
                            box.identifiers_by_slot[std::stoi(entry.first)] = entry.second.asString();
                        }
                    }
                }
                bank.boxes.push_back(std::move(box));
            }
        }
        data.banks.push_back(std::move(bank));
    }
    return data.banks.empty() ? defaultOpenHomeBankData() : data;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Could not open OpenHome payload for write: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw std::runtime_error("Could not write OpenHome payload: " + path.string());
    }
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Could not open OpenHome payload: " + path.string());
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

std::string fallbackId(const std::filesystem::path& path) {
    return path.stem().string();
}

} // namespace

OpenHomeBankData defaultOpenHomeBankData() {
    OpenHomeBank bank;
    bank.id = deterministicUuid(1);
    bank.index = 0;
    bank.current_box = 0;
    for (int i = 0; i < kDefaultBoxCount; ++i) {
        OpenHomeBox box;
        box.id = deterministicUuid(i + 2);
        box.index = i;
        bank.boxes.push_back(std::move(box));
    }
    OpenHomeBankData data;
    data.banks.push_back(std::move(bank));
    data.current_bank = 0;
    return data;
}

OpenHomeStorageBridge::OpenHomeStorageBridge(std::filesystem::path storage_root)
    : storage_root_(std::move(storage_root)) {}

const std::filesystem::path& OpenHomeStorageBridge::storageRoot() const {
    return storage_root_;
}

std::filesystem::path OpenHomeStorageBridge::monsDirectory() const {
    return storage_root_ / "mons_v2";
}

std::filesystem::path OpenHomeStorageBridge::banksPath() const {
    return storage_root_ / "banks.json";
}

std::vector<OpenHomeStoredPokemon> OpenHomeStorageBridge::loadOhpkmStore() const {
    std::vector<OpenHomeStoredPokemon> out;
    const auto dir = monsDirectory();
    if (!std::filesystem::exists(dir)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ohpkm") {
            continue;
        }
        OpenHomeStoredPokemon stored;
        stored.openhome_id = fallbackId(entry.path());
        stored.payload.openhome_id = stored.openhome_id;
        stored.payload.openhome_format_version = "OHPKM";
        stored.payload.serialized_identity_or_ohpkm = readBytes(entry.path());
        out.push_back(std::move(stored));
    }
    return out;
}

void OpenHomeStorageBridge::upsertOhpkm(const OpenHomePokemonPayload& payload) const {
    if (!isValidOpenHomeId(payload.openhome_id)) {
        throw std::runtime_error("OpenHome payload requires openhome_id");
    }
    if (payload.serialized_identity_or_ohpkm.empty()) {
        throw std::runtime_error("OpenHome payload requires serialized OHPKM bytes");
    }
    std::filesystem::create_directories(monsDirectory());
    writeBytes(monsDirectory() / (payload.openhome_id + ".ohpkm"), payload.serialized_identity_or_ohpkm);
}

bool OpenHomeStorageBridge::removeOhpkm(const OpenHomeId& openhome_id) const {
    if (!isValidOpenHomeId(openhome_id)) {
        return false;
    }
    return std::filesystem::remove(monsDirectory() / (openhome_id + ".ohpkm"));
}

OpenHomeBankData OpenHomeStorageBridge::loadHomeBanks() const {
    if (!std::filesystem::exists(banksPath())) {
        return defaultOpenHomeBankData();
    }
    return bankDataFromJson(pr::parseJsonFile(banksPath().string()));
}

void OpenHomeStorageBridge::writeHomeBanks(const OpenHomeBankData& data) const {
    std::filesystem::create_directories(storage_root_);
    std::ofstream out(banksPath(), std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Could not open OpenHome banks file for write: " + banksPath().string());
    }
    out << "{\"banks\":[";
    for (std::size_t bi = 0; bi < data.banks.size(); ++bi) {
        const auto& bank = data.banks[bi];
        if (bi > 0) out << ',';
        out << "{\"id\":\"" << jsonEscape(bank.id) << "\","
            << "\"name\":";
        if (bank.name) {
            out << "\"" << jsonEscape(*bank.name) << "\"";
        } else {
            out << "null";
        }
        out << ",\"index\":" << bank.index
            << ",\"boxes\":[";
        for (std::size_t xi = 0; xi < bank.boxes.size(); ++xi) {
            const auto& box = bank.boxes[xi];
            if (xi > 0) out << ',';
            out << "{\"id\":\"" << jsonEscape(box.id) << "\","
                << "\"name\":";
            if (box.name) {
                out << "\"" << jsonEscape(*box.name) << "\"";
            } else {
                out << "null";
            }
            out << ",\"index\":" << box.index << ",\"identifiers\":{";
            bool first = true;
            for (const auto& [slot, id] : box.identifiers_by_slot) {
                if (!first) out << ',';
                first = false;
                out << "\"" << slot << "\":\"" << jsonEscape(id) << "\"";
            }
            out << "}}";
        }
        out << "],\"current_box\":" << bank.current_box << "}";
    }
    out << "],\"current_bank\":" << data.current_bank << "}";
}

void OpenHomeStorageBridge::placeInHomeBox(const OpenHomeId& openhome_id, int bank, int box, int slot) const {
    if (!isValidOpenHomeId(openhome_id)) {
        throw std::runtime_error("Cannot place empty OpenHome ID");
    }
    OpenHomeBankData data = loadHomeBanks();
    while (static_cast<int>(data.banks.size()) <= bank) {
        OpenHomeBank next = defaultOpenHomeBankData().banks.front();
        next.index = static_cast<int>(data.banks.size());
        next.id = deterministicUuid((next.index + 1) * 1000);
        data.banks.push_back(std::move(next));
    }
    auto& target_bank = data.banks[bank];
    while (static_cast<int>(target_bank.boxes.size()) <= box) {
        OpenHomeBox next;
        next.index = static_cast<int>(target_bank.boxes.size());
        next.id = deterministicUuid((target_bank.index + 1) * 1000 + next.index + 1);
        target_bank.boxes.push_back(std::move(next));
    }

    for (auto& existing_bank : data.banks) {
        for (auto& existing_box : existing_bank.boxes) {
            for (auto it = existing_box.identifiers_by_slot.begin(); it != existing_box.identifiers_by_slot.end();) {
                if (it->second == openhome_id) {
                    it = existing_box.identifiers_by_slot.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    target_bank.boxes[box].identifiers_by_slot[slot] = openhome_id;
    writeHomeBanks(data);
}

} // namespace pr::resort::openhome
