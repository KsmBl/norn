# The norn executable, plus the static library it shares with the test suite.
#
# Sources are grouped by layer: core/ talks to git and knows nothing about widgets,
# models/ adapts core data to GTK tree models, ui/ is widgets only. core/ is built
# separately so the parsers can be tested without opening a display.
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(norncore STATIC
    core/gitenvironment.cc
    core/gitjob.cc
    core/gitrunner.cc
    core/parsers/diffparser.cc
    core/graphlayout.cc
    core/parsers/logparser.cc
    core/parsers/refparser.cc
    core/parsers/statusparser.cc
    core/pathspec.cc
    core/patchbuilder.cc
    core/repository.cc
    core/repositorylocator.cc
    core/repositorywatcher.cc
    core/operationstate.cc
    core/indexservice.cc
    core/commitservice.cc
    core/diffservice.cc
    core/conflictservice.cc
    core/editorbridge.cc
    core/rebaseservice.cc
    core/rebasetodo.cc
    core/refservice.cc
    core/remoteservice.cc
    core/settings.cc
)

target_include_directories(norncore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# glibmm and giomm come through gtkmm; core/ uses only those, not the widgets.
target_link_libraries(norncore PUBLIC PkgConfig::GTKMM PkgConfig::GIOUNIX)

# Where the editor helper is installed, so the bridge can find it at runtime.
target_compile_definitions(norncore PUBLIC NORN_LIBEXEC_DIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}")
target_compile_definitions(norncore PUBLIC NORN_VERSION="${PROJECT_VERSION}")

add_executable(norn
    main.cc
    ui/diffview.cc
    ui/graphcellrenderer.cc
    ui/historyview.cc
    ui/editorview.cc
    ui/messageeditordialog.cc
    ui/settingsdialog.cc
    ui/rebasetododialog.cc
    ui/commitpanel.cc
    ui/mainwindow.cc
    ui/repositorysidebar.cc
    ui/statuscolumns.cc
    ui/workingcopyview.cc
)

target_link_libraries(norn PRIVATE
    norncore
    PkgConfig::GTKMM
    PkgConfig::GTKSOURCEVIEW
)

install(TARGETS norn RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
