/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "commitrecord.h"

#include <string>
#include <vector>

/*!
 * Assigns commits to lanes so the history can be drawn as a graph.
 *
 * Incremental: commits are fed in topological order and each one is placed using
 * only the state left by the ones above it, so a page appended to the model
 * continues the drawing rather than forcing a recomputation. Topological order is
 * what makes this sound — it guarantees no commit arrives before all of its
 * children, so by the time a commit is placed, every lane expecting it exists.
 *
 * Runs on the main thread. Lane assignment is O(commits × lanes) with very small
 * constants; threading it would buy nothing and cost a synchronisation bug.
 */
class GraphLayout
{
public:
    /*! Places @p commit and returns where to draw it. */
    GraphRow place(const CommitRecord &commit);

    void reset();

    /*! Beyond this many lanes the graph is unreadable anyway, so it stops widening. */
    static constexpr int s_max_lanes = 24;

private:
    int allocate_lane(const std::string &expected_commit);

    /*! Lane index to the commit that lane is waiting for. Empty means free. */
    std::vector<std::string> m_lanes;
    /*!
     * Colour index per lane, assigned when the lane is allocated and held until it
     * is freed. Colouring by lane index instead would make colours jump around as
     * lanes are recycled.
     */
    std::vector<int> m_lane_colors;
    int m_next_color = 0;
};
