/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "logparser.h"

namespace
{
/*! Field separator inside a commit record. %x1f in the format string. */
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

std::vector<std::string> split_non_empty(const std::string &value, const std::string &separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;

    while (start < value.size()) {
        const std::size_t end = value.find(separator, start);
        const std::string part = trimmed(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!part.empty()) {
            parts.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + separator.size();
    }

    return parts;
}
}

std::vector<std::string> LogParser::log_arguments(int limit, int skip)
{
    std::vector<std::string> args{
        "log",
        // NUL between records, so a subject or body containing newlines cannot be
        // mistaken for a record boundary.
        "-z",
        // Topological order guarantees no commit is emitted before all of its
        // children, which is exactly the invariant incremental lane assignment
        // depends on. Date order interleaves branches and produces crossings.
        "--topo-order",
        "--decorate=full",
        "--max-count=" + std::to_string(limit),
        "--format=%H%x1f%h%x1f%P%x1f%an%x1f%ae%x1f%aI%x1f%cn%x1f%cI%x1f%D%x1f%s%x1f%b",
        // Branches, tags and remote branches, so history other than the checked-out
        // branch is drawn — but deliberately not --all, which also pulls in
        // refs/stash and renders each stash's internal index commit as though it
        // were part of the history.
        "--branches",
        "--tags",
        "--remotes",
        // Explicitly, so a detached HEAD still appears.
        "HEAD",
    };

    if (skip > 0) {
        args.push_back("--skip=" + std::to_string(skip));
    }

    return args;
}

std::vector<CommitRecord> LogParser::parse(const std::string &output)
{
    std::vector<CommitRecord> commits;

    for (const std::string &raw : split(output, '\0')) {
        // Records after the first are preceded by the newline git writes between
        // entries, so trim before deciding the record is empty.
        const std::string record = trimmed(raw);
        if (record.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split(record, s_field_separator);
        if (fields.size() < 11) {
            continue;
        }

        CommitRecord commit;
        commit.m_object_name = fields[0];
        commit.m_abbreviated_name = fields[1];
        commit.m_parents = split_non_empty(fields[2], " ");
        commit.m_author_name = fields[3];
        commit.m_author_email = fields[4];
        commit.m_author_date = fields[5];
        commit.m_committer_name = fields[6];
        commit.m_committer_date = fields[7];
        commit.m_refs = split_non_empty(fields[8], ", ");
        commit.m_subject = fields[9];

        // The body may itself contain the separator, so take everything remaining.
        std::string body;
        for (std::size_t i = 10; i < fields.size(); ++i) {
            if (i > 10) {
                body.push_back(s_field_separator);
            }
            body += fields[i];
        }
        commit.m_body = body;

        commits.push_back(commit);
    }

    return commits;
}
