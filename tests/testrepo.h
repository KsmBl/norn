/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

/*!
 * A scratch git repository that removes itself when it goes out of scope.
 *
 * Shared by the tests that need a real repository rather than a fixture string,
 * which is most of the ones covering anything that writes.
 */
class TestRepo
{
public:
    TestRepo();
    ~TestRepo();

    TestRepo(const TestRepo &) = delete;
    TestRepo &operator=(const TestRepo &) = delete;

    const std::string &path() const
    {
        return m_path;
    }

    /*!
     * Runs git in the repository and returns its stdout.
     *
     * Truncates at the first NUL, because Glib::spawn_sync hands the captured
     * output back as a std::string built from a char*. Fine for ordinary output;
     * use git_raw() for anything -z separated.
     */
    std::string git(const std::vector<std::string> &arguments) const;

    /*!
     * Runs git and returns its stdout with embedded NULs intact.
     *
     * Needed for every -z porcelain format: the ordinary capture paths hand back a
     * NUL-terminated buffer with no length, so all but the first record is lost.
     */
    std::string git_raw(const std::vector<std::string> &arguments) const;

    void write_file(const std::string &relative_path, const std::string &contents) const;
    /*! Adds the owner execute bit, for exercising mode-change handling. */
    void chmod_executable(const std::string &relative_path) const;
    /*! Runs git with @p input on stdin and returns its exit status. */
    int git_stdin(const std::vector<std::string> &arguments, const std::string &input) const;
    std::string read_file(const std::string &relative_path) const;

    /*! Commits everything currently in the working tree. */
    void commit_all(const std::string &message) const;

    /*!
     * Spins the main loop until @p predicate holds or the timeout expires.
     *
     * Counting signals would be wrong in several places: consecutive status reads
     * share a dedupe key and collapse into one, so two writes can produce a single
     * refresh. The condition is what matters, not how often it was announced.
     */
    static bool wait_until(const std::function<bool()> &predicate, int timeout_ms = 10000);

private:
    std::string m_path;
};
