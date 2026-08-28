/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pathspec.h"

std::string Pathspec::encode(const std::vector<std::string> &paths)
{
    std::string encoded;
    for (const std::string &path : paths) {
        if (path.empty()) {
            continue;
        }
        encoded += ":(literal)";
        encoded += path;
        encoded.push_back('\0');
    }
    return encoded;
}

std::string Pathspec::literal(const std::string &path)
{
    return ":(literal)" + path;
}
