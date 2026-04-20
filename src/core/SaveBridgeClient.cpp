#include "core/SaveBridgeClient.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <spawn.h>
#include <sstream>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace fs = std::filesystem;

namespace pr {

namespace {

struct ProcessCaptureResult {
    bool launched = false;
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
    std::string error_message;
};

struct BridgeLaunchSpec {
    fs::path bridge_path;
    std::vector<std::string> args;
};

fs::path normalizePath(const fs::path& path) {
    std::error_code error;
    const fs::path normalized = fs::weakly_canonical(path, error);
    return error ? path.lexically_normal() : normalized;
}

std::optional<fs::path> resolveExecutablePath(const char* argv0) {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return normalizePath(fs::path(buffer.c_str()));
    }
#endif

    if (!argv0 || std::string_view(argv0).empty()) {
        return std::nullopt;
    }

    fs::path path(argv0);
    if (path.is_relative()) {
        path = fs::current_path() / path;
    }
    return normalizePath(path);
}

std::string shellQuote(std::string_view value) {
    std::string out = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

bool isExecutableFile(const fs::path& path) {
    std::error_code error;
    if (!fs::exists(path, error) || fs::is_directory(path, error)) {
        return false;
    }
    return ::access(path.c_str(), X_OK) == 0;
}

bool isManagedAssembly(const fs::path& path) {
    return path.extension() == ".dll";
}

std::string joinCommand(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << shellQuote(args[i]);
    }
    return out.str();
}

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

ProcessCaptureResult runProcessCapture(const std::vector<std::string>& args) {
    ProcessCaptureResult result;
    if (args.empty()) {
        result.error_message = "No process arguments provided";
        return result;
    }

    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.error_message = std::string("Failed to create pipes: ") + std::strerror(errno);
        if (stdout_pipe[0] != -1) {
            close(stdout_pipe[0]);
        }
        if (stdout_pipe[1] != -1) {
            close(stdout_pipe[1]);
        }
        if (stderr_pipe[0] != -1) {
            close(stderr_pipe[0]);
        }
        if (stderr_pipe[1] != -1) {
            close(stderr_pipe[1]);
        }
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
        result.error_message = std::string("Failed to launch process: ") + std::strerror(spawn_error);
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
            result.error_message = std::string("waitpid failed: ") + std::strerror(errno);
            return result;
        }
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }

    return result;
}

std::vector<fs::path> findBridgeCandidates(
    const fs::path& project_root,
    const std::optional<fs::path>& executable_path) {
    std::vector<fs::path> candidates;

    if (const char* override_path = std::getenv("PKHEX_BRIDGE_EXECUTABLE")) {
        if (*override_path != '\0') {
            candidates.emplace_back(override_path);
        }
    }

    if (executable_path) {
        const fs::path executable_dir = executable_path->parent_path();
        candidates.push_back(executable_dir / "pkhex_bridge" / "osx-arm64" / "PKHeXBridge");
        candidates.push_back(executable_dir / "pkhex_bridge" / "PKHeXBridge");
        candidates.push_back(executable_dir / "pkhex_bridge" / "osx-arm64" / "PKHeXBridge.dll");
        candidates.push_back(executable_dir / "pkhex_bridge" / "PKHeXBridge.dll");

        if (executable_dir.filename() == "MacOS" &&
            executable_dir.parent_path().filename() == "Contents") {
            const fs::path resources_dir = executable_dir.parent_path() / "Resources";
            candidates.push_back(resources_dir / "pkhex_bridge" / "osx-arm64" / "PKHeXBridge");
            candidates.push_back(resources_dir / "pkhex_bridge" / "PKHeXBridge");
            candidates.push_back(resources_dir / "pkhex_bridge" / "osx-arm64" / "PKHeXBridge.dll");
            candidates.push_back(resources_dir / "pkhex_bridge" / "PKHeXBridge.dll");
        }
    }

    const fs::path workspace_root = project_root.parent_path();
    const fs::path bridge_root = workspace_root / "tools" / "pkhex_bridge";
    // Prefer a local `dotnet build` output over `publish/`. Stale publish binaries have been
    // observed to win first and emit minimal JSON (no boxes), breaking transfer sprites.
    candidates.push_back(bridge_root / "bin" / "Debug" / "net10.0" / "PKHeXBridge");
    candidates.push_back(bridge_root / "bin" / "Release" / "net10.0" / "PKHeXBridge");
    candidates.push_back(bridge_root / "bin" / "Debug" / "net10.0" / "PKHeXBridge.dll");
    candidates.push_back(bridge_root / "bin" / "Release" / "net10.0" / "PKHeXBridge.dll");
    candidates.push_back(bridge_root / "publish" / "osx-arm64" / "PKHeXBridge");
    candidates.push_back(bridge_root / "publish" / "PKHeXBridge");
    return candidates;
}

std::optional<BridgeLaunchSpec> resolveBridgeLaunchSpec(
    const fs::path& project_root,
    const std::optional<fs::path>& executable_path) {
    for (const fs::path& candidate : findBridgeCandidates(project_root, executable_path)) {
        if (isExecutableFile(candidate)) {
            return BridgeLaunchSpec{normalizePath(candidate), {normalizePath(candidate).string()}};
        }
        if (isManagedAssembly(candidate) && fs::exists(candidate)) {
            return BridgeLaunchSpec{normalizePath(candidate), {"dotnet", normalizePath(candidate).string()}};
        }
    }

    const fs::path bridge_project = project_root.parent_path() / "tools" / "pkhex_bridge" / "PKHeXBridge.csproj";
    if (fs::exists(bridge_project)) {
        return BridgeLaunchSpec{
            normalizePath(bridge_project),
            {"dotnet", "run", "--project", normalizePath(bridge_project).string(), "--no-launch-profile", "--"}
        };
    }

    return std::nullopt;
}

} // namespace

SaveBridgeProbeResult runBridgeForSave(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path,
    const std::vector<std::string>& operation_args) {
    SaveBridgeProbeResult result;
    result.save_path = save_path;

    if (!fs::exists(save_path)) {
        result.error_message = "Save file does not exist";
        return result;
    }

    const std::optional<fs::path> executable_path = resolveExecutablePath(argv0);
    const std::optional<BridgeLaunchSpec> spec = resolveBridgeLaunchSpec(project_root, executable_path);
    if (!spec) {
        result.error_message = "Bridge executable or project could not be resolved";
        return result;
    }

    result.bridge_path = spec->bridge_path.string();

    std::vector<std::string> args = spec->args;
    args.insert(args.end(), operation_args.begin(), operation_args.end());
    args.push_back(save_path);
    result.command = joinCommand(args);

    ProcessCaptureResult process = runProcessCapture(args);
    result.launched = process.launched;
    result.exit_code = process.exit_code;
    result.stdout_text = std::move(process.stdout_text);
    result.stderr_text = std::move(process.stderr_text);
    result.error_message = std::move(process.error_message);
    result.success = result.launched && result.exit_code == 0;
    return result;
}

SaveBridgeProbeResult runBridgeWithArgs(
    const std::string& project_root,
    const char* argv0,
    const std::vector<std::string>& bridge_args) {
    SaveBridgeProbeResult result;
    if (!bridge_args.empty()) {
        result.save_path = bridge_args.size() > 1 ? bridge_args[1] : std::string();
    }

    const std::optional<fs::path> executable_path = resolveExecutablePath(argv0);
    const std::optional<BridgeLaunchSpec> spec = resolveBridgeLaunchSpec(project_root, executable_path);
    if (!spec) {
        result.error_message = "Bridge executable or project could not be resolved";
        return result;
    }

    result.bridge_path = spec->bridge_path.string();
    std::vector<std::string> args = spec->args;
    args.insert(args.end(), bridge_args.begin(), bridge_args.end());
    result.command = joinCommand(args);

    ProcessCaptureResult process = runProcessCapture(args);
    result.launched = process.launched;
    result.exit_code = process.exit_code;
    result.stdout_text = std::move(process.stdout_text);
    result.stderr_text = std::move(process.stderr_text);
    result.error_message = std::move(process.error_message);
    result.success = result.launched && result.exit_code == 0;
    return result;
}

SaveBridgeProbeResult probeSaveWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path) {
    return runBridgeForSave(project_root, argv0, save_path, {});
}

SaveBridgeProbeResult importSaveWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path) {
    return runBridgeForSave(project_root, argv0, save_path, {"import"});
}

SaveBridgeProbeResult writeProjectionWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path,
    const std::string& projection_json_path) {
    return runBridgeWithArgs(project_root, argv0, {"write-projection", save_path, projection_json_path});
}

} // namespace pr
