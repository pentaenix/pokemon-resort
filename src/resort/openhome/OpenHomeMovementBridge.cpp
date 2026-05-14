#include "resort/openhome/OpenHomeMovementBridge.hpp"

#include "core/config/Json.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

extern char** environ;

namespace pr::resort::openhome {

namespace {

struct ProcessResult {
    bool launched = false;
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
    std::string error_message;
};

std::string readAllFromFd(int fd) {
    std::string out;
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            out.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
    return out;
}

std::optional<pr::JsonValue> tryParseJsonFromStdout(const std::string& stdout_text) {
    try {
        return pr::parseJsonText(stdout_text);
    } catch (...) {
        // fall through
    }

    // Some Node environments (or dependencies) can emit non-JSON bytes to stdout.
    // Our bridge contract is "print exactly one JSON object to stdout"; harden by
    // extracting the last JSON-looking object from the mixed stream.
    //
    // Strategy: find candidate '{' positions from the end, attempt to parse the suffix.
    for (std::size_t i = stdout_text.size(); i-- > 0;) {
        if (stdout_text[i] != '{') {
            continue;
        }
        try {
            return pr::parseJsonText(stdout_text.substr(i));
        } catch (...) {
            continue;
        }
    }
    return std::nullopt;
}

ProcessResult runProcessCapture(const std::vector<std::string>& args) {
    ProcessResult result;
    if (args.empty()) {
        result.error_message = "No OpenHome bridge command provided";
        return result;
    }

    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.error_message = std::string("Failed to create OpenHome bridge pipes: ") + std::strerror(errno);
        if (stdout_pipe[0] != -1) close(stdout_pipe[0]);
        if (stdout_pipe[1] != -1) close(stdout_pipe[1]);
        if (stderr_pipe[0] != -1) close(stderr_pipe[0]);
        if (stderr_pipe[1] != -1) close(stderr_pipe[1]);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child_pid = 0;
    const int spawn_error = posix_spawnp(
        &child_pid,
        args.front().c_str(),
        &actions,
        nullptr,
        argv.data(),
        environ);
    posix_spawn_file_actions_destroy(&actions);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (spawn_error != 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        result.error_message = std::string("Failed to launch OpenHome bridge: ") + std::strerror(spawn_error);
        return result;
    }

    result.launched = true;
    result.stdout_text = readAllFromFd(stdout_pipe[0]);
    result.stderr_text = readAllFromFd(stderr_pipe[0]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status = 0;
    while (waitpid(child_pid, &status, 0) < 0) {
        if (errno != EINTR) {
            result.error_message = std::string("OpenHome bridge waitpid failed: ") + std::strerror(errno);
            return result;
        }
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

OpenHomeMovementError movementError(const std::string& code, const std::string& message) {
    return OpenHomeMovementError{code, message};
}

const pr::JsonValue* child(const pr::JsonValue& value, const std::string& key) {
    return value.isObject() ? value.get(key) : nullptr;
}

std::string stringField(const pr::JsonValue& value, const std::string& key) {
    const pr::JsonValue* field = child(value, key);
    return field && field->isString() ? field->asString() : std::string{};
}

bool boolField(const pr::JsonValue& value, const std::string& key) {
    const pr::JsonValue* field = child(value, key);
    return field && field->isBool() && field->asBool();
}

std::string supportKey(int dex_number, int form_number) {
    return std::to_string(dex_number) + ":" + std::to_string(form_number);
}

std::string supportQueriesJson(const std::vector<OpenHomePokemonSupportQuery>& queries) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < queries.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "{\"dexNumber\":" << queries[i].dex_number
            << ",\"formeNumber\":" << queries[i].form_number << '}';
    }
    out << ']';
    return out.str();
}

int base64Value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    if (ch == '=') return -2;
    if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') return -3;
    return -1;
}

std::vector<std::uint8_t> decodeBase64(const std::string& text) {
    std::vector<std::uint8_t> out;
    std::array<int, 4> block{};
    int count = 0;
    for (const char ch : text) {
        const int value = base64Value(ch);
        if (value == -3) continue;
        if (value == -1) throw std::runtime_error("Invalid base64 in OpenHome bridge output");
        block[static_cast<std::size_t>(count++)] = value;
        if (count != 4) continue;
        if (block[0] < 0 || block[1] < 0) throw std::runtime_error("Invalid base64 padding");
        out.push_back(static_cast<std::uint8_t>((block[0] << 2) | (block[1] >> 4)));
        if (block[2] != -2) {
            out.push_back(static_cast<std::uint8_t>(((block[1] & 0x0f) << 4) | (block[2] >> 2)));
        }
        if (block[3] != -2) {
            if (block[2] == -2) throw std::runtime_error("Invalid base64 padding");
            out.push_back(static_cast<std::uint8_t>(((block[2] & 0x03) << 6) | block[3]));
        }
        count = 0;
    }
    if (count != 0) {
        throw std::runtime_error("Truncated base64 in OpenHome bridge output");
    }
    return out;
}

OpenHomePokemonPayload payloadFromJson(const pr::JsonValue& root, const OpenHomeId& fallback_id) {
    OpenHomePokemonPayload payload;
    payload.openhome_id = stringField(root, "openhomeId").empty() ? fallback_id : stringField(root, "openhomeId");
    payload.openhome_format_version = "OHPKM";
    const std::string encoded = stringField(root, "ohpkmBase64");
    if (!encoded.empty()) {
        payload.serialized_identity_or_ohpkm = decodeBase64(encoded);
    }
    return payload;
}

std::vector<std::string> bridgeArgs(
    const std::filesystem::path& bridge_script,
    const std::filesystem::path& storage_root,
    const std::string& command) {
    const char* node_override = std::getenv("OPENHOME_NODE_EXECUTABLE");
    return {
        node_override && *node_override ? node_override : "node",
        bridge_script.string(),
        command,
        "--storage-root",
        storage_root.string(),
    };
}

void appendSaveSlotArgs(std::vector<std::string>& args, const OpenHomeSaveSlot& slot) {
    args.push_back("--save");
    args.push_back(slot.save_path.string());
    if (!slot.save_type.empty()) {
        args.push_back("--save-type");
        args.push_back(slot.save_type);
    }
    args.push_back("--box");
    args.push_back(std::to_string(slot.box));
    args.push_back("--slot");
    args.push_back(std::to_string(slot.slot));
}

/// When the bridge exits non-zero it usually writes `jsonErr` to **stdout** (with stderr empty).
std::string bridgeProcessFailureDetail(const ProcessResult& process) {
    std::string detail = process.error_message.empty() ? process.stderr_text : process.error_message;
    if (!detail.empty()) {
        return detail;
    }
    if (const auto root = tryParseJsonFromStdout(process.stdout_text);
        root && root->isObject()) {
        const std::string from_json = stringField(*root, "error");
        if (!from_json.empty()) {
            return from_json;
        }
    }
    std::ostringstream oss;
    oss << "OpenHome bridge exited with code " << process.exit_code;
    if (!process.stdout_text.empty()) {
        constexpr std::size_t cap = 280;
        oss << " stdout=";
        if (process.stdout_text.size() <= cap) {
            oss << process.stdout_text;
        } else {
            oss << process.stdout_text.substr(0, cap) << "…";
        }
    }
    return oss.str();
}

double doubleField(const pr::JsonValue& value, const std::string& key, double fallback) {
    const pr::JsonValue* field = child(value, key);
    return field && field->isNumber() ? field->asNumber() : fallback;
}

OpenHomeBridgePerf parseBridgePerf(const pr::JsonValue& root) {
    OpenHomeBridgePerf perf;
    const pr::JsonValue* perf_node = child(root, "perf");
    if (!perf_node || !perf_node->isObject()) {
        return perf;
    }
    perf.save_loads = static_cast<int>(doubleField(*perf_node, "save_loads", 0));
    perf.save_writes = static_cast<int>(doubleField(*perf_node, "save_writes", 0));
    perf.count = static_cast<int>(doubleField(*perf_node, "count", 0));
    perf.total_ms = doubleField(*perf_node, "total_ms", 0);
    return perf;
}

int intChild(const pr::JsonValue& object, const std::string& key, int fallback) {
    const pr::JsonValue* field = child(object, key);
    return field && field->isNumber() ? static_cast<int>(field->asNumber()) : fallback;
}

void logOpenHomeBridgePerfIfEnabled(const OpenHomeBridgePerf& perf, const char* label) {
    const char* flag = std::getenv("PDSM_TRACE_OPENHOME_PERF");
    if (flag == nullptr || std::string(flag) != "1") {
        return;
    }
    std::cerr << "[openhome-perf] " << label << " save_loads=" << perf.save_loads
              << " save_writes=" << perf.save_writes << " count=" << perf.count << " total_ms=" << perf.total_ms
              << '\n';
}

} // namespace

OpenHomeCliMovementBridge::OpenHomeCliMovementBridge(
    std::filesystem::path project_root,
    std::filesystem::path storage_root)
    : project_root_(std::move(project_root)),
      storage_root_(std::move(storage_root)) {}

std::filesystem::path OpenHomeCliMovementBridge::bridgeScriptPath() const {
    if (const char* override_path = std::getenv("OPENHOME_RESORT_BRIDGE")) {
        if (*override_path != '\0') {
            return override_path;
        }
    }
    return project_root_ / "tools" / "OpenHome" / "dist" / "resort-bridge" / "resortBridge.mjs";
}

OpenHomeSyncSaveResult OpenHomeCliMovementBridge::syncOpenSave(const OpenHomeSyncSaveRequest& request) {
    OpenHomeSyncSaveResult out;
    const auto script = bridgeScriptPath();
    if (!std::filesystem::exists(script)) {
        out.error = movementError("bridge_missing", "OpenHome resort bridge is not built: " + script.string());
        return out;
    }
    std::vector<std::string> args = bridgeArgs(script, storage_root_, "sync-save");
    args.push_back("--save");
    args.push_back(request.save_path.string());
    if (!request.save_type.empty()) {
        args.push_back("--save-type");
        args.push_back(request.save_type);
    }
    const ProcessResult process = runProcessCapture(args);
    if (!process.launched || process.exit_code != 0) {
        out.error = movementError("bridge_failed", bridgeProcessFailureDetail(process));
        return out;
    }
    try {
        const std::optional<pr::JsonValue> root_opt = tryParseJsonFromStdout(process.stdout_text);
        if (!root_opt.has_value()) {
            throw std::runtime_error("OpenHome bridge returned non-JSON stdout");
        }
        const pr::JsonValue& root = *root_opt;
        out.success = boolField(root, "success");
        if (const pr::JsonValue* ids = child(root, "syncedOpenhomeIds"); ids && ids->isArray()) {
            for (const pr::JsonValue& item : ids->asArray()) {
                if (item.isString()) out.matched_openhome_ids.push_back(item.asString());
            }
        }
    } catch (const std::exception& ex) {
        out.error = movementError("parse_failed", ex.what());
    }
    return out;
}

OpenHomePullToHomeResult OpenHomeCliMovementBridge::pullPokemonToHome(const OpenHomePullToHomeRequest& request) {
    OpenHomePullToHomeResult out;
    const auto script = bridgeScriptPath();
    if (!std::filesystem::exists(script)) {
        out.error = movementError("bridge_missing", "OpenHome resort bridge is not built: " + script.string());
        return out;
    }
    std::vector<std::string> args = bridgeArgs(script, storage_root_, "pull-to-home");
    appendSaveSlotArgs(args, request.source);
    args.push_back("--home-bank");
    args.push_back(std::to_string(request.destination.bank));
    args.push_back("--home-box");
    args.push_back(std::to_string(request.destination.box));
    args.push_back("--home-slot");
    args.push_back(std::to_string(request.destination.slot));
    args.push_back("--write-save");
    args.push_back(request.write_source_save ? "true" : "false");

    const ProcessResult process = runProcessCapture(args);
    if (!process.launched || process.exit_code != 0) {
        out.error = movementError("bridge_failed", bridgeProcessFailureDetail(process));
        return out;
    }
    try {
        const std::optional<pr::JsonValue> root_opt = tryParseJsonFromStdout(process.stdout_text);
        if (!root_opt.has_value()) {
            throw std::runtime_error("OpenHome bridge returned non-JSON stdout");
        }
        const pr::JsonValue& root = *root_opt;
        out.success = boolField(root, "success");
        out.openhome_id = stringField(root, "openhomeId");
        out.payload = payloadFromJson(root, out.openhome_id);
        out.source_save_written = boolField(root, "sourceSaveWritten");
        out.home_banks_written = true;
        out.ohpkm_store_written = true;
        if (const pr::JsonValue* displaced = child(root, "displacedHomeOpenhomeId");
            displaced && displaced->isString() && !displaced->asString().empty()) {
            out.displaced_home_openhome_id = displaced->asString();
        }
        if (!out.success) {
            std::string err = stringField(root, "error");
            if (err.empty()) {
                err = "OpenHome pull-to-home reported failure without an error message";
            }
            out.error = movementError("bridge_error", err);
        }
    } catch (const std::exception& ex) {
        out.error = movementError("parse_failed", ex.what());
    }
    return out;
}

OpenHomePushToGameResult OpenHomeCliMovementBridge::pushPokemonToGame(const OpenHomePushToGameRequest& request) {
    OpenHomePushToGameResult out;
    const auto script = bridgeScriptPath();
    if (!std::filesystem::exists(script)) {
        out.error = movementError("bridge_missing", "OpenHome resort bridge is not built: " + script.string());
        return out;
    }
    std::vector<std::string> args = bridgeArgs(script, storage_root_, "push-to-game");
    args.push_back("--openhome-id");
    args.push_back(request.openhome_id);
    appendSaveSlotArgs(args, request.destination);
    args.push_back("--write-save");
    args.push_back(request.write_target_save ? "true" : "false");

    const ProcessResult process = runProcessCapture(args);
    if (!process.launched || process.exit_code != 0) {
        out.error = movementError("bridge_failed", bridgeProcessFailureDetail(process));
        return out;
    }
    try {
        const std::optional<pr::JsonValue> root_opt = tryParseJsonFromStdout(process.stdout_text);
        if (!root_opt.has_value()) {
            throw std::runtime_error("OpenHome bridge returned non-JSON stdout");
        }
        const pr::JsonValue& root = *root_opt;
        out.success = boolField(root, "success");
        out.openhome_id = stringField(root, "openhomeId");
        out.updated_payload = payloadFromJson(root, out.openhome_id);
        out.target_save_written = boolField(root, "targetSaveWritten");
        out.ohpkm_store_written = true;
        if (!out.success) {
            std::string err = stringField(root, "error");
            if (err.empty()) {
                err = "OpenHome push-to-game reported failure without an error message";
            }
            out.error = movementError("bridge_error", err);
        }
    } catch (const std::exception& ex) {
        out.error = movementError("parse_failed", ex.what());
    }
    return out;
}

OpenHomeBatchPullToHomeResult OpenHomeCliMovementBridge::batchPullPokemonToHome(
    const OpenHomeBatchPullToHomeRequest& request) {
    OpenHomeBatchPullToHomeResult out;
    const auto script = bridgeScriptPath();
    if (!std::filesystem::exists(script)) {
        out.error = movementError("bridge_missing", "OpenHome resort bridge is not built: " + script.string());
        return out;
    }
    std::vector<std::string> args = bridgeArgs(script, storage_root_, "batch-pull-to-home");
    args.push_back("--save");
    args.push_back(request.save_path.string());
    if (!request.save_type.empty()) {
        args.push_back("--save-type");
        args.push_back(request.save_type);
    }
    args.push_back("--ops-json");
    args.push_back(request.ops_json_path.string());
    args.push_back("--write-save");
    args.push_back(request.write_source_save ? "true" : "false");

    const ProcessResult process = runProcessCapture(args);
    if (!process.launched || process.exit_code != 0) {
        out.error = movementError("bridge_failed", bridgeProcessFailureDetail(process));
        return out;
    }
    try {
        const std::optional<pr::JsonValue> root_opt = tryParseJsonFromStdout(process.stdout_text);
        if (!root_opt.has_value()) {
            throw std::runtime_error("OpenHome bridge returned non-JSON stdout");
        }
        const pr::JsonValue& root = *root_opt;
        out.success = boolField(root, "success");
        out.perf = parseBridgePerf(root);
        logOpenHomeBridgePerfIfEnabled(out.perf, "batch-pull-to-home");
        if (const pr::JsonValue* pulls = child(root, "pulls"); pulls && pulls->isArray()) {
            for (const pr::JsonValue& item : pulls->asArray()) {
                if (!item.isObject()) {
                    continue;
                }
                OpenHomeBatchPullItemResult row;
                row.openhome_id = stringField(item, "openhomeId");
                row.payload = payloadFromJson(item, row.openhome_id);
                const pr::JsonValue* disp = child(item, "displacedHomeOpenhomeId");
                if (disp && disp->isString() && !disp->asString().empty()) {
                    row.displaced_home_openhome_id = disp->asString();
                }
                row.source_box = intChild(item, "sourceBox", 0);
                row.source_slot = intChild(item, "sourceSlot", 0);
                if (const pr::JsonValue* hl = child(item, "homeLocation"); hl && hl->isObject()) {
                    row.home_bank = intChild(*hl, "bank", 0);
                    row.home_box = intChild(*hl, "box", 0);
                    row.home_slot = intChild(*hl, "slot", 0);
                }
                out.pulls.push_back(std::move(row));
            }
        }
        if (!out.success) {
            std::string err = stringField(root, "error");
            if (err.empty()) {
                err = "OpenHome batch-pull-to-home reported failure without an error message";
            }
            out.error = movementError("bridge_error", err);
        }
    } catch (const std::exception& ex) {
        out.error = movementError("parse_failed", ex.what());
    }
    return out;
}

OpenHomeBatchPushToGameResult OpenHomeCliMovementBridge::batchPushPokemonToGame(
    const OpenHomeBatchPushToGameRequest& request) {
    OpenHomeBatchPushToGameResult out;
    const auto script = bridgeScriptPath();
    if (!std::filesystem::exists(script)) {
        out.error = movementError("bridge_missing", "OpenHome resort bridge is not built: " + script.string());
        return out;
    }
    std::vector<std::string> args = bridgeArgs(script, storage_root_, "batch-push-to-game");
    args.push_back("--save");
    args.push_back(request.save_path.string());
    if (!request.save_type.empty()) {
        args.push_back("--save-type");
        args.push_back(request.save_type);
    }
    args.push_back("--ops-json");
    args.push_back(request.ops_json_path.string());
    args.push_back("--write-save");
    args.push_back(request.write_target_save ? "true" : "false");

    const ProcessResult process = runProcessCapture(args);
    if (!process.launched || process.exit_code != 0) {
        out.error = movementError("bridge_failed", bridgeProcessFailureDetail(process));
        return out;
    }
    try {
        const std::optional<pr::JsonValue> root_opt = tryParseJsonFromStdout(process.stdout_text);
        if (!root_opt.has_value()) {
            throw std::runtime_error("OpenHome bridge returned non-JSON stdout");
        }
        const pr::JsonValue& root = *root_opt;
        out.success = boolField(root, "success");
        out.perf = parseBridgePerf(root);
        logOpenHomeBridgePerfIfEnabled(out.perf, "batch-push-to-game");
        if (const pr::JsonValue* pushes = child(root, "pushes"); pushes && pushes->isArray()) {
            for (const pr::JsonValue& item : pushes->asArray()) {
                if (!item.isObject()) {
                    continue;
                }
                OpenHomeBatchPushItemResult row;
                row.openhome_id = stringField(item, "openhomeId");
                row.payload = payloadFromJson(item, row.openhome_id);
                out.pushes.push_back(std::move(row));
            }
        }
        if (!out.success) {
            std::string err = stringField(root, "error");
            if (err.empty()) {
                err = "OpenHome batch-push-to-game reported failure without an error message";
            }
            out.error = movementError("bridge_error", err);
        }
    } catch (const std::exception& ex) {
        out.error = movementError("parse_failed", ex.what());
    }
    return out;
}

OpenHomeMoveBetweenGamesResult OpenHomeCliMovementBridge::movePokemonBetweenGames(
    const OpenHomeMoveBetweenGamesRequest& request) {
    (void)request;
    OpenHomeMoveBetweenGamesResult out;
    out.error = movementError("unsupported", "The current OpenHome CLI exposes pull-to-home and push-to-game only");
    return out;
}

OpenHomePokemonSupportResult OpenHomeCliMovementBridge::queryPokemonSupport(
    const OpenHomeSaveSlot& save,
    const std::vector<OpenHomePokemonSupportQuery>& queries) {
    OpenHomePokemonSupportResult out;
    if (queries.empty()) {
        out.success = true;
        return out;
    }
    const auto script = bridgeScriptPath();
    if (!std::filesystem::exists(script)) {
        out.error = movementError("bridge_missing", "OpenHome resort bridge is not built: " + script.string());
        return out;
    }
    std::vector<std::string> args = bridgeArgs(script, storage_root_, "supports-mons");
    args.push_back("--save");
    args.push_back(save.save_path.string());
    if (!save.save_type.empty()) {
        args.push_back("--save-type");
        args.push_back(save.save_type);
    }
    args.push_back("--mons-json");
    args.push_back(supportQueriesJson(queries));

    const ProcessResult process = runProcessCapture(args);
    if (!process.launched || process.exit_code != 0) {
        out.error = movementError("bridge_failed", bridgeProcessFailureDetail(process));
        return out;
    }
    try {
        const std::optional<pr::JsonValue> root_opt = tryParseJsonFromStdout(process.stdout_text);
        if (!root_opt.has_value()) {
            throw std::runtime_error("OpenHome bridge returned non-JSON stdout");
        }
        const pr::JsonValue& root = *root_opt;
        out.success = boolField(root, "success");
        if (const pr::JsonValue* results = child(root, "results"); results && results->isArray()) {
            for (const pr::JsonValue& item : results->asArray()) {
                if (!item.isObject()) {
                    continue;
                }
                const pr::JsonValue* dex = child(item, "dexNumber");
                const pr::JsonValue* form = child(item, "formeNumber");
                const pr::JsonValue* supported = child(item, "supported");
                if (!dex || !dex->isNumber() || !form || !form->isNumber() || !supported || !supported->isBool()) {
                    continue;
                }
                out.supported_by_key[supportKey(
                    static_cast<int>(dex->asNumber()),
                    static_cast<int>(form->asNumber()))] = supported->asBool();
            }
        }
        if (!out.success) {
            std::string err = stringField(root, "error");
            if (err.empty()) {
                err = "OpenHome supports-mons reported failure without an error message";
            }
            out.error = movementError("bridge_error", err);
        }
    } catch (const std::exception& ex) {
        out.error = movementError("parse_failed", ex.what());
    }
    return out;
}

bool isValidOpenHomeSaveSlot(const OpenHomeSaveSlot& slot) {
    return !slot.save_path.empty() && !slot.save_type.empty() && slot.box >= 0 && slot.slot >= 0;
}

bool isValidOpenHomeHomeSlot(const OpenHomeHomeSlot& slot) {
    return slot.bank >= 0 && slot.box >= 0 && slot.slot >= 0;
}

std::vector<OpenHomeMovementStep> planOpenHomePullToHome(const OpenHomePullToHomeRequest& request) {
    if (!isValidOpenHomeSaveSlot(request.source)) {
        throw std::invalid_argument("OpenHome pull requires a valid source save slot");
    }
    if (!isValidOpenHomeHomeSlot(request.destination)) {
        throw std::invalid_argument("OpenHome pull requires a valid Home destination slot");
    }

    std::vector<OpenHomeMovementStep> steps{
        OpenHomeMovementStep::LoadSourceSave,
        OpenHomeMovementStep::SyncTrackedPokemonWithSaveData,
        OpenHomeMovementStep::LoadTrackedPokemonOrStartTracking,
        OpenHomeMovementStep::ClearSourceSaveSlot,
    };
    if (request.write_ohpkm_store) {
        steps.push_back(OpenHomeMovementStep::UpsertOhpkmStore);
    }
    steps.push_back(OpenHomeMovementStep::PlaceOpenHomeIdInHomeBank);
    if (request.write_home_banks) {
        steps.push_back(OpenHomeMovementStep::WriteOpenHomeBanks);
    }
    if (request.write_source_save) {
        steps.push_back(OpenHomeMovementStep::PrepareSaveWriter);
        steps.push_back(OpenHomeMovementStep::WriteSaveFile);
    }
    return steps;
}

std::vector<OpenHomeMovementStep> planOpenHomePushToGame(const OpenHomePushToGameRequest& request) {
    if (!isValidOpenHomeId(request.openhome_id)) {
        throw std::invalid_argument("OpenHome push requires openhome_id");
    }
    if (!isValidOpenHomeSaveSlot(request.destination)) {
        throw std::invalid_argument("OpenHome push requires a valid destination save slot");
    }

    std::vector<OpenHomeMovementStep> steps{
        OpenHomeMovementStep::LoadTargetSave,
        OpenHomeMovementStep::LoadTrackedPokemonOrStartTracking,
        OpenHomeMovementStep::ConvertOhpkmForTargetSave,
        OpenHomeMovementStep::WriteTargetSaveSlot,
    };
    if (request.write_ohpkm_store) {
        steps.push_back(OpenHomeMovementStep::UpsertOhpkmStore);
    }
    if (request.write_target_save) {
        steps.push_back(OpenHomeMovementStep::PrepareSaveWriter);
        steps.push_back(OpenHomeMovementStep::WriteSaveFile);
    }
    return steps;
}

std::vector<OpenHomeMovementStep> planOpenHomeMoveBetweenGames(const OpenHomeMoveBetweenGamesRequest& request) {
    if (!isValidOpenHomeSaveSlot(request.source)) {
        throw std::invalid_argument("OpenHome move requires a valid source save slot");
    }
    if (!isValidOpenHomeSaveSlot(request.destination)) {
        throw std::invalid_argument("OpenHome move requires a valid destination save slot");
    }

    std::vector<OpenHomeMovementStep> steps{
        OpenHomeMovementStep::LoadSourceSave,
        OpenHomeMovementStep::LoadTargetSave,
        OpenHomeMovementStep::SyncTrackedPokemonWithSaveData,
        OpenHomeMovementStep::LoadTrackedPokemonOrStartTracking,
        OpenHomeMovementStep::ConvertOhpkmForTargetSave,
        OpenHomeMovementStep::ClearSourceSaveSlot,
        OpenHomeMovementStep::WriteTargetSaveSlot,
    };
    if (request.write_ohpkm_store) {
        steps.push_back(OpenHomeMovementStep::UpsertOhpkmStore);
    }
    if (request.write_source_save || request.write_target_save) {
        steps.push_back(OpenHomeMovementStep::PrepareSaveWriter);
        steps.push_back(OpenHomeMovementStep::WriteSaveFile);
    }
    return steps;
}

const char* openHomeMovementStepName(OpenHomeMovementStep step) {
    switch (step) {
        case OpenHomeMovementStep::LoadSourceSave: return "load_source_save";
        case OpenHomeMovementStep::LoadTargetSave: return "load_target_save";
        case OpenHomeMovementStep::SyncTrackedPokemonWithSaveData: return "sync_tracked_pokemon_with_save_data";
        case OpenHomeMovementStep::LoadTrackedPokemonOrStartTracking: return "load_tracked_pokemon_or_start_tracking";
        case OpenHomeMovementStep::ConvertOhpkmForTargetSave: return "convert_ohpkm_for_target_save";
        case OpenHomeMovementStep::ClearSourceSaveSlot: return "clear_source_save_slot";
        case OpenHomeMovementStep::WriteTargetSaveSlot: return "write_target_save_slot";
        case OpenHomeMovementStep::UpsertOhpkmStore: return "upsert_ohpkm_store";
        case OpenHomeMovementStep::PlaceOpenHomeIdInHomeBank: return "place_openhome_id_in_home_bank";
        case OpenHomeMovementStep::WriteOpenHomeBanks: return "write_openhome_banks";
        case OpenHomeMovementStep::PrepareSaveWriter: return "prepare_save_writer";
        case OpenHomeMovementStep::WriteSaveFile: return "write_save_file";
    }
    return "unknown";
}

} // namespace pr::resort::openhome
