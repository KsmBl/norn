/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gitenvironment.h"

#include <giomm/file.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>

namespace
{
/*! Routes credential and host-key prompts to a graphical dialog when present. */
const char *const s_askpass_candidates[] = {
    "/usr/bin/ssh-askpass",
    "/usr/lib/seahorse/ssh-askpass",
    "/usr/bin/ksshaskpass",
    "/usr/bin/lxqt-openssh-askpass",
};

/*! Replaces or appends NAME=VALUE in an environment vector. */
void env_set(std::vector<std::string> &env, const std::string &name, const std::string &value)
{
    const std::string prefix = name + "=";
    for (std::string &entry : env) {
        if (entry.compare(0, prefix.size(), prefix) == 0) {
            entry = prefix + value;
            return;
        }
    }
    env.push_back(prefix + value);
}

void env_unset(std::vector<std::string> &env, const std::string &name)
{
    const std::string prefix = name + "=";
    std::erase_if(env, [&prefix](const std::string &entry) {
        return entry.compare(0, prefix.size(), prefix) == 0;
    });
}

std::string env_get(const std::vector<std::string> &env, const std::string &name)
{
    const std::string prefix = name + "=";
    for (const std::string &entry : env) {
        if (entry.compare(0, prefix.size(), prefix) == 0) {
            return entry.substr(prefix.size());
        }
    }
    return {};
}

/*! Quotes a path for the shell git runs its editor through. */
std::string shell_quote(const std::string &path)
{
    std::string quoted = "'";
    for (const char c : path) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}
}

std::vector<std::string> GitEnvironment::config_prelude(bool disable_auto_gc)
{
    std::vector<std::string> prelude{
        // Colour codes would have to be stripped back out of every parse.
        "-c", "color.ui=false",
        // Without this, paths outside ASCII come back C-quoted and double-escaped.
        "-c", "core.quotepath=false",
        // A user with signature verification enabled otherwise gets GPG output
        // interleaved into git log, which destroys any --format parse.
        "-c", "log.showSignature=false",
        // A configured external differ would replace diff output wholesale.
        "-c", "diff.external=",
        // The patch builder depends on the a/ and b/ prefixes being present and plain.
        "-c", "diff.noprefix=false",
        "-c", "diff.mnemonicPrefix=false",
        // Never hand output to a pager; there is no terminal to page into.
        "-c", "core.pager=cat",
        "-c", "i18n.logOutputEncoding=UTF-8",
        // Advice text is written for humans at a terminal and only adds noise here.
        "-c", "advice.detachedHead=false",
        "-c", "advice.statusHints=false",
    };

    if (disable_auto_gc) {
        // A surprise repack must never be what makes a commit appear to hang.
        prelude.emplace_back("-c");
        prelude.emplace_back("gc.auto=0");
    }

    return prelude;
}

std::vector<std::string> GitEnvironment::process_environment()
{
    // listenv() gives names only, so the values are fetched back individually.
    const std::vector<std::string> names = Glib::listenv();

    std::vector<std::string> entries;
    entries.reserve(names.size());
    for (const std::string &name : names) {
        bool found = false;
        const std::string value = Glib::getenv(name, found);
        if (found) {
            entries.push_back(name + "=" + value);
        }
    }

    // LC_ALL outranks LC_MESSAGES, so setting the latter alone achieves nothing.
    // LC_CTYPE and LANG are left alone: they govern how filename bytes are handled.
    env_unset(entries, "LC_ALL");
    env_set(entries, "LC_MESSAGES", "C");

    // There is no terminal to prompt on; a prompt attempt would hang forever.
    env_set(entries, "GIT_TERMINAL_PROMPT", "0");

    // Only step in when the user has expressed no preference of their own. An
    // already-configured GIT_ASKPASS, or core.askPass which git consults itself, is
    // a deliberate choice and must win.
    if (env_get(entries, "GIT_ASKPASS").empty()) {
        for (const char *candidate : s_askpass_candidates) {
            if (Gio::File::create_for_path(candidate)->query_exists()) {
                env_set(entries, "GIT_ASKPASS", candidate);
                if (env_get(entries, "SSH_ASKPASS").empty()) {
                    env_set(entries, "SSH_ASKPASS", candidate);
                    // Without this, ssh only uses the helper when it has no tty at
                    // all; forcing it also routes host-key confirmation to a dialog.
                    env_set(entries, "SSH_ASKPASS_REQUIRE", "force");
                }
                break;
            }
        }
    }

    return entries;
}

void GitEnvironment::apply_editor_bridge(std::vector<std::string> &env,
                                         const std::string &helper_path,
                                         const std::string &socket_path,
                                         const std::string &token)
{
    if (helper_path.empty() || socket_path.empty()) {
        return;
    }

    // Whatever would have been used otherwise, so the helper can fall back to it
    // rather than to an interactive editor that has no terminal to run in.
    std::string previous = env_get(env, "GIT_EDITOR");
    if (previous.empty()) {
        previous = env_get(env, "VISUAL");
    }
    if (previous.empty()) {
        previous = env_get(env, "EDITOR");
    }
    if (!previous.empty()) {
        env_set(env, "NORN_FALLBACK_EDITOR", previous);
    }

    // git runs these through a shell, so the path is quoted against spaces.
    const std::string quoted = shell_quote(helper_path);

    env_set(env, "GIT_EDITOR", quoted + " --message");
    env_set(env, "GIT_SEQUENCE_EDITOR", quoted + " --sequence");
    env_set(env, "NORN_SOCKET", socket_path);
    env_set(env, "NORN_TOKEN", token);
}

Glib::ustring GitEnvironment::redact_url(const Glib::ustring &url)
{
    // Matches scheme://userinfo@host, which is the only shape that can carry a token.
    static const Glib::RefPtr<Glib::Regex> with_userinfo = Glib::Regex::create("(\\w+://)[^/\\s@]+@");
    return with_userinfo->replace(url, 0, "\\1", static_cast<Glib::RegexMatchFlags>(0));
}

Glib::ustring GitEnvironment::redact_text(const Glib::ustring &text)
{
    return redact_url(text);
}
