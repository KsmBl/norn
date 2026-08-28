/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "rebasetodo.h"

namespace
{
std::string trimmed(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}
}

std::string RebaseStep::action_keyword() const
{
    switch (m_action) {
    case Action::Pick:
        return "pick";
    case Action::Reword:
        return "reword";
    case Action::Edit:
        return "edit";
    case Action::Squash:
        return "squash";
    case Action::Fixup:
        return "fixup";
    case Action::Drop:
        return "drop";
    case Action::Break:
        return "break";
    }
    return "pick";
}

RebaseStep::Action RebaseStep::action_from_keyword(const std::string &keyword)
{
    // git accepts one-letter abbreviations in the todo file, and writes them when
    // rebase.abbreviateCommands is set, so both spellings have to be understood.
    if (keyword == "reword" || keyword == "r") {
        return Action::Reword;
    }
    if (keyword == "edit" || keyword == "e") {
        return Action::Edit;
    }
    if (keyword == "squash" || keyword == "s") {
        return Action::Squash;
    }
    if (keyword == "fixup" || keyword == "f") {
        return Action::Fixup;
    }
    if (keyword == "drop" || keyword == "d") {
        return Action::Drop;
    }
    if (keyword == "break" || keyword == "b") {
        return Action::Break;
    }
    return Action::Pick;
}

std::string RebaseTodo::render(const std::vector<RebaseStep> &steps)
{
    std::string todo;

    for (const RebaseStep &step : steps) {
        if (step.m_action == RebaseStep::Action::Break) {
            todo += "break\n";
            continue;
        }

        // A dropped commit is written as an explicit `drop` rather than omitted, so
        // the file still records the decision and git's own checks can see it.
        todo += step.action_keyword();
        todo.push_back(' ');
        todo += step.m_commit;
        todo.push_back(' ');
        todo += step.m_subject;
        todo.push_back('\n');
    }

    return todo;
}

std::vector<RebaseStep> RebaseTodo::parse(const std::string &contents)
{
    std::vector<RebaseStep> steps;

    std::size_t start = 0;
    while (start <= contents.size()) {
        const std::size_t end = contents.find('\n', start);
        const std::string line = trimmed(contents.substr(start, end == std::string::npos ? std::string::npos : end - start));

        if (!line.empty() && line[0] != '#') {
            const std::size_t first_space = line.find(' ');
            const std::string keyword = first_space == std::string::npos ? line : line.substr(0, first_space);

            RebaseStep step;
            step.m_action = RebaseStep::action_from_keyword(keyword);

            if (step.m_action != RebaseStep::Action::Break && first_space != std::string::npos) {
                const std::size_t second_space = line.find(' ', first_space + 1);
                if (second_space != std::string::npos) {
                    step.m_commit = line.substr(first_space + 1, second_space - first_space - 1);
                    step.m_subject = line.substr(second_space + 1);
                } else {
                    step.m_commit = line.substr(first_space + 1);
                }
            }

            steps.push_back(step);
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return steps;
}
