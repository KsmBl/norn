/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "diffparser.h"

#include <cstdlib>

namespace
{
bool starts_with(const std::string &value, const char *prefix)
{
    return value.rfind(prefix, 0) == 0;
}

/*! Splits into lines, without a trailing empty element. */
std::vector<std::string> split_lines(const std::string &output)
{
    std::vector<std::string> lines;
    std::size_t start = 0;

    while (start <= output.size()) {
        const std::size_t end = output.find('\n', start);
        if (end == std::string::npos) {
            if (start < output.size()) {
                lines.push_back(output.substr(start));
            }
            break;
        }
        lines.push_back(output.substr(start, end - start));
        start = end + 1;
    }

    return lines;
}

/*! Strips the `a/` or `b/` prefix from a header path. */
std::string strip_prefix(const std::string &path)
{
    if (starts_with(path, "a/") || starts_with(path, "b/")) {
        return path.substr(2);
    }
    return path;
}

/*!
 * Parses `@@ -a,b +c,d @@ heading`.
 *
 * The counts are optional: git omits them when they are 1, and defaulting a
 * missing one to 0 rather than 1 corrupts every single-line hunk.
 */
bool parse_hunk_header(const std::string &line, DiffHunk &hunk)
{
    if (!starts_with(line, "@@ -")) {
        return false;
    }

    const std::size_t close = line.find(" @@");
    if (close == std::string::npos) {
        return false;
    }

    const std::string ranges = line.substr(4, close - 4);
    const std::size_t plus = ranges.find(" +");
    if (plus == std::string::npos) {
        return false;
    }

    const auto parse_range = [](const std::string &range, int &start, int &count) {
        const std::size_t comma = range.find(',');
        if (comma == std::string::npos) {
            start = std::atoi(range.c_str());
            count = 1;
        } else {
            start = std::atoi(range.substr(0, comma).c_str());
            count = std::atoi(range.substr(comma + 1).c_str());
        }
    };

    parse_range(ranges.substr(0, plus), hunk.m_old_start, hunk.m_old_count);
    parse_range(ranges.substr(plus + 2), hunk.m_new_start, hunk.m_new_count);

    if (close + 3 < line.size()) {
        hunk.m_heading = line.substr(close + 4);
    }

    return true;
}
}

std::vector<std::string> DiffParser::arguments(int context_lines)
{
    return {
        "--no-color",
        // A configured external differ would otherwise replace the output entirely.
        "--no-ext-diff",
        "--unified=" + std::to_string(context_lines),
        // Forced so the generated patch always matches what `git apply -p1` expects,
        // whatever diff.noprefix or diff.mnemonicPrefix are set to.
        "--src-prefix=a/",
        "--dst-prefix=b/",
    };
}

DiffDocument DiffParser::parse(const std::string &output)
{
    DiffDocument document;

    DiffFile file;
    DiffHunk hunk;
    bool in_file = false;
    bool in_hunk = false;
    int old_line = 0;
    int new_line = 0;

    const auto flush_hunk = [&] {
        if (in_hunk) {
            file.m_hunks.push_back(hunk);
            hunk = DiffHunk();
            in_hunk = false;
        }
    };

    const auto flush_file = [&] {
        flush_hunk();
        if (in_file) {
            document.m_files.push_back(file);
            file = DiffFile();
            in_file = false;
        }
    };

    for (const std::string &line : split_lines(output)) {
        if (starts_with(line, "diff --git ")) {
            flush_file();
            in_file = true;
            file.m_header_lines.push_back(line);
            continue;
        }

        if (!in_file) {
            continue;
        }

        // Everything between `diff --git` and the first `@@` is header.
        if (!in_hunk && !starts_with(line, "@@")) {
            file.m_header_lines.push_back(line);

            if (starts_with(line, "old mode ") || starts_with(line, "new mode ")) {
                file.m_has_mode_change = true;
            } else if (starts_with(line, "rename from ") || starts_with(line, "rename to ")) {
                file.m_is_rename = true;
            } else if (starts_with(line, "new file mode ")) {
                file.m_is_new_file = true;
            } else if (starts_with(line, "deleted file mode ")) {
                file.m_is_deleted_file = true;
            } else if (starts_with(line, "Binary files ") || starts_with(line, "GIT binary patch")) {
                file.m_is_binary = true;
            } else if (starts_with(line, "--- ")) {
                const std::string path = line.substr(4);
                file.m_old_path = path == "/dev/null" ? std::string() : strip_prefix(path);
            } else if (starts_with(line, "+++ ")) {
                const std::string path = line.substr(4);
                if (path != "/dev/null") {
                    file.m_new_path = strip_prefix(path);
                }
            }
            continue;
        }

        if (starts_with(line, "@@")) {
            flush_hunk();

            if (!parse_hunk_header(line, hunk)) {
                continue;
            }

            old_line = hunk.m_old_start;
            new_line = hunk.m_new_start;
            in_hunk = true;
            continue;
        }

        DiffLine parsed;

        if (!line.empty() && line[0] == '\\') {
            // "\ No newline at end of file". Matched on the leading backslash rather
            // than the English text, which is not stable across locales.
            parsed.m_kind = DiffLine::Kind::NoNewline;
            parsed.m_text = line.size() > 2 ? line.substr(2) : std::string();
            hunk.m_has_no_newline_marker = true;
        } else if (!line.empty() && line[0] == '+') {
            parsed.m_kind = DiffLine::Kind::Added;
            parsed.m_text = line.substr(1);
            parsed.m_new_line = new_line++;
        } else if (!line.empty() && line[0] == '-') {
            parsed.m_kind = DiffLine::Kind::Removed;
            parsed.m_text = line.substr(1);
            parsed.m_old_line = old_line++;
        } else if (line.empty() || line[0] == ' ') {
            // Some producers emit a bare empty line for an empty context line rather
            // than a single space.
            parsed.m_kind = DiffLine::Kind::Context;
            parsed.m_text = line.empty() ? std::string() : line.substr(1);
            parsed.m_old_line = old_line++;
            parsed.m_new_line = new_line++;
        } else {
            // Not part of the hunk body; the hunk has ended.
            flush_hunk();
            continue;
        }

        hunk.m_lines.push_back(parsed);
    }

    flush_file();
    return document;
}
