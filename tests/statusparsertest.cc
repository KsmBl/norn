/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/parsers/statusparser.h"

#include <gtest/gtest.h>

namespace
{
/*! Builds NUL-separated porcelain output from a list of records. */
std::string records(std::initializer_list<std::string> lines)
{
    std::string output;
    for (const std::string &line : lines) {
        output += line;
        output.push_back('\0');
    }
    return output;
}
}

TEST(StatusParser, EmptyOutput)
{
    const StatusSnapshot snapshot = StatusParser::parse({});

    EXPECT_TRUE(snapshot.m_entries.empty());
    EXPECT_TRUE(snapshot.is_clean());
    EXPECT_FALSE(snapshot.has_conflicts());
    EXPECT_FALSE(snapshot.m_is_unborn);
}

TEST(StatusParser, BranchHeaders)
{
    const StatusSnapshot snapshot = StatusParser::parse(records({
        "# branch.oid b31df2744dadd5e076d4e9c8ba8598bdda241aad",
        "# branch.head custom",
        "# branch.upstream origin/custom",
        "# branch.ab +3 -2",
        "# stash 4",
    }));

    EXPECT_EQ(snapshot.m_head_oid, "b31df2744dadd5e076d4e9c8ba8598bdda241aad");
    EXPECT_EQ(snapshot.m_branch, "custom");
    EXPECT_EQ(snapshot.m_upstream, "origin/custom");
    EXPECT_EQ(snapshot.m_ahead, 3);
    EXPECT_EQ(snapshot.m_behind, 2);
    EXPECT_TRUE(snapshot.m_has_ahead_behind);
    EXPECT_EQ(snapshot.m_stash_count, 4);
    EXPECT_FALSE(snapshot.m_is_detached);
}

TEST(StatusParser, UnbornBranch)
{
    const StatusSnapshot snapshot = StatusParser::parse(records({"# branch.oid (initial)", "# branch.head main"}));

    EXPECT_TRUE(snapshot.m_is_unborn);
    EXPECT_TRUE(snapshot.m_head_oid.empty());
    EXPECT_EQ(snapshot.m_branch, "main");
}

TEST(StatusParser, DetachedHead)
{
    const StatusSnapshot snapshot = StatusParser::parse(records({"# branch.head (detached)"}));

    EXPECT_TRUE(snapshot.m_is_detached);
    EXPECT_TRUE(snapshot.m_branch.empty());
}

TEST(StatusParser, OrdinaryEntries)
{
    const StatusSnapshot snapshot = StatusParser::parse(records({
        "1 M. N... 100644 100644 100644 aaaa bbbb src/foo.cpp",
        "1 .D N... 100644 100644 000000 cccc dddd gone.txt",
    }));

    ASSERT_EQ(snapshot.m_entries.size(), 2u);

    const StatusEntry &staged = snapshot.m_entries[0];
    EXPECT_EQ(staged.m_path, "src/foo.cpp");
    EXPECT_EQ(staged.m_index_status, 'M');
    EXPECT_EQ(staged.m_worktree_status, '.');
    EXPECT_TRUE(staged.is_staged());
    EXPECT_FALSE(staged.is_unstaged());
    EXPECT_EQ(staged.m_mode_head, 0100644u);
    EXPECT_EQ(staged.m_oid_head, "aaaa");

    const StatusEntry &deleted = snapshot.m_entries[1];
    EXPECT_EQ(deleted.m_path, "gone.txt");
    EXPECT_FALSE(deleted.is_staged());
    EXPECT_TRUE(deleted.is_unstaged());
    // Absent from the worktree, which is what makes a deletion distinguishable.
    EXPECT_EQ(deleted.m_mode_worktree, 0u);
}

TEST(StatusParser, StagedAndUnstagedSimultaneously)
{
    // "MM": staged modification plus further unstaged edits. The entry has to appear
    // in both lists; showing it once is a classic Git GUI bug.
    const StatusSnapshot snapshot = StatusParser::parse(records({"1 MM N... 100644 100644 100644 aaaa bbbb both.txt"}));

    ASSERT_EQ(snapshot.m_entries.size(), 1u);
    EXPECT_TRUE(snapshot.m_entries[0].is_staged());
    EXPECT_TRUE(snapshot.m_entries[0].is_unstaged());
}

TEST(StatusParser, RenameConsumesTwoTokens)
{
    const StatusSnapshot snapshot =
        StatusParser::parse(records({"2 R. N... 100644 100644 100644 aaaa bbbb R100 new/name.cpp", "old/name.cpp"}));

    ASSERT_EQ(snapshot.m_entries.size(), 1u);

    const StatusEntry &entry = snapshot.m_entries[0];
    EXPECT_EQ(entry.m_kind, StatusEntry::Kind::RenamedOrCopied);
    EXPECT_EQ(entry.m_path, "new/name.cpp");
    EXPECT_EQ(entry.m_orig_path, "old/name.cpp");
    EXPECT_EQ(entry.m_score, 100);
    EXPECT_TRUE(entry.is_staged());
}

TEST(StatusParser, RenameFollowedByOtherEntries)
{
    // The regression this guards: treating the origPath token as its own record
    // desynchronises the parse and misattributes everything after the rename.
    const StatusSnapshot snapshot = StatusParser::parse(records({
        "2 R. N... 100644 100644 100644 aaaa bbbb R100 new.cpp",
        "old.cpp",
        "1 .M N... 100644 100644 100644 cccc dddd after.cpp",
        "? untracked.cpp",
    }));

    ASSERT_EQ(snapshot.m_entries.size(), 3u);
    EXPECT_EQ(snapshot.m_entries[0].m_orig_path, "old.cpp");
    EXPECT_EQ(snapshot.m_entries[1].m_path, "after.cpp");
    EXPECT_EQ(snapshot.m_entries[1].m_kind, StatusEntry::Kind::Ordinary);
    EXPECT_EQ(snapshot.m_entries[2].m_path, "untracked.cpp");
    EXPECT_EQ(snapshot.m_entries[2].m_kind, StatusEntry::Kind::Untracked);
}

TEST(StatusParser, UnmergedStages)
{
    const StatusSnapshot snapshot = StatusParser::parse(records({
        "u UU N... 100644 100644 100644 100644 1111 2222 3333 conflict.cpp",
        "u DU N... 100644 000000 100644 100644 1111 0000000000000000000000000000000000000000 3333 theirs.cpp",
    }));

    ASSERT_EQ(snapshot.m_entries.size(), 2u);
    EXPECT_TRUE(snapshot.has_conflicts());

    const StatusEntry &both = snapshot.m_entries[0];
    EXPECT_EQ(both.conflict_code(), "UU");
    EXPECT_TRUE(both.is_conflicted());
    // A conflict belongs in neither the staged nor the unstaged list.
    EXPECT_FALSE(both.is_staged());
    EXPECT_FALSE(both.is_unstaged());
    EXPECT_EQ(both.m_oid_stage1, "1111");
    EXPECT_EQ(both.m_oid_stage2, "2222");
    EXPECT_EQ(both.m_oid_stage3, "3333");

    // An all-zero object name means that stage is absent, which is how
    // "deleted by us" is told apart from "both modified".
    const StatusEntry &deleted_by_us = snapshot.m_entries[1];
    EXPECT_EQ(deleted_by_us.conflict_code(), "DU");
    EXPECT_TRUE(deleted_by_us.m_oid_stage2.empty());
    EXPECT_EQ(deleted_by_us.m_oid_stage3, "3333");
}

TEST(StatusParser, PathsWithSpaces)
{
    const StatusSnapshot snapshot = StatusParser::parse(records({
        "? a file with spaces.txt",
        "1 .M N... 100644 100644 100644 aaaa bbbb dir with spaces/file.cpp",
    }));

    ASSERT_EQ(snapshot.m_entries.size(), 2u);
    EXPECT_EQ(snapshot.m_entries[0].m_path, "a file with spaces.txt");
    EXPECT_TRUE(snapshot.m_entries[0].is_unstaged());
    // The path is the remainder of the record, so it must not be split on spaces.
    EXPECT_EQ(snapshot.m_entries[1].m_path, "dir with spaces/file.cpp");
}

TEST(StatusParser, SubmoduleSubState)
{
    const StatusSnapshot snapshot = StatusParser::parse(records({"1 .M SCMU 160000 160000 160000 aaaa bbbb vendor/lib"}));

    ASSERT_EQ(snapshot.m_entries.size(), 1u);

    const StatusEntry &entry = snapshot.m_entries[0];
    EXPECT_TRUE(entry.m_is_submodule);
    EXPECT_TRUE(entry.m_submodule_commit_changed);
    EXPECT_TRUE(entry.m_submodule_has_modified);
    EXPECT_TRUE(entry.m_submodule_has_untracked);
}
