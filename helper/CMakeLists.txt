# norn-editor, the program git runs as GIT_EDITOR and GIT_SEQUENCE_EDITOR.
#
# Installed to libexec rather than bin: it is never meant to be run by hand, and it
# links only GLib and GIO so that git, which starts it synchronously and waits, is
# not held up loading a GUI stack.
#
# SPDX-License-Identifier: GPL-2.0-or-later

pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

add_executable(norn-editor main.cc)
target_link_libraries(norn-editor PRIVATE PkgConfig::GIO)

install(TARGETS norn-editor RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
