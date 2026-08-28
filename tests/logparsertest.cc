/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/parsers/logparser.h"
#include "testrepo.h"

#include <gtest/gtest.h>

TEST(LogParser, ParsesRecordsFromRealGit)
{
    TestRepo repo;
    repo.write_file("f.txt", "one\n");
    repo.commit_all("First commit");
    repo.write_file("f.txt", "two\n");
    repo.commit_all("Second commit");

    std::vector<std::string> args = LogParser::log_arguments(10);
    const std::vector<CommitRecord> commits = LogParser::parse(repo.git_raw(args));

    ASSERT_EQ(commits.size(), 2u);
    EXPECT_EQ(commits[0].m_subject, "Second commit");
    EXPECT_EQ(commits[1].m_subject, "First commit");

    // The newest commit has the older one as its only parent; the root has none.
    ASSERT_EQ(commits[0].m_parents.size(), 1u);
    EXPECT_EQ(commits[0].m_parents.front(), commits[1].m_object_name);
    EXPECT_TRUE(commits[1].is_root());
    EXPECT_FALSE(commits[0].is_merge());
}

TEST(LogParser, SubjectWithNewlinesInBodySurvives)
{
    TestRepo repo;
    repo.write_file("f.txt", "one\n");
    repo.git({"add", "-A"});
    repo.git({"commit", "-qm", "Subject line", "-m", "Body first paragraph.\n\nBody second paragraph."});

    const std::vector<CommitRecord> commits = LogParser::parse(repo.git_raw(LogParser::log_arguments(10)));

    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].m_subject, "Subject line");
    // Records are NUL-separated precisely so a multi-line body cannot be mistaken
    // for a record boundary.
    EXPECT_NE(commits[0].m_body.find("second paragraph"), std::string::npos);
}

TEST(LogParser, MergeCommitHasTwoParents)
{
    TestRepo repo;
    repo.write_file("base.txt", "base\n");
    repo.commit_all("base");

    repo.git({"checkout", "-q", "-b", "side"});
    repo.write_file("side.txt", "side\n");
    repo.commit_all("side work");

    repo.git({"checkout", "-q", "main"});
    repo.write_file("main.txt", "main\n");
    repo.commit_all("main work");

    repo.git({"merge", "--no-ff", "--no-edit", "side"});

    const std::vector<CommitRecord> commits = LogParser::parse(repo.git_raw(LogParser::log_arguments(20)));

    ASSERT_FALSE(commits.empty());
    EXPECT_TRUE(commits.front().is_merge());
    EXPECT_EQ(commits.front().m_parents.size(), 2u);
}

TEST(LogParser, RefDecorationsAreParsed)
{
    TestRepo repo;
    repo.write_file("f.txt", "one\n");
    repo.commit_all("Only commit");
    repo.git({"tag", "v1.0"});

    const std::vector<CommitRecord> commits = LogParser::parse(repo.git_raw(LogParser::log_arguments(10)));

    ASSERT_EQ(commits.size(), 1u);

    bool saw_head = false;
    bool saw_tag = false;
    for (const std::string &ref : commits[0].m_refs) {
        if (ref.find("HEAD") != std::string::npos) {
            saw_head = true;
        }
        if (ref.find("refs/tags/v1.0") != std::string::npos) {
            saw_tag = true;
        }
    }

    EXPECT_TRUE(saw_head);
    EXPECT_TRUE(saw_tag);
}

TEST(LogParser, StashIsNotIncludedInHistory)
{
    TestRepo repo;
    repo.write_file("f.txt", "one\n");
    repo.commit_all("Only commit");

    repo.write_file("f.txt", "dirty\n");
    repo.git({"stash", "push", "-q", "-m", "wip"});

    // --all would pull in refs/stash and render each stash's internal index commit
    // as though it were part of the history.
    const std::vector<CommitRecord> commits = LogParser::parse(repo.git_raw(LogParser::log_arguments(20)));

    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].m_subject, "Only commit");
}
