/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <giomm/socketconnection.h>
#include <giomm/socketservice.h>
#include <glibmm/refptr.h>
#include <sigc++/signal.h>

#include <string>

class GitCommand;

/*!
 * The application side of the norn-editor helper.
 *
 * Listens on a local socket. When git runs the helper as its editor, the helper
 * connects here, names the file git wants edited, and waits. The application shows
 * whatever UI that file calls for, writes it, and answers; the helper's exit code
 * then tells git whether to proceed.
 *
 * Almost every editor invocation is avoided rather than handled this way — commits
 * pass their message with `-F -`, merges and reverts use `--no-edit`, continuing an
 * operation runs with core.editor=true, and a rebase plan the application generated
 * is delivered with a copying sequence editor. What is left is rewording during a
 * rebase, which has no other route.
 */
class EditorBridge
{
public:
    EditorBridge();
    ~EditorBridge();

    EditorBridge(const EditorBridge &) = delete;
    EditorBridge &operator=(const EditorBridge &) = delete;

    /*! Starts listening. Returns false if the socket could not be created. */
    bool start();

    bool listening() const
    {
        return m_listening;
    }

    /*! Puts the editor environment on @p command, if the bridge is available. */
    void apply_to(GitCommand &command);

    /*! Path to the installed helper binary, or empty if it cannot be found. */
    static std::string helper_path();

    /*! git wants a commit message edited. The file already holds git's draft. */
    sigc::signal<void(const std::string &)> &signal_message_requested()
    {
        return m_signal_message_requested;
    }
    /*! git wants its generated rebase todo edited. */
    sigc::signal<void(const std::string &)> &signal_sequence_requested()
    {
        return m_signal_sequence_requested;
    }

    /*! Answers the waiting helper: the file has been written, carry on. */
    void accept();
    /*! Answers the waiting helper: abandon this step. */
    void reject();

private:
    bool on_incoming(const Glib::RefPtr<Gio::SocketConnection> &connection, const Glib::RefPtr<Glib::Object> &source);
    void reply(const std::string &status);
    std::string rotate_token();

    Glib::RefPtr<Gio::SocketService> m_service;
    std::string m_socket_path;
    std::string m_token;
    bool m_listening = false;

    /*! The helper currently waiting for an answer, if any. */
    Glib::RefPtr<Gio::SocketConnection> m_pending;

    sigc::signal<void(const std::string &)> m_signal_message_requested;
    sigc::signal<void(const std::string &)> m_signal_sequence_requested;
};
