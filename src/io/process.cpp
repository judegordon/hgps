#include "process.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fmt/format.h>

extern char **environ;

namespace hgps::io {
namespace {

constexpr std::size_t kMaxCapturedOutput = 8192;

} // namespace

ProcessResult run_process(const std::string &program,
                          const std::vector<std::string> &arguments) {
    ProcessResult result;

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        result.output = "could not create a pipe to capture the program's output";
        return result;
    }

    // posix_spawn with an explicit argv: no shell, so no quoting or metacharacter semantics.
    std::vector<std::string> argv_storage;
    argv_storage.push_back(program);
    argv_storage.insert(argv_storage.end(), arguments.begin(), arguments.end());

    std::vector<char *> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto &argument : argv_storage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

    pid_t pid = -1;
    const int spawned = ::posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(),
                                       ::environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_fds[1]);

    if (spawned != 0) {
        ::close(pipe_fds[0]);
        result.output = fmt::format("could not start '{}': {}", program, std::strerror(spawned));
        return result;
    }

    result.started = true;

    std::array<char, 1024> buffer{};
    ssize_t read_bytes = 0;
    while ((read_bytes = ::read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
        if (result.output.size() < kMaxCapturedOutput) {
            result.output.append(buffer.data(), static_cast<std::size_t>(read_bytes));
        }
    }
    ::close(pipe_fds[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            result.output += "\nwaitpid failed";
            return result;
        }
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }

    if (result.output.size() > kMaxCapturedOutput) {
        result.output.resize(kMaxCapturedOutput);
        result.output += "... (output truncated)";
    }

    return result;
}

bool tool_available(const std::string &program) {
    // `command -v` would need a shell; asking the tool itself is enough, and every tool this
    // project runs supports --version.
    const auto result = run_process(program, {"--version"});
    return result.started;
}

} // namespace hgps::io
