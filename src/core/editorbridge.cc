/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "editorbridge.h"

#include "gitcommand.h"

#include <giomm/datainputstream.h>
#include <giomm/unixsocketaddress.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <glib/gstdio.h>

namespace
{
/*! Where the helper is installed, baked in at build time. */
std::string installed_helper_path()
{
#ifdef NORN_LIBEXEC_DIR
    return Glib::build_filename(NORN_LIBEXEC_DIR, "norn-editor");
#else
    return {};
#endif
}

std::string random_hex(int bytes)
{
    std::string value;
    for (int i = 0; i < bytes; ++i) {
        char buffer[3];
        std::snprintf(buffer, sizeof(buffer), "%02x", g_random_int_range(0, 256));
        value += buffer;
    }
    return value;
}

/*! Extracts a JSON string field without pulling in a parser for four keys. */
std::string json_string(const std::string &document, const std::string &key)
{
    const std::string needle = "\"" + key + "\":\"";
    const std::size_t start = document.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t from = start + needle.size();
    const std::size_t end = document.find('"', from);
    return end == std::string::npos ? std::string() : document.substr(from, end - from);
}
}

EditorBridge::EditorBridge()
    : m_service(Gio::SocketService::create())
{
}

EditorBridge::~EditorBridge()
{
    if (m_service) {
        m_service->stop();
    }
    if (!m_socket_path.empty()) {
        g_unlink(m_socket_path.c_str());
    }
}

std::string EditorBridge::helper_path()
{
    const std::string installed = installed_helper_path();
    if (!installed.empty() && Glib::file_test(installed, Glib::FILE_TEST_IS_EXECUTABLE)) {
        return installed;
    }

    // An uninstalled build keeps the helper in the build tree beside the binary.
    const std::string beside = Glib::build_filename(Glib::path_get_dirname(Glib::find_program_in_path("norn")), "norn-editor");
    return Glib::file_test(beside, Glib::FILE_TEST_IS_EXECUTABLE) ? beside : std::string();
}

bool EditorBridge::start()
{
    if (m_listening) {
        return true;
    }

    // Under the runtime directory, which is already private to the user, and with
    // a filesystem path rather than an abstract socket: an abstract socket has no
    // permissions at all and any local process could connect to it.
    const std::string directory = Glib::get_user_runtime_dir();
    m_socket_path = Glib::build_filename(directory, "norn-" + std::to_string(getpid()) + "-" + random_hex(4));

    g_unlink(m_socket_path.c_str());

    try {
        const Glib::RefPtr<Gio::SocketAddress> address = Gio::UnixSocketAddress::create(m_socket_path, Gio::UNIX_SOCKET_ADDRESS_PATH);
        Glib::RefPtr<Gio::SocketAddress> effective;
        m_service->add_address(address, Gio::SOCKET_TYPE_STREAM, Gio::SOCKET_PROTOCOL_DEFAULT, effective);
    } catch (const Glib::Error &) {
        m_socket_path.clear();
        return false;
    }

    // Owner-only, so nothing else on the machine can drive the editor channel.
    g_chmod(m_socket_path.c_str(), 0600);

    m_service->signal_incoming().connect(sigc::mem_fun(*this, &EditorBridge::on_incoming));
    m_service->start();

    m_listening = true;
    return true;
}

std::string EditorBridge::rotate_token()
{
    m_token = random_hex(16);
    return m_token;
}

void EditorBridge::apply_to(GitCommand &command)
{
    const std::string helper = helper_path();
    if (!m_listening || helper.empty()) {
        return;
    }

    command.m_needs_editor = true;
    command.m_editor_path = helper;
    command.m_editor_socket = m_socket_path;
    // Rotated per command, so a token that leaked from an earlier invocation
    // cannot be replayed against this one.
    command.m_editor_token = rotate_token();
}

bool EditorBridge::on_incoming(const Glib::RefPtr<Gio::SocketConnection> &connection, const Glib::RefPtr<Glib::Object> &)
{
    const Glib::RefPtr<Gio::DataInputStream> stream = Gio::DataInputStream::create(connection->get_input_stream());

    std::string line;
    try {
        stream->read_line(line);
    } catch (const Glib::Error &) {
        return true;
    }

    if (json_string(line, "token") != m_token || m_token.empty()) {
        // Not from the command we started; say nothing useful and drop it.
        try {
            connection->get_output_stream()->write("{\"v\":1,\"status\":\"abort\"}\n");
            connection->close();
        } catch (const Glib::Error &) {
        }
        return true;
    }

    m_pending = connection;

    const std::string file = json_string(line, "file");
    if (json_string(line, "role") == "sequence") {
        m_signal_sequence_requested.emit(file);
    } else {
        m_signal_message_requested.emit(file);
    }

    return true;
}

void EditorBridge::reply(const std::string &status)
{
    if (!m_pending) {
        return;
    }

    try {
        m_pending->get_output_stream()->write("{\"v\":1,\"status\":\"" + status + "\"}\n");
        m_pending->close();
    } catch (const Glib::Error &) {
        // The helper went away; git will see a non-zero exit and abort, which is
        // the safe outcome.
    }

    m_pending.reset();
}

void EditorBridge::accept()
{
    reply("ok");
}

void EditorBridge::reject()
{
    reply("abort");
}
