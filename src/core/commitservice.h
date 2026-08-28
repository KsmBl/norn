/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <string>

class Repository;

/*! How a commit should be made. */
class CommitOptions
{
public:
    std::string m_message;
    /*! Replace HEAD rather than adding a commit on top of it. */
    bool m_amend = false;
    /*! Append a Signed-off-by trailer. */
    bool m_sign_off = false;
    /*! Skip pre-commit and commit-msg hooks. Always an explicit user choice. */
    bool m_no_verify = false;
    /*! Take the author identity from the current user rather than the amended commit. */
    bool m_reset_author = false;
    /*! Allow a commit that changes nothing, which `git commit` otherwise refuses. */
    bool m_allow_empty = false;
};

/*!
 * Creates and amends commits.
 *
 * The message is always passed on stdin with `-F -`, never through an editor, so
 * no GIT_EDITOR round trip is involved and a message containing anything at all
 * survives intact.
 */
class CommitService
{
public:
    explicit CommitService(Repository &repository);

    void commit(const CommitOptions &options);

    /*! Asks for HEAD's message, so the amend editor can be pre-filled with it. */
    void request_head_message();

    sigc::signal<void()> &signal_committed()
    {
        return m_signal_committed;
    }
    /*! Hooks wrote output and the commit failed; it is shown verbatim. */
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }
    sigc::signal<void(const Glib::ustring &)> &signal_head_message_ready()
    {
        return m_signal_head_message_ready;
    }

private:
    Repository &m_repository;

    sigc::signal<void()> m_signal_committed;
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
    sigc::signal<void(const Glib::ustring &)> m_signal_head_message_ready;
};
