/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "graphlayout.h"

#include <algorithm>

namespace
{
/*! How many distinct lane colours to cycle through. */
constexpr int s_color_count = 8;
}

void GraphLayout::reset()
{
    m_lanes.clear();
    m_lane_colors.clear();
    m_next_color = 0;
}

int GraphLayout::allocate_lane(const std::string &expected_commit)
{
    for (std::size_t i = 0; i < m_lanes.size(); ++i) {
        if (m_lanes[i].empty()) {
            m_lanes[i] = expected_commit;
            m_lane_colors[i] = m_next_color;
            m_next_color = (m_next_color + 1) % s_color_count;
            return static_cast<int>(i);
        }
    }

    if (m_lanes.size() >= static_cast<std::size_t>(s_max_lanes)) {
        // Out of room. Everything further right collapses onto the last lane rather
        // than growing a column nobody can read.
        m_lanes[s_max_lanes - 1] = expected_commit;
        return s_max_lanes - 1;
    }

    m_lanes.push_back(expected_commit);
    m_lane_colors.push_back(m_next_color);
    m_next_color = (m_next_color + 1) % s_color_count;
    return static_cast<int>(m_lanes.size()) - 1;
}

GraphRow GraphLayout::place(const CommitRecord &commit)
{
    GraphRow row;

    // Lanes already waiting for this commit. More than one means several children
    // converge here, and their lines join into whichever lane is leftmost.
    std::vector<int> incoming;
    for (std::size_t i = 0; i < m_lanes.size(); ++i) {
        if (m_lanes[i] == commit.m_object_name) {
            incoming.push_back(static_cast<int>(i));
        }
    }

    // A tip: nothing below has referred to it yet.
    const int my_lane = incoming.empty() ? allocate_lane(commit.m_object_name) : incoming.front();

    row.m_lane = my_lane;
    row.m_color_index = m_lane_colors[static_cast<std::size_t>(my_lane)];
    row.m_is_merge = commit.is_merge();
    row.m_is_root = commit.is_root();

    // Joining edges are recorded before the lanes are freed, so the lines are still
    // drawn on the row where they converge.
    for (const int lane : incoming) {
        if (lane != my_lane) {
            row.m_edges_in.emplace_back(lane, m_lane_colors[static_cast<std::size_t>(lane)]);
            m_lanes[static_cast<std::size_t>(lane)].clear();
        }
    }

    // Lanes carrying on past this row, other than this commit's own.
    for (std::size_t i = 0; i < m_lanes.size(); ++i) {
        if (static_cast<int>(i) != my_lane && !m_lanes[i].empty()) {
            row.m_pass_through.emplace_back(static_cast<int>(i), m_lane_colors[i]);
        }
    }

    if (commit.m_parents.empty()) {
        // A root commit ends its lane.
        m_lanes[static_cast<std::size_t>(my_lane)].clear();
    } else {
        // The first parent continues straight down in the same lane, which is what
        // keeps a branch's mainline visually straight.
        m_lanes[static_cast<std::size_t>(my_lane)] = commit.m_parents.front();

        for (std::size_t i = 1; i < commit.m_parents.size(); ++i) {
            const std::string &parent = commit.m_parents[i];

            // If some lane already expects this parent, the merge line runs to it
            // rather than opening a duplicate lane for the same commit.
            const auto existing = std::find(m_lanes.begin(), m_lanes.end(), parent);
            const int target = existing != m_lanes.end() ? static_cast<int>(std::distance(m_lanes.begin(), existing)) : allocate_lane(parent);
            row.m_edges_out.emplace_back(target, m_lane_colors[static_cast<std::size_t>(target)]);
        }
    }

    // Trailing free lanes should not widen the column.
    int used = 0;
    for (std::size_t i = 0; i < m_lanes.size(); ++i) {
        if (!m_lanes[i].empty()) {
            used = static_cast<int>(i) + 1;
        }
    }
    row.m_lane_count = std::max({used, my_lane + 1, 1});

    return row;
}
