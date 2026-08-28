/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

/*! One line inside a hunk. */
class DiffLine
{
public:
    enum class Kind {
        /*! A line present on both sides. */
        Context,
        /*! Present only in the old version. */
        Removed,
        /*! Present only in the new version. */
        Added,
        /*!
         * The `\ No newline at end of file` marker. Belongs to the line before it
         * and is never independently selectable.
         */
        NoNewline,
    };

    Kind m_kind = Kind::Context;

    /*! Line content without the leading +, - or space. Raw bytes, never re-encoded. */
    std::string m_text;

    /*! 1-based line number in the old file, or 0 if the line is not in it. */
    int m_old_line = 0;
    /*! 1-based line number in the new file, or 0 if the line is not in it. */
    int m_new_line = 0;

    bool is_change() const
    {
        return m_kind == Kind::Added || m_kind == Kind::Removed;
    }
};

/*! One `@@` block. */
class DiffHunk
{
public:
    int m_old_start = 0;
    int m_old_count = 0;
    int m_new_start = 0;
    int m_new_count = 0;

    /*! Text after the closing `@@`, usually the enclosing function. */
    std::string m_heading;

    std::vector<DiffLine> m_lines;

    /*!
     * True when the hunk contains a no-newline marker.
     *
     * Line-level selection is refused for such hunks. Rewriting the marker under a
     * partial selection requires knowing whether the other side also lacks a
     * trailing newline; guessing wrong produces a patch that corrupts the file's
     * last line, and whole-hunk staging is always correct.
     */
    bool m_has_no_newline_marker = false;

    int change_count() const
    {
        int count = 0;
        for (const DiffLine &line : m_lines) {
            if (line.is_change()) {
                ++count;
            }
        }
        return count;
    }
};

/*! One file's diff. */
class DiffFile
{
public:
    std::string m_old_path;
    std::string m_new_path;

    /*!
     * The header lines before the first `@@`, kept verbatim so a generated patch
     * carries the same `diff --git`, `index` and `---`/`+++` lines git produced.
     */
    std::vector<std::string> m_header_lines;

    /*! Set when the header carried `old mode`/`new mode`. */
    bool m_has_mode_change = false;
    /*! Set for `rename from`/`rename to`. Partial staging is refused for these. */
    bool m_is_rename = false;
    /*! Set when git reported the content as binary. */
    bool m_is_binary = false;
    bool m_is_new_file = false;
    bool m_is_deleted_file = false;

    std::vector<DiffHunk> m_hunks;

    /*!
     * Whether individual hunks and lines can be staged separately.
     *
     * A rename cannot: the `rename from`/`rename to` headers carry no content, so
     * there is nothing partial to express. Binary cannot either. git's own
     * `add --patch` refuses both.
     */
    bool supports_partial_staging() const
    {
        return !m_is_rename && !m_is_binary;
    }
};

/*! A whole `git diff` invocation's output. */
class DiffDocument
{
public:
    std::vector<DiffFile> m_files;

    bool empty() const
    {
        return m_files.empty();
    }
};
