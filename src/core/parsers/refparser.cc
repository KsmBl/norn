/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "refparser.h"

#include <cstdlib>

namespace
{
/*! Field separator inside a record. %1f in a for-each-ref format. */
constexpr char s_field_separator = '\x1f';

std::vector<std::string> split(const std::string &value, char separator)
{
    std::vector<std::string> fields;
    std::size_t start = 0;

    while (start <= value.size()) {
        const std::size_t end = value.find(separator, start);
        if (end == std::string::npos) {
            fields.push_back(value.substr(start));
            break;
        }
        fields.push_back(value.substr(start, end - start));
        start = end + 1;
    }

    return fields;
}

std::string trimmed(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string join(const std::vector<std::string> &fields, std::size_t from, char separator)
{
    std::string joined;
    for (std::size_t i = from; i < fields.size(); ++i) {
        if (i > from) {
            joined.push_back(separator);
        }
        joined += fields[i];
    }
    return joined;
}

/*! "[ahead 3, behind 1]", "[gone]", or empty. */
void apply_tracking_info(const std::string &track, RefRecord &record)
{
    if (track.empty()) {
        return;
    }

    if (track.find("gone") != std::string::npos) {
        record.m_upstream_gone = true;
        return;
    }

    const std::size_t ahead = track.find("ahead ");
    if (ahead != std::string::npos) {
        record.m_ahead = std::atoi(track.c_str() + ahead + 6);
    }

    const std::size_t behind = track.find("behind ");
    if (behind != std::string::npos) {
        record.m_behind = std::atoi(track.c_str() + behind + 7);
    }
}

bool starts_with(const std::string &value, const char *prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool ends_with(const std::string &value, const std::string &suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

std::vector<std::string> RefParser::ref_arguments()
{
    return {
        "for-each-ref",
        "--format=%(refname)%1f%(refname:short)%1f%(objectname)%1f%(objecttype)%1f%(*objectname)"
        "%1f%(upstream:short)%1f%(upstream:track)%1f%(HEAD)%1f%(committerdate:iso-strict)%1f%(contents:subject)",
        "refs/heads",
        "refs/remotes",
        "refs/tags",
    };
}

std::vector<RefRecord> RefParser::parse_refs(const std::string &output)
{
    std::vector<RefRecord> records;

    for (const std::string &line : split(output, '\n')) {
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split(line, s_field_separator);
        if (fields.size() < 10) {
            continue;
        }

        RefRecord record;
        record.m_ref_name = fields[0];
        record.m_short_name = fields[1];
        record.m_object_name = fields[2];

        record.m_peeled_object_name = fields[4];
        // An annotated tag is its own object; a lightweight one points straight at
        // the commit, and only the former has a peeled object name.
        record.m_is_annotated_tag = fields[3] == "tag";

        record.m_upstream = fields[5];
        record.m_has_tracking = !record.m_upstream.empty();
        apply_tracking_info(fields[6], record);

        record.m_is_head = trimmed(fields[7]) == "*";
        record.m_date = fields[8];
        // The subject may itself contain the separator, so take everything left.
        record.m_subject = join(fields, 9, s_field_separator);

        if (starts_with(record.m_ref_name, "refs/heads/")) {
            record.m_kind = RefRecord::Kind::LocalBranch;
        } else if (starts_with(record.m_ref_name, "refs/remotes/")) {
            record.m_kind = RefRecord::Kind::RemoteBranch;
        } else if (starts_with(record.m_ref_name, "refs/tags/")) {
            record.m_kind = RefRecord::Kind::Tag;
        } else {
            continue;
        }

        // refs/remotes/<remote>/HEAD is a symbolic pointer, not a branch anyone
        // checks out; listing it just duplicates the branch it points at.
        if (record.m_kind == RefRecord::Kind::RemoteBranch && ends_with(record.m_short_name, "/HEAD")) {
            continue;
        }

        records.push_back(record);
    }

    return records;
}

std::vector<std::string> RefParser::stash_arguments()
{
    return {"stash", "list", "-z", "--format=%gd%x1f%H%x1f%gs%x1f%aI"};
}

std::vector<StashRecord> RefParser::parse_stashes(const std::string &output)
{
    std::vector<StashRecord> records;

    for (const std::string &entry : split(output, '\0')) {
        if (entry.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split(entry, s_field_separator);
        if (fields.size() < 4) {
            continue;
        }

        StashRecord record;
        record.m_selector = trimmed(fields[0]);
        record.m_object_name = fields[1];
        record.m_message = fields[2];
        record.m_date = fields[3];
        record.m_index = static_cast<int>(records.size());

        records.push_back(record);
    }

    return records;
}

std::vector<std::string> RefParser::worktree_arguments()
{
    return {"worktree", "list", "--porcelain"};
}

std::vector<WorktreeRecord> RefParser::parse_worktrees(const std::string &output, const std::string &current_path)
{
    std::vector<WorktreeRecord> records;

    // Records are separated by a blank line; each is a series of "key value" or
    // bare-keyword lines.
    WorktreeRecord current;
    bool started = false;

    const auto flush = [&] {
        if (started) {
            // The first entry git reports is always the main worktree, and it is the
            // only one that cannot be removed.
            current.m_is_main = records.empty();
            current.m_is_current = current.m_path == current_path;
            records.push_back(current);
            current = WorktreeRecord();
            started = false;
        }
    };

    for (const std::string &raw : split(output, '\n')) {
        const std::string line = trimmed(raw);
        if (line.empty()) {
            flush();
            continue;
        }

        const std::size_t space = line.find(' ');
        const std::string key = space == std::string::npos ? line : line.substr(0, space);
        const std::string value = space == std::string::npos ? std::string() : line.substr(space + 1);

        if (key == "worktree") {
            flush();
            started = true;
            current.m_path = value;
        } else if (key == "HEAD") {
            current.m_head = value;
        } else if (key == "branch") {
            current.m_branch = starts_with(value, "refs/heads/") ? value.substr(11) : value;
        } else if (key == "detached") {
            current.m_is_detached = true;
        } else if (key == "locked") {
            current.m_is_locked = true;
            current.m_lock_reason = value;
        } else if (key == "prunable") {
            current.m_is_prunable = true;
        }
    }

    flush();
    return records;
}

std::vector<std::string> RefParser::submodule_arguments()
{
    return {"submodule", "status", "--recursive"};
}

std::vector<SubmoduleRecord> RefParser::parse_submodules(const std::string &output)
{
    std::vector<SubmoduleRecord> records;

    // " <sha> <path> (<describe>)", where the leading character carries the state:
    // '-' not initialised, '+' checked out at a different commit, 'U' conflicts,
    // and a space for up to date.
    for (const std::string &line : split(output, '\n')) {
        if (trimmed(line).empty()) {
            continue;
        }

        SubmoduleRecord record;

        const char state = line[0];
        record.m_is_uninitialised = state == '-';
        record.m_is_modified = state == '+';
        record.m_has_conflicts = state == 'U';

        const std::string rest = line.substr(1);
        const std::size_t first_space = rest.find(' ');
        if (first_space == std::string::npos) {
            continue;
        }

        record.m_commit = rest.substr(0, first_space);

        std::string remainder = rest.substr(first_space + 1);

        // The describe suffix is parenthesised and optional.
        const std::size_t open_paren = remainder.rfind(" (");
        if (open_paren != std::string::npos && ends_with(remainder, ")")) {
            record.m_describe = remainder.substr(open_paren + 2, remainder.size() - open_paren - 3);
            remainder = remainder.substr(0, open_paren);
        }

        record.m_path = remainder;
        records.push_back(record);
    }

    return records;
}
