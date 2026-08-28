/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "patchbuilder.h"

namespace
{
bool starts_with(const std::string &value, const char *prefix)
{
    return value.rfind(prefix, 0) == 0;
}

/*! Header lines that must not appear in a content-only patch. */
bool is_mode_header(const std::string &line)
{
    return starts_with(line, "old mode ") || starts_with(line, "new mode ");
}

/*!
 * Emits the file header.
 *
 * Mode headers are dropped: including them in a patch that only stages content
 * hunks would silently stage a chmod the user did not select. Staging a mode
 * change is a separate, explicit action.
 */
std::string build_header(const DiffFile &file)
{
    std::string header;
    for (const std::string &line : file.m_header_lines) {
        if (is_mode_header(line)) {
            continue;
        }
        header += line;
        header.push_back('\n');
    }
    return header;
}

char prefix_for(DiffLine::Kind kind)
{
    switch (kind) {
    case DiffLine::Kind::Added:
        return '+';
    case DiffLine::Kind::Removed:
        return '-';
    case DiffLine::Kind::Context:
        return ' ';
    case DiffLine::Kind::NoNewline:
        return '\\';
    }
    return ' ';
}

/*! The role a line takes in the emitted patch, after the direction rule is applied. */
DiffLine::Kind role_for(const DiffLine &line, bool selected, PatchBuilder::Direction direction)
{
    if (!line.is_change() || selected) {
        return line.m_kind;
    }

    // Unselected change. Which side survives depends on which side the patch is
    // being applied against.
    const bool old_side_is_target = direction == PatchBuilder::Direction::Stage;

    if (old_side_is_target) {
        // Applying to the index against its old content: removals that were not
        // chosen still exist there and must appear as context; additions that were
        // not chosen do not exist there at all.
        return line.m_kind == DiffLine::Kind::Removed ? DiffLine::Kind::Context : DiffLine::Kind::NoNewline;
    }

    // Reverse-applying: the new side is the current content. Unselected additions
    // are present and become context; unselected removals are already gone.
    return line.m_kind == DiffLine::Kind::Added ? DiffLine::Kind::Context : DiffLine::Kind::NoNewline;
}

/*!
 * Emits one hunk with @p selected deciding which changed lines are included.
 *
 * @p delta accumulates the drift between old and new line numbering across the
 * hunks already emitted for this file, so the rewritten @@ header stays consistent.
 * Returns false when the hunk ends up containing no change at all, in which case
 * it must not be emitted: a context-only hunk is a no-op that git rejects.
 */
bool build_hunk(const DiffHunk &hunk, const std::set<int> &selected, bool select_all, PatchBuilder::Direction direction, int &delta, std::string &out)
{
    std::string body;
    int old_count = 0;
    int new_count = 0;
    int changes = 0;

    for (std::size_t i = 0; i < hunk.m_lines.size(); ++i) {
        const DiffLine &line = hunk.m_lines[i];

        if (line.m_kind == DiffLine::Kind::NoNewline) {
            // Belongs to the preceding line; carried through verbatim.
            body += "\\ ";
            body += line.m_text;
            body.push_back('\n');
            continue;
        }

        const bool is_selected = select_all || selected.count(static_cast<int>(i)) > 0;
        const DiffLine::Kind role = role_for(line, is_selected, direction);

        // NoNewline is reused here as the "drop this line" marker.
        if (role == DiffLine::Kind::NoNewline) {
            continue;
        }

        body.push_back(prefix_for(role));
        body += line.m_text;
        body.push_back('\n');

        switch (role) {
        case DiffLine::Kind::Context:
            ++old_count;
            ++new_count;
            break;
        case DiffLine::Kind::Removed:
            ++old_count;
            ++changes;
            break;
        case DiffLine::Kind::Added:
            ++new_count;
            ++changes;
            break;
        case DiffLine::Kind::NoNewline:
            break;
        }
    }

    if (changes == 0) {
        return false;
    }

    // The old side is unchanged by selection, so old_start is git's original. The
    // new side has drifted by everything emitted so far.
    const int new_start = hunk.m_old_start + delta;
    delta += new_count - old_count;

    out += "@@ -" + std::to_string(hunk.m_old_start) + "," + std::to_string(old_count) //
        + " +" + std::to_string(new_start) + "," + std::to_string(new_count) + " @@";
    if (!hunk.m_heading.empty()) {
        out.push_back(' ');
        out += hunk.m_heading;
    }
    out.push_back('\n');
    out += body;

    return true;
}
}

std::string PatchBuilder::build_for_hunks(const DiffFile &file, const std::set<int> &hunk_indexes, Direction direction)
{
    if (hunk_indexes.empty() || !file.supports_partial_staging()) {
        return {};
    }

    std::string body;
    int delta = 0;
    for (std::size_t i = 0; i < file.m_hunks.size(); ++i) {
        if (hunk_indexes.count(static_cast<int>(i)) == 0) {
            continue;
        }
        build_hunk(file.m_hunks[i], {}, true, direction, delta, body);
    }

    return body.empty() ? std::string() : build_header(file) + body;
}

std::string PatchBuilder::build_for_lines(const DiffFile &file, const std::map<int, std::set<int>> &selected_lines, Direction direction)
{
    if (selected_lines.empty() || !file.supports_partial_staging()) {
        return {};
    }

    std::string body;
    int delta = 0;
    for (std::size_t i = 0; i < file.m_hunks.size(); ++i) {
        const auto selection = selected_lines.find(static_cast<int>(i));
        if (selection == selected_lines.end() || selection->second.empty()) {
            continue;
        }

        const DiffHunk &hunk = file.m_hunks[i];

        // A no-newline marker makes partial selection unsafe: converting the final
        // removal into context asserts that both sides lack a trailing newline,
        // which may be false and would corrupt the last line. Take the whole hunk.
        const bool select_all = hunk.m_has_no_newline_marker;

        build_hunk(hunk, selection->second, select_all, direction, delta, body);
    }

    return body.empty() ? std::string() : build_header(file) + body;
}

bool PatchBuilder::needs_unidiff_zero(const std::string &patch)
{
    std::size_t start = 0;
    while (start < patch.size()) {
        const std::size_t end = patch.find('\n', start);
        const std::string line = patch.substr(start, end == std::string::npos ? std::string::npos : end - start);

        if (starts_with(line, "@@")) {
            if (line.find(",0 +") != std::string::npos || line.find(",0 @@") != std::string::npos) {
                return true;
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

std::vector<std::string> PatchBuilder::apply_arguments(Direction direction, bool dry_run, bool unidiff_zero)
{
    std::vector<std::string> args{"apply"};

    if (direction != Direction::Discard) {
        // Index only. Never --index, which additionally requires the index and
        // worktree to be identical for the path — precisely the case never in force
        // when someone is staging part of a change.
        args.emplace_back("--cached");
    }

    if (direction != Direction::Stage) {
        args.emplace_back("--reverse");
    }

    if (dry_run) {
        args.emplace_back("--check");
    }

    if (unidiff_zero) {
        args.emplace_back("--unidiff-zero");
    }

    // Prevents a repository with core.whitespace settings from spraying warnings.
    // Never --whitespace=fix, which silently alters content.
    args.emplace_back("--whitespace=nowarn");
    args.emplace_back("-");

    return args;
}
