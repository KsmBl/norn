/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <giomm/init.h>
#include <glibmm/init.h>

#include <gtest/gtest.h>

/*!
 * The tests need their own entry point rather than gtest_main.
 *
 * glibmm and giomm register the C++ wrapper types for their GObject classes in
 * init(), and Glib::wrap() cannot produce a wrapper for a type that was never
 * registered. The application gets this for free from Gtk::Application; a test
 * binary that never constructs one has to do it itself.
 */
int main(int argc, char **argv)
{
    Glib::init();
    Gio::init();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
