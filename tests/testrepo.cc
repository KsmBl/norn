/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "testrepo.h"

#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <glibmm/spawn.h>

#include <cstdio>
#include <glib/gstdio.h>
#include <sys/wait.h>
#include <fstream>
#include <functional>

namespace
{
/*! Removes a directory tree; std::filesystem would do, but this keeps the deps flat. */
void remove_tree(const std::string &path)
{
    Glib::spawn_sync("/", std::vector<std::string>{"rm", "-rf", path}, Glib::SPAWN_SEARCH_PATH);
}
}

TestRepo::TestRepo()
{
    m_path = Glib::build_filename(Glib::get_tmp_dir(), "norn-test-" + std::to_string(g_random_int()));
    g_mkdir_with_parents(m_path.c_str(), 0700);

    // Isolated from whatever the developer has configured, so the tests behave the
    // same on any machine.
    Glib::setenv("GIT_CONFIG_GLOBAL", "/dev/null", true);
    Glib::setenv("GIT_CONFIG_SYSTEM", "/dev/null", true);

    git({"init", "-q", "-b", "main"});
    git({"config", "user.name", "Test"});
    git({"config", "user.email", "test@example.invalid"});
}

TestRepo::~TestRepo()
{
    remove_tree(m_path);
}

std::string TestRepo::git(const std::vector<std::string> &arguments) const
{
    std::vector<std::string> argv{"git"};
    argv.insert(argv.end(), arguments.begin(), arguments.end());

    std::string out;
    std::string err;
    int status = 0;

    Glib::spawn_sync(m_path, argv, Glib::SPAWN_SEARCH_PATH, Glib::SlotSpawnChildSetup(), &out, &err, &status);
    return out;
}

std::string TestRepo::git_raw(const std::vector<std::string> &arguments) const
{
    // For NUL-separated output, the only reliable route is reading the pipe
    // directly: g_spawn_sync's buffer is NUL-terminated with no length returned.
    std::string command = "git";
    for (const std::string &argument : arguments) {
        command += " '" + argument + "'";
    }

    const std::string script = "cd '" + m_path + "' && " + command;

    std::string result;
    if (FILE *pipe = popen(script.c_str(), "r")) {
        char buffer[4096];
        std::size_t count = 0;
        while ((count = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
            result.append(buffer, count);
        }
        pclose(pipe);
    }

    return result;
}

void TestRepo::write_file(const std::string &relative_path, const std::string &contents) const
{
    const std::string absolute = Glib::build_filename(m_path, relative_path);
    g_mkdir_with_parents(Glib::path_get_dirname(absolute).c_str(), 0700);

    std::ofstream file(absolute, std::ios::binary | std::ios::trunc);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

void TestRepo::chmod_executable(const std::string &relative_path) const
{
    const std::string absolute = Glib::build_filename(m_path, relative_path);
    g_chmod(absolute.c_str(), 0755);
}

int TestRepo::git_stdin(const std::vector<std::string> &arguments, const std::string &input) const
{
    std::string command = "git";
    for (const std::string &argument : arguments) {
        command += " '" + argument + "'";
    }

    const std::string script = "cd '" + m_path + "' && " + command;

    FILE *pipe = popen(script.c_str(), "w");
    if (pipe == nullptr) {
        return -1;
    }

    std::fwrite(input.data(), 1, input.size(), pipe);
    const int status = pclose(pipe);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string TestRepo::read_file(const std::string &relative_path) const
{
    std::ifstream file(Glib::build_filename(m_path, relative_path), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void TestRepo::commit_all(const std::string &message) const
{
    git({"add", "-A"});
    git({"commit", "-qm", message});
}

bool TestRepo::wait_until(const std::function<bool()> &predicate, int timeout_ms)
{
    const gint64 deadline = g_get_monotonic_time() + timeout_ms * 1000;

    while (!predicate()) {
        if (g_get_monotonic_time() > deadline) {
            return false;
        }
        // The runner is entirely main-loop driven, so the loop has to be turned for
        // anything at all to happen.
        Glib::MainContext::get_default()->iteration(false);
        g_usleep(1000);
    }

    return true;
}
