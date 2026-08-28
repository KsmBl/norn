/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/parsers/diffparser.h"
#include "core/patchbuilder.h"
#include "core/pathspec.h"
#include "testrepo.h"

#include <gtest/gtest.h>

namespace
{
/*! Reads the unstaged, or staged, diff for one path. */
DiffFile diff_for(const TestRepo &repo, const std::string &path, bool cached = false)
{
    std::vector<std::string> args{"diff"};
    if (cached) {
        args.emplace_back("--cached");
    }
    const std::vector<std::string> diff_args = DiffParser::arguments();
    args.insert(args.end(), diff_args.begin(), diff_args.end());
    args.emplace_back("--");
    args.push_back(Pathspec::literal(path));

    const DiffDocument document = DiffParser::parse(repo.git_raw(args));
    return document.m_files.empty() ? DiffFile() : document.m_files.front();
}

/*! Index of the line whose text is @p text, or -1. */
int index_of_line(const DiffHunk &hunk, const std::string &text)
{
    for (std::size_t i = 0; i < hunk.m_lines.size(); ++i) {
        if (hunk.m_lines[i].m_text == text) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/*!
 * Applies @p patch the way the application does, dry run first.
 *
 * The dry run is not decoration: a patch git would reject has to be caught before
 * it has touched anything, and a subtly wrong one applies cleanly and stages the
 * wrong thing rather than failing.
 */
void apply(const TestRepo &repo, const std::string &patch, PatchBuilder::Direction direction)
{
    ASSERT_FALSE(patch.empty());

    const bool zero = PatchBuilder::needs_unidiff_zero(patch);

    EXPECT_EQ(repo.git_stdin(PatchBuilder::apply_arguments(direction, true, zero), patch), 0) << "dry run refused the patch";
    EXPECT_EQ(repo.git_stdin(PatchBuilder::apply_arguments(direction, false, zero), patch), 0) << "apply refused the patch";
}
}

TEST(DiffParser, HunkHeaderWithOmittedCounts)
{
    // git writes "@@ -5 +5,3 @@" rather than "@@ -5,1 +5,3 @@". A parser defaulting
    // the omitted count to 0 instead of 1 corrupts every single-line hunk.
    const DiffDocument document = DiffParser::parse("diff --git a/f b/f\n"
                                                    "--- a/f\n"
                                                    "+++ b/f\n"
                                                    "@@ -5 +5,3 @@\n"
                                                    "-old\n"
                                                    "+new\n"
                                                    "+extra\n"
                                                    "+more\n");

    ASSERT_EQ(document.m_files.size(), 1u);
    ASSERT_EQ(document.m_files.front().m_hunks.size(), 1u);

    const DiffHunk &hunk = document.m_files.front().m_hunks.front();
    EXPECT_EQ(hunk.m_old_start, 5);
    EXPECT_EQ(hunk.m_old_count, 1);
    EXPECT_EQ(hunk.m_new_start, 5);
    EXPECT_EQ(hunk.m_new_count, 3);
}

TEST(PatchBuilder, StageWholeHunk)
{
    TestRepo repo;
    repo.write_file("f.txt", "a\nb\nc\n");
    repo.commit_all("base");

    repo.write_file("f.txt", "a\nCHANGED\nc\n");

    const DiffFile file = diff_for(repo, "f.txt");
    ASSERT_EQ(file.m_hunks.size(), 1u);

    apply(repo, PatchBuilder::build_for_hunks(file, {0}, PatchBuilder::Direction::Stage), PatchBuilder::Direction::Stage);

    EXPECT_EQ(repo.git({"show", ":f.txt"}), "a\nCHANGED\nc\n");
    EXPECT_TRUE(repo.git({"diff", "--name-only"}).empty());
}

TEST(PatchBuilder, StageOneHunkOfTwo)
{
    TestRepo repo;
    repo.write_file("f.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n");
    repo.commit_all("base");

    // Two changes far enough apart to produce separate hunks.
    repo.write_file("f.txt", "1\nTWO\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\nFOURTEEN\n15\n");

    const DiffFile file = diff_for(repo, "f.txt");
    ASSERT_EQ(file.m_hunks.size(), 2u);

    // Stage only the second hunk. The first must stay entirely unstaged.
    apply(repo, PatchBuilder::build_for_hunks(file, {1}, PatchBuilder::Direction::Stage), PatchBuilder::Direction::Stage);

    EXPECT_EQ(repo.git({"show", ":f.txt"}), "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\nFOURTEEN\n15\n");
}

TEST(PatchBuilder, StageSingleAddedLine)
{
    TestRepo repo;
    repo.write_file("f.txt", "a\nb\n");
    repo.commit_all("base");

    repo.write_file("f.txt", "a\nfirst\nsecond\nb\n");

    const DiffFile file = diff_for(repo, "f.txt");
    ASSERT_EQ(file.m_hunks.size(), 1u);

    const int first = index_of_line(file.m_hunks.front(), "first");
    ASSERT_GE(first, 0);

    apply(repo, PatchBuilder::build_for_lines(file, {{0, {first}}}, PatchBuilder::Direction::Stage), PatchBuilder::Direction::Stage);

    // Only "first" made it into the index; "second" is still only in the worktree.
    EXPECT_EQ(repo.git({"show", ":f.txt"}), "a\nfirst\nb\n");
    EXPECT_EQ(repo.read_file("f.txt"), "a\nfirst\nsecond\nb\n");
}

TEST(PatchBuilder, StageSingleRemovedLine)
{
    TestRepo repo;
    repo.write_file("f.txt", "keep\ndrop1\ndrop2\nkeep2\n");
    repo.commit_all("base");

    repo.write_file("f.txt", "keep\nkeep2\n");

    const DiffFile file = diff_for(repo, "f.txt");
    const int drop_one = index_of_line(file.m_hunks.front(), "drop1");
    ASSERT_GE(drop_one, 0);

    apply(repo, PatchBuilder::build_for_lines(file, {{0, {drop_one}}}, PatchBuilder::Direction::Stage), PatchBuilder::Direction::Stage);

    // Only drop1 was removed in the index; drop2 must survive there. This is the
    // case that breaks when unselected removals are dropped instead of becoming
    // context lines.
    EXPECT_EQ(repo.git({"show", ":f.txt"}), "keep\ndrop2\nkeep2\n");
}

TEST(PatchBuilder, UnstageSingleLine)
{
    TestRepo repo;
    repo.write_file("f.txt", "a\n");
    repo.commit_all("base");

    repo.write_file("f.txt", "a\nx\ny\n");
    repo.git({"add", "-A"});

    // Both lines are staged; take just "x" back out.
    const DiffFile file = diff_for(repo, "f.txt", true);
    const int x = index_of_line(file.m_hunks.front(), "x");
    ASSERT_GE(x, 0);

    apply(repo, PatchBuilder::build_for_lines(file, {{0, {x}}}, PatchBuilder::Direction::Unstage), PatchBuilder::Direction::Unstage);

    // "y" stays staged, "x" does not, and the worktree is untouched.
    EXPECT_EQ(repo.git({"show", ":f.txt"}), "a\ny\n");
    EXPECT_EQ(repo.read_file("f.txt"), "a\nx\ny\n");
}

TEST(PatchBuilder, DiscardHunkRestoresContent)
{
    TestRepo repo;
    repo.write_file("f.txt", "a\nb\nc\n");
    repo.commit_all("base");

    repo.write_file("f.txt", "a\nWRONG\nc\n");

    const DiffFile file = diff_for(repo, "f.txt");
    apply(repo, PatchBuilder::build_for_hunks(file, {0}, PatchBuilder::Direction::Discard), PatchBuilder::Direction::Discard);

    EXPECT_EQ(repo.read_file("f.txt"), "a\nb\nc\n");
}

TEST(PatchBuilder, ModeChangeIsNotSmuggledIn)
{
    TestRepo repo;
    repo.write_file("s.sh", "one\n");
    repo.commit_all("base");

    // Change the content and the mode at once.
    repo.write_file("s.sh", "two\n");
    repo.chmod_executable("s.sh");

    const DiffFile file = diff_for(repo, "s.sh");
    ASSERT_TRUE(file.m_has_mode_change);

    apply(repo, PatchBuilder::build_for_hunks(file, {0}, PatchBuilder::Direction::Stage), PatchBuilder::Direction::Stage);

    // The content is staged, but the mode in the index must still be the old one:
    // staging a hunk must not silently stage a chmod the user never selected.
    EXPECT_EQ(repo.git({"show", ":s.sh"}), "two\n");
    EXPECT_EQ(repo.git({"ls-files", "--stage", "s.sh"}).substr(0, 6), "100644");
}

TEST(PatchBuilder, NoNewlineHunkIsStagedWhole)
{
    TestRepo repo;
    // No trailing newline on either side.
    repo.write_file("f.txt", "a\nb\nlast");
    repo.commit_all("base");

    repo.write_file("f.txt", "a\nCHANGED\nlast-changed");

    const DiffFile file = diff_for(repo, "f.txt");
    ASSERT_EQ(file.m_hunks.size(), 1u);
    ASSERT_TRUE(file.m_hunks.front().m_has_no_newline_marker);

    // Ask for only one line. Because the hunk carries a no-newline marker, the whole
    // hunk is promoted rather than risking a corrupted final line.
    const int changed = index_of_line(file.m_hunks.front(), "CHANGED");
    ASSERT_GE(changed, 0);

    apply(repo, PatchBuilder::build_for_lines(file, {{0, {changed}}}, PatchBuilder::Direction::Stage), PatchBuilder::Direction::Stage);

    EXPECT_EQ(repo.git({"show", ":f.txt"}), "a\nCHANGED\nlast-changed");
}

TEST(PatchBuilder, RenamesRefusePartialStaging)
{
    DiffFile file;
    file.m_is_rename = true;
    file.m_hunks.emplace_back();

    // A rename's headers carry no content, so there is no partial rename to express;
    // git's own add --patch refuses these too.
    EXPECT_FALSE(file.supports_partial_staging());
    EXPECT_TRUE(PatchBuilder::build_for_hunks(file, {0}, PatchBuilder::Direction::Stage).empty());
}
