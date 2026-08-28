# Norn - a graphical Git client built on GTK 3 via gtkmm, the toolkit Thunar
# uses, so it sits alongside the rest of an Xfce desktop rather than dragging in a
# second widget stack.
#
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.20)

project(norn VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wsuggest-override -Wnon-virtual-dtor)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

# gtkmm 3 pulls in glibmm, giomm, pangomm, cairomm and sigc++ as dependencies.
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-3.0)

# GtkSourceView has no C++ binding, so the editor uses its C API directly. This is
# the same version Mousepad, Xfce's own editor, is built against.
pkg_check_modules(GTKSOURCEVIEW REQUIRED IMPORTED_TARGET gtksourceview-4)

# giomm 2.4 does not wrap the Unix stream types, so the pipes from a spawned
# process are wrapped from their C constructors instead.
pkg_check_modules(GIOUNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)

add_subdirectory(data)
add_subdirectory(helper)
add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

message(STATUS "Norn ${PROJECT_VERSION}: gtkmm ${GTKMM_VERSION}, gtksourceview ${GTKSOURCEVIEW_VERSION}")
