/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <glibmm/ustring.h>

#include <string>
#include <vector>

/*!
 * Builds the fixed argument prelude and process environment shared by every git
 * invocation.
 *
 * Both are correctness-critical rather than cosmetic: the prelude neutralises the
 * user configuration that would otherwise corrupt machine-readable output, and the
 * environment must be derived from the real one so credential helpers keep working.
 */
namespace GitEnvironment
{
/*!
 * The `-c key=value` options prepended to every command.
 *
 * @p disable_auto_gc adds `gc.auto=0`, which belongs on mutating commands only.
 *
 * Deliberately absent: core.autocrlf, commit.gpgsign, rebase.autostash,
 * merge.conflictStyle and status.renames. Those change semantics the user chose,
 * and silently overriding them would make norn disagree with their terminal.
 */
std::vector<std::string> config_prelude(bool disable_auto_gc);

/*!
 * The environment for a git child process, as NAME=VALUE entries derived from the
 * current one.
 *
 * Never constructed from scratch: the user's credential helper may be a shell
 * alias that needs PATH, HOME, XDG_* and SSH_AUTH_SOCK intact.
 */
std::vector<std::string> process_environment();

/*!
 * Adds the editor-bridge entries to @p env, pointing GIT_EDITOR and
 * GIT_SEQUENCE_EDITOR at the norn-editor helper.
 *
 * The previously effective editor is preserved in NORN_FALLBACK_EDITOR so the
 * helper has somewhere to turn if it cannot reach the application at all.
 */
void apply_editor_bridge(std::vector<std::string> &env, const std::string &helper_path, const std::string &socket_path, const std::string &token);

/*!
 * Strips userinfo from a URL so embedded tokens never reach the command log, a
 * dialog or a log file. `https://user:token@host/x` becomes `https://host/x`.
 */
Glib::ustring redact_url(const Glib::ustring &url);

/*! Applies redact_url() to anything in @p text that looks like a URL with userinfo. */
Glib::ustring redact_text(const Glib::ustring &text);
}
