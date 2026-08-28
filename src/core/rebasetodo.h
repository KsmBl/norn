/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

/*! One line of an interactive rebase todo list. */
class RebaseStep
{
public:
    enum class Action {
        /*! Keep the commit as it is. */
        Pick,
        /*! Keep it, but stop to change its message. */
        Reword,
        /*! Stop after applying it, so the working tree can be amended. */
        Edit,
        /*! Fold into the previous commit, combining the messages. */
        Squash,
        /*! Fold into the previous commit, discarding this message. */
        Fixup,
        /*! Leave the commit out entirely. */
        Drop,
        /*! Stop here without applying anything. */
        Break,
    };

    Action m_action = Action::Pick;
    std::string m_commit;
    std::string m_subject;

    /*! The keyword git writes in the todo file. */
    std::string action_keyword() const;
    static Action action_from_keyword(const std::string &keyword);
};

/*! An interactive rebase plan. */
namespace RebaseTodo
{
/*! Renders @p steps as the todo file git expects. */
std::string render(const std::vector<RebaseStep> &steps);

/*! Parses a todo file, ignoring comments and blank lines. */
std::vector<RebaseStep> parse(const std::string &contents);
}
