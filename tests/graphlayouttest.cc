/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/graphlayout.h"

#include <gtest/gtest.h>

namespace
{
/*! A commit with the given id and parents; the rest is irrelevant to layout. */
CommitRecord commit(const std::string &id, const std::vector<std::string> &parents = {})
{
    CommitRecord record;
    record.m_object_name = id;
    record.m_parents = parents;
    return record;
}

bool contains_lane(const std::vector<std::pair<int, int>> &entries, int lane)
{
    for (const auto &entry : entries) {
        if (entry.first == lane) {
            return true;
        }
    }
    return false;
}
}

TEST(GraphLayout, LinearHistoryStaysInOneLane)
{
    GraphLayout layout;

    const GraphRow c = layout.place(commit("C", {"B"}));
    const GraphRow b = layout.place(commit("B", {"A"}));
    const GraphRow a = layout.place(commit("A"));

    EXPECT_EQ(c.m_lane, 0);
    EXPECT_EQ(b.m_lane, 0);
    EXPECT_EQ(a.m_lane, 0);
    EXPECT_EQ(c.m_lane_count, 1);
    // A straight history should never draw a second lane's worth of width.
    EXPECT_TRUE(c.m_pass_through.empty());
    EXPECT_TRUE(b.m_pass_through.empty());
}

TEST(GraphLayout, RootEndsItsLane)
{
    GraphLayout layout;

    layout.place(commit("B", {"A"}));
    const GraphRow a = layout.place(commit("A"));

    EXPECT_TRUE(a.m_is_root);
    EXPECT_FALSE(a.m_is_merge);
}

TEST(GraphLayout, BranchOpensASecondLane)
{
    GraphLayout layout;

    // Two tips over a shared parent: X and Y both descend from B.
    const GraphRow x = layout.place(commit("X", {"B"}));
    const GraphRow y = layout.place(commit("Y", {"B"}));

    EXPECT_EQ(x.m_lane, 0);
    // Y is a tip nothing refers to yet, so it needs a lane of its own.
    EXPECT_EQ(y.m_lane, 1);
    // While Y is drawn, X's lane is still waiting for B and must be drawn through.
    EXPECT_TRUE(contains_lane(y.m_pass_through, 0));

    // B closes both: the two lines converge here.
    const GraphRow b = layout.place(commit("B", {"A"}));
    EXPECT_EQ(b.m_lane, 0);
    ASSERT_EQ(b.m_edges_in.size(), 1u);
    EXPECT_EQ(b.m_edges_in.front().first, 1);
}

TEST(GraphLayout, MergeBringsLanesBackTogether)
{
    GraphLayout layout;

    const GraphRow m = layout.place(commit("M", {"P1", "P2"}));

    EXPECT_EQ(m.m_lane, 0);
    EXPECT_TRUE(m.m_is_merge);
    // The second parent branches away into a lane of its own.
    ASSERT_EQ(m.m_edges_out.size(), 1u);
    EXPECT_EQ(m.m_edges_out.front().first, 1);

    // The first parent continues straight down, which is what keeps a mainline
    // visually straight rather than zig-zagging at every merge.
    const GraphRow p1 = layout.place(commit("P1", {"BASE"}));
    EXPECT_EQ(p1.m_lane, 0);

    const GraphRow p2 = layout.place(commit("P2", {"BASE"}));
    EXPECT_EQ(p2.m_lane, 1);

    const GraphRow base = layout.place(commit("BASE"));
    EXPECT_EQ(base.m_lane, 0);
    EXPECT_EQ(base.m_edges_in.size(), 1u);
}

TEST(GraphLayout, OctopusMergeOpensALanePerExtraParent)
{
    GraphLayout layout;

    const GraphRow ordinary = layout.place(commit("A", {"B"}));
    EXPECT_FALSE(ordinary.m_is_merge);

    const GraphRow octopus = layout.place(commit("B", {"C", "D", "E"}));
    EXPECT_TRUE(octopus.m_is_merge);
    EXPECT_EQ(octopus.m_edges_out.size(), 2u);
}

TEST(GraphLayout, LaneIsReusedAfterItCloses)
{
    GraphLayout layout;

    layout.place(commit("X", {"B"}));
    layout.place(commit("Y", {"B"}));
    // B closes lane 1, which should then be free again.
    layout.place(commit("B", {"A"}));

    // A new unrelated tip takes the freed lane rather than widening the graph.
    const GraphRow z = layout.place(commit("Z", {"A"}));
    EXPECT_EQ(z.m_lane, 1);
}

TEST(GraphLayout, ResetClearsState)
{
    GraphLayout layout;

    layout.place(commit("X", {"B"}));
    layout.place(commit("Y", {"B"}));
    layout.reset();

    // After a reset the next commit is a tip again, in lane zero.
    const GraphRow fresh = layout.place(commit("Q", {"R"}));
    EXPECT_EQ(fresh.m_lane, 0);
    EXPECT_EQ(fresh.m_lane_count, 1);
}
