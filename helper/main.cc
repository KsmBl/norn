/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*!
 * @file
 * norn-editor: the program norn hands to git as GIT_EDITOR and
 * GIT_SEQUENCE_EDITOR.
 *
 * git invokes an editor for things norn cannot express any other way — most
 * importantly rewording a commit during a rebase. Rather than opening a terminal
 * editor, this helper connects back to the running application over a local
 * socket, hands it the file git wants edited, and blocks until the application
 * says the file has been written or the operation was abandoned. Its exit code is
 * what git reads: zero to proceed, non-zero to abort.
 *
 * Deliberately tiny and linked against GLib and GIO only, because git starts it
 * synchronously and waits.
 */

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
/*! Long enough to cover a busy application, short enough not to hang a rebase. */
constexpr int s_connect_timeout_seconds = 5;

/*! Exit codes git interprets: zero proceeds, anything else aborts the step. */
constexpr int s_proceed = 0;
constexpr int s_abort = 1;

void warn(const std::string &message)
{
    std::fprintf(stderr, "norn-editor: %s\n", message.c_str());
}

/*!
 * Falls back to whatever editor the user had configured before norn replaced it.
 *
 * Never falls back to a default such as vi: this process has no terminal, so an
 * interactive editor would block forever with nothing on screen to explain why.
 */
int run_fallback_editor(const std::string &file)
{
    const char *fallback = g_getenv("NORN_FALLBACK_EDITOR");
    if (fallback == nullptr || *fallback == '\0') {
        warn("cannot reach norn and no fallback editor is configured");
        return s_abort;
    }

    // Interpreted by a shell, the same way git treats GIT_EDITOR, so a value with
    // arguments works.
    const std::string command = std::string(fallback) + " \"$@\"";
    const char *argv[] = {"/bin/sh", "-c", command.c_str(), "sh", file.c_str(), nullptr};

    gint status = 0;
    GError *error = nullptr;

    if (!g_spawn_sync(nullptr, const_cast<gchar **>(argv), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr, &status, &error)) {
        warn(error != nullptr ? error->message : "could not run the fallback editor");
        if (error != nullptr) {
            g_error_free(error);
        }
        return s_abort;
    }

    return g_spawn_check_wait_status(status, nullptr) ? s_proceed : s_abort;
}
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        warn("usage: norn-editor --message|--sequence <file>");
        return s_abort;
    }

    const std::string role = std::strcmp(argv[1], "--sequence") == 0 ? "sequence" : "message";

    gchar *absolute = g_canonicalize_filename(argv[2], nullptr);
    const std::string file = absolute != nullptr ? absolute : argv[2];
    g_free(absolute);

    const char *socket_path = g_getenv("NORN_SOCKET");
    if (socket_path == nullptr || *socket_path == '\0') {
        return run_fallback_editor(file);
    }

    GSocketClient *client = g_socket_client_new();
    g_socket_client_set_timeout(client, s_connect_timeout_seconds);

    GSocketAddress *address = g_unix_socket_address_new(socket_path);
    GError *error = nullptr;
    GSocketConnection *connection = g_socket_client_connect(client, G_SOCKET_CONNECTABLE(address), nullptr, &error);

    g_object_unref(address);

    if (connection == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        g_object_unref(client);
        return run_fallback_editor(file);
    }

    // Newline-delimited JSON: no length framing to get wrong, and trivially
    // debuggable with socat.
    GString *request = g_string_new(nullptr);
    g_string_append_printf(request,
                           "{\"v\":1,\"token\":\"%s\",\"role\":\"%s\",\"file\":\"%s\"}\n",
                           g_getenv("NORN_TOKEN") != nullptr ? g_getenv("NORN_TOKEN") : "",
                           role.c_str(),
                           file.c_str());

    GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    const gboolean written = g_output_stream_write_all(out, request->str, request->len, nullptr, nullptr, &error);
    g_string_free(request, TRUE);

    if (!written) {
        warn("could not send the request to norn");
        if (error != nullptr) {
            g_error_free(error);
        }
        g_object_unref(connection);
        g_object_unref(client);
        return s_abort;
    }

    // No timeout on the reply: the user may take as long as they like over the
    // dialog. The application closing the socket is what ends the wait.
    GDataInputStream *in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(connection)));
    gsize length = 0;
    gchar *reply = g_data_input_stream_read_line(in, &length, nullptr, &error);

    int result = s_abort;
    if (reply != nullptr) {
        // A crude check rather than a JSON parse: the reply has exactly two shapes.
        result = std::strstr(reply, "\"ok\"") != nullptr ? s_proceed : s_abort;
        g_free(reply);
    } else {
        // The application went away without answering. Aborting is the safe reading:
        // git then leaves its state on disk, and norn offers to continue or abort
        // next time it opens the repository.
        warn("norn closed without answering");
    }

    if (error != nullptr) {
        g_error_free(error);
    }

    g_object_unref(in);
    g_object_unref(connection);
    g_object_unref(client);

    return result;
}
