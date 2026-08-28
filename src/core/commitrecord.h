/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <utility>
#include <vector>

/*! One commit, as read from `git log`. */
class CommitRecord
{
public:
    std::string m_object_name;
    std::string m_abbreviated_name;
    /*! Parent commits. More than one means a merge; none means a root. */
    std::vector<std::string> m_parents;

    std::string m_author_name;
    std::string m_author_email;
    std::string m_author_date;
    std::string m_committer_name;
    std::string m_committer_date;

    std::string m_subject;
    std::string m_body;

    /*! Ref names pointing here, e.g. "HEAD -> refs/heads/main, refs/tags/v1". */
    std::vector<std::string> m_refs;

    bool is_merge() const
    {
        return m_parents.size() > 1;
    }

    bool is_root() const
    {
        return m_parents.empty();
    }
};

/*! Where a commit sits in the drawn graph. */
class GraphRow
{
public:
    /*! The lane holding this commit's dot. */
    int m_lane = 0;
    /*! How many lanes are occupied on this row, so the column can be sized. */
    int m_lane_count = 1;
    /*! Stable colour index, held for as long as the lane lives. */
    int m_color_index = 0;

    /*! Lanes passing straight through this row without touching the commit. */
    std::vector<std::pair<int, int>> m_pass_through;
    /*! Lanes joining into m_lane from above, as (fromLane, colorIndex). */
    std::vector<std::pair<int, int>> m_edges_in;
    /*! Lanes branching out of m_lane below, as (toLane, colorIndex). */
    std::vector<std::pair<int, int>> m_edges_out;

    bool m_is_merge = false;
    bool m_is_root = false;
};
