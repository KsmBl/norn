/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "statusparser.h"

#include <cstdlib>

namespace
{
/*! Splits on NUL, keeping empty trailing pieces out. */
std::vector<std::string> split_nul(const std::string &output)
{
    std::vector<std::string> records;
    std::size_t start = 0;

    while (start <= output.size()) {
        const std::size_t end = output.find('\0', start);
        if (end == std::string::npos) {
            if (start < output.size()) {
                records.push_back(output.substr(start));
            }
            break;
        }
        records.push_back(output.substr(start, end - start));
        start = end + 1;
    }

    return records;
}

std::vector<std::string> split_space(const std::string &value)
{
    std::vector<std::string> fields;
    std::size_t start = 0;

    while (start < value.size()) {
        const std::size_t end = value.find(' ', start);
        if (end == std::string::npos) {
            fields.push_back(value.substr(start));
            break;
        }
        fields.push_back(value.substr(start, end - start));
        start = end + 1;
    }

    return fields;
}

unsigned parse_octal_mode(const std::string &field)
{
    return static_cast<unsigned>(std::strtoul(field.c_str(), nullptr, 8));
}

/*! An all-zero object name means that conflict stage is absent. */
std::string oid_or_empty(const std::string &field)
{
    return field.find_first_not_of('0') == std::string::npos ? std::string() : field;
}

void parse_branch_header(const std::string &record, StatusSnapshot &snapshot)
{
    // "# branch.oid <sha>", "# branch.head <name>", "# branch.ab +1 -2", "# stash 3"
    const std::vector<std::string> fields = split_space(record);
    if (fields.size() < 3) {
        return;
    }

    const std::string &key = fields[1];
    const std::string &value = fields[2];

    if (key == "branch.oid") {
        if (value == "(initial)") {
            snapshot.m_is_unborn = true;
        } else {
            snapshot.m_head_oid = value;
        }
    } else if (key == "branch.head") {
        if (value == "(detached)") {
            snapshot.m_is_detached = true;
        } else {
            snapshot.m_branch = value;
        }
    } else if (key == "branch.upstream") {
        snapshot.m_upstream = value;
    } else if (key == "branch.ab" && fields.size() >= 4) {
        // Written as "+3 -2"; the signs are part of the format, not information.
        snapshot.m_ahead = std::abs(std::atoi(value.c_str()));
        snapshot.m_behind = std::abs(std::atoi(fields[3].c_str()));
        snapshot.m_has_ahead_behind = true;
    } else if (key == "stash") {
        snapshot.m_stash_count = std::atoi(value.c_str());
    }
}

void apply_submodule_field(const std::string &field, StatusEntry &entry)
{
    // "N..." for a plain path, "S<c><m><u>" for a submodule.
    if (field.size() != 4 || field[0] != 'S') {
        return;
    }

    entry.m_is_submodule = true;
    entry.m_submodule_commit_changed = field[1] == 'C';
    entry.m_submodule_has_modified = field[2] == 'M';
    entry.m_submodule_has_untracked = field[3] == 'U';
}

void set_xy(const std::string &xy, StatusEntry &entry)
{
    if (xy.size() < 2) {
        return;
    }
    entry.m_index_status = xy[0];
    entry.m_worktree_status = xy[1];
}

/*!
 * Splits a record into @p count leading space-separated fields plus the remainder,
 * which is the path and must not be split because it may contain spaces.
 */
bool split_fields(const std::string &record, int count, std::vector<std::string> &fields, std::string &remainder)
{
    std::size_t offset = 0;
    for (int i = 0; i < count; ++i) {
        const std::size_t space = record.find(' ', offset);
        if (space == std::string::npos) {
            return false;
        }
        fields.push_back(record.substr(offset, space - offset));
        offset = space + 1;
    }
    remainder = record.substr(offset);
    return true;
}
}

std::vector<std::string> StatusParser::arguments()
{
    return {
        "status",
        "--porcelain=v2",
        "-z",
        "--branch",
        "--show-stash",
        "--untracked-files=all",
        "--ignored=no",
        "--ahead-behind",
        // Renames are what let the UI show "old -> new" instead of a delete plus an add.
        "--renames",
        "--find-renames=50%",
        // Submodule sub-state comes back in the <sub> field, so the sidebar gets it free.
        "--ignore-submodules=none",
    };
}

StatusSnapshot StatusParser::parse(const std::string &output)
{
    StatusSnapshot snapshot;

    const std::vector<std::string> records = split_nul(output);

    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::string &record = records[i];
        if (record.empty()) {
            continue;
        }

        const char type = record[0];

        if (type == '#') {
            parse_branch_header(record, snapshot);
            continue;
        }

        StatusEntry entry;
        std::vector<std::string> fields;
        std::string path;

        switch (type) {
        case '1': {
            // 1 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <path>
            if (!split_fields(record, 8, fields, path)) {
                continue;
            }
            entry.m_kind = StatusEntry::Kind::Ordinary;
            set_xy(fields[1], entry);
            apply_submodule_field(fields[2], entry);
            entry.m_mode_head = parse_octal_mode(fields[3]);
            entry.m_mode_index = parse_octal_mode(fields[4]);
            entry.m_mode_worktree = parse_octal_mode(fields[5]);
            entry.m_oid_head = fields[6];
            entry.m_oid_index = fields[7];
            break;
        }
        case '2': {
            // 2 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <X><score> <path>\0<origPath>
            //
            // The second NUL-terminated token belongs to this record. A parser that
            // treats every token as its own record desynchronises on the first
            // rename and misattributes everything after it.
            if (!split_fields(record, 9, fields, path)) {
                continue;
            }
            entry.m_kind = StatusEntry::Kind::RenamedOrCopied;
            set_xy(fields[1], entry);
            apply_submodule_field(fields[2], entry);
            entry.m_mode_head = parse_octal_mode(fields[3]);
            entry.m_mode_index = parse_octal_mode(fields[4]);
            entry.m_mode_worktree = parse_octal_mode(fields[5]);
            entry.m_oid_head = fields[6];
            entry.m_oid_index = fields[7];
            entry.m_score = std::atoi(fields[8].c_str() + 1);

            if (i + 1 < records.size()) {
                entry.m_orig_path = records[++i];
            }
            break;
        }
        case 'u': {
            // u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path>
            if (!split_fields(record, 10, fields, path)) {
                continue;
            }
            entry.m_kind = StatusEntry::Kind::Unmerged;
            set_xy(fields[1], entry);
            apply_submodule_field(fields[2], entry);
            entry.m_mode_worktree = parse_octal_mode(fields[6]);
            entry.m_oid_stage1 = oid_or_empty(fields[7]);
            entry.m_oid_stage2 = oid_or_empty(fields[8]);
            entry.m_oid_stage3 = oid_or_empty(fields[9]);
            break;
        }
        case '?':
        case '!': {
            entry.m_kind = type == '?' ? StatusEntry::Kind::Untracked : StatusEntry::Kind::Ignored;
            path = record.substr(2);
            break;
        }
        default:
            continue;
        }

        entry.m_path = path;
        snapshot.m_entries.push_back(entry);
    }

    return snapshot;
}
