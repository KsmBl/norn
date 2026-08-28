/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/gitrunner.h"
#include "core/parsers/statusparser.h"
#include "testrepo.h"

#include <gtest/gtest.h>

/*!
 * Exercises the spawn-and-read path itself.
 *
 * The parsers can be tested on fixture strings, but nothing proves the process
 * plumbing works except running a real git and reading what comes back — which is
 * also where a port to a different toolkit is most likely to have gone wrong.
 */
TEST(GitRunner, ReadsCommandOutput)
{
    TestRepo repo;
    repo.write_file("f.txt", "hello\n");
    repo.commit_all("Initial");

    GitRunner runner(repo.path());

    GitCommand command(GitLane::Read, {"log", "-1", "--format=%s"});
    GitJob *job = runner.run(command);

    ASSERT_TRUE(TestRepo::wait_until([job] {
        return job->finished();
    }));

    EXPECT_TRUE(job->succeeded());
    EXPECT_EQ(job->stdout_data(), "Initial\n");
}

TEST(GitRunner, ReportsFailureAndStderr)
{
    TestRepo repo;
    GitRunner runner(repo.path());

    GitCommand command(GitLane::Read, {"rev-parse", "--verify", "does-not-exist"});
    GitJob *job = runner.run(command);

    ASSERT_TRUE(TestRepo::wait_until([job] {
        return job->finished();
    }));

    EXPECT_FALSE(job->succeeded());
    EXPECT_NE(job->exit_code(), 0);
}

TEST(GitRunner, WritesStdin)
{
    TestRepo repo;
    repo.write_file("f.txt", "hello\n");
    repo.commit_all("Initial");

    GitRunner runner(repo.path());

    // hash-object reads the content it should hash from stdin, so this proves the
    // whole write-then-close path rather than just that the process started.
    GitCommand command(GitLane::Read, {"hash-object", "--stdin"});
    command.m_stdin = "some content\n";
    GitJob *job = runner.run(command);

    ASSERT_TRUE(TestRepo::wait_until([job] {
        return job->finished();
    }));

    ASSERT_TRUE(job->succeeded());

    // Compared against the hash git computes for the same bytes in a file, which
    // proves stdin delivered them unchanged rather than merely that it delivered
    // something.
    repo.write_file("expected.txt", "some content\n");
    const std::string expected = repo.git({"hash-object", "expected.txt"});

    ASSERT_GE(expected.size(), 40u);
    EXPECT_EQ(job->stdout_data().substr(0, 40), expected.substr(0, 40));
}

TEST(GitRunner, HandlesLargeStdinWithoutDeadlocking)
{
    TestRepo repo;
    repo.write_file("f.txt", "hello\n");
    repo.commit_all("Initial");

    GitRunner runner(repo.path());

    // Well past the 64 KiB pipe buffer. Writing this much without draining stdout
    // concurrently is the classic way to deadlock both processes.
    GitCommand command(GitLane::Read, {"hash-object", "--stdin"});
    command.m_stdin = std::string(4 * 1024 * 1024, 'x');
    GitJob *job = runner.run(command);

    ASSERT_TRUE(TestRepo::wait_until(
        [job] {
            return job->finished();
        },
        20000));

    EXPECT_TRUE(job->succeeded());
    EXPECT_EQ(job->stdout_data().size(), 41u);
}

TEST(GitRunner, ParsesStatusThroughTheRunner)
{
    TestRepo repo;
    repo.write_file("tracked.txt", "one\n");
    repo.commit_all("Initial");
    repo.write_file("tracked.txt", "two\n");
    repo.write_file("new.txt", "fresh\n");

    GitRunner runner(repo.path());

    GitCommand command(GitLane::Read, StatusParser::arguments());
    GitJob *job = runner.run(command);

    ASSERT_TRUE(TestRepo::wait_until([job] {
        return job->finished();
    }));
    ASSERT_TRUE(job->succeeded());

    const StatusSnapshot snapshot = StatusParser::parse(job->stdout_data());

    EXPECT_EQ(snapshot.m_branch, "main");
    EXPECT_FALSE(snapshot.m_is_unborn);
    ASSERT_EQ(snapshot.m_entries.size(), 2u);
}

TEST(GitRunner, CoalescesDuplicateReads)
{
    TestRepo repo;
    repo.write_file("f.txt", "hello\n");
    repo.commit_all("Initial");

    GitRunner runner(repo.path());

    GitCommand command(GitLane::Read, StatusParser::arguments());
    command.m_dedupe_key = "status";

    // A burst of filesystem events must cost one query, not one per event.
    GitJob *first = runner.run(command);
    GitJob *second = runner.run(command);
    GitJob *third = runner.run(command);

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, third);

    ASSERT_TRUE(TestRepo::wait_until([first] {
        return first->finished();
    }));
    EXPECT_TRUE(first->succeeded());
}

TEST(GitRunner, SerialisesWrites)
{
    TestRepo repo;
    repo.write_file("f.txt", "hello\n");
    repo.commit_all("Initial");

    GitRunner runner(repo.path());

    // Writers contend on index.lock, so the runner must never have two in flight.
    std::vector<GitJob *> jobs;
    for (int i = 0; i < 5; ++i) {
        repo.write_file("f" + std::to_string(i) + ".txt", "content\n");
        jobs.push_back(runner.run(GitCommand(GitLane::Write, {"add", "f" + std::to_string(i) + ".txt"})));
    }

    ASSERT_TRUE(TestRepo::wait_until([&jobs] {
        for (GitJob *job : jobs) {
            if (!job->finished()) {
                return false;
            }
        }
        return true;
    }));

    for (GitJob *job : jobs) {
        EXPECT_TRUE(job->succeeded());
    }

    // All five landed, which is what serialising them was for.
    EXPECT_EQ(StatusParser::parse(repo.git_raw({"status", "--porcelain=v2", "-z"})).m_entries.size(), 5u);
}
