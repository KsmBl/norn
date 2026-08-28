/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "commitpanel.h"
#include "core/commitservice.h"
#include "core/conflictservice.h"
#include "core/diffservice.h"
#include "core/indexservice.h"
#include "core/operationstate.h"
#include "core/remoteservice.h"
#include "core/repository.h"
#include "core/repositorywatcher.h"
#include "core/editorbridge.h"
#include "core/rebaseservice.h"
#include "core/refservice.h"
#include "diffview.h"
#include "historyview.h"
#include "editorview.h"
#include "messageeditordialog.h"
#include "rebasetododialog.h"
#include "settingsdialog.h"
#include "repositorysidebar.h"
#include "workingcopyview.h"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/entry.h>
#include <gtkmm/infobar.h>
#include <gtkmm/label.h>
#include <gtkmm/menubar.h>
#include <gtkmm/notebook.h>
#include <gtkmm/paned.h>
#include <gtkmm/separator.h>
#include <gtkmm/button.h>
#include <gtkmm/toolbar.h>
#include <gtkmm/toolitem.h>

#include <memory>
#include <vector>
#include <string>

/*!
 * The norn main window.
 *
 * Laid out the way a file manager is rather than the way a typical toolkit demo
 * is: menubar, a flat icon-only toolbar, a location bar naming the repository and
 * the checked-out branch, a side pane, and a status line. Fixed panes throughout —
 * nothing floats and no panel carries its own title bar.
 */
class MainWindow : public Gtk::ApplicationWindow
{
public:
    /*!
     * Creates a window for the working tree rooted at @p repository_path.
     *
     * An empty path opens the window in its no-repository state.
     */
    explicit MainWindow(const std::string &repository_path);
    ~MainWindow() override;

    /*! Shows an inline, non-modal message across the top of the window. */
    void show_message(const Glib::ustring &text, bool is_error = false);

private:
    void build_services();
    void build_ui();
    void build_menu();
    void build_toolbar();
    void update_summary();
    void update_actions();
    void show_no_repository_offer();

    void push();
    void amend_push();
    void on_force_push_preview_ready(const ForcePushPreview &preview);
    /*! Shows the plan editor, then starts the rebase if it is accepted. */
    void on_rebase_plan_ready(const std::string &upstream, const std::vector<RebaseStep> &steps);
    /*! Answers git's request for an edited commit message. */
    void on_editor_message_requested(const std::string &file);
    /*! Answers git's request for an edited rebase todo. */
    void on_editor_sequence_requested(const std::string &file);

    void open_repository();
    void init_repository();
    void show_settings();
    /*! Opens @p path in an editor tab, or focuses the tab already showing it. */
    void open_in_editor(const std::string &path);

    /*! Editor tabs, so they can be found again and closed. */
    std::vector<std::unique_ptr<EditorView>> m_editors;

    std::string m_repository_path;

    std::unique_ptr<Repository> m_repository;
    std::unique_ptr<RepositoryWatcher> m_watcher;
    std::unique_ptr<OperationState> m_operation_state;
    std::unique_ptr<IndexService> m_index_service;
    std::unique_ptr<CommitService> m_commit_service;
    std::unique_ptr<DiffService> m_diff_service;
    std::unique_ptr<ConflictService> m_conflict_service;
    std::unique_ptr<RemoteService> m_remote_service;
    std::unique_ptr<RefService> m_ref_service;
    std::unique_ptr<RebaseService> m_rebase_service;
    std::unique_ptr<EditorBridge> m_editor_bridge;

    Gtk::Box m_root{Gtk::ORIENTATION_VERTICAL};
    Gtk::MenuBar m_menu_bar;
    Gtk::Toolbar m_toolbar;

    /*! A repository's equivalent of a path is where it is and which branch it is on. */
    Gtk::Box m_location_bar{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Entry m_location;
    Gtk::Label m_branch;

    Gtk::InfoBar m_message;
    Gtk::Label m_message_label;

    Gtk::InfoBar m_operation_banner;
    Gtk::Label m_operation_label;
    /*!
     * Kept because gtkmm 3's InfoBar cannot look a button up by response id, and
     * both of these change state: Continue is unavailable while conflicts remain,
     * and Skip only exists for operations that have a step to skip.
     */
    Gtk::Button *m_continue_button = nullptr;
    Gtk::Button *m_skip_button = nullptr;

    Gtk::Paned m_pane_splitter{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Box m_side_pane{Gtk::ORIENTATION_VERTICAL};
    Gtk::Notebook m_tabs;

    Gtk::Box m_working_copy_page{Gtk::ORIENTATION_VERTICAL};
    Gtk::Paned m_files_and_diff{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Paned m_commit_splitter{Gtk::ORIENTATION_VERTICAL};

    std::unique_ptr<WorkingCopyView> m_working_copy_view;
    std::unique_ptr<DiffView> m_diff_view;
    std::unique_ptr<CommitPanel> m_commit_panel;
    std::unique_ptr<RepositorySidebar> m_sidebar;
    std::unique_ptr<HistoryView> m_history_view;

    Gtk::Box m_status_bar{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Label m_progress;
    Gtk::Label m_summary;

    Gtk::Button *m_stage_button = nullptr;
    Gtk::Button *m_unstage_button = nullptr;
    Gtk::Button *m_discard_button = nullptr;
    Gtk::Button *m_push_button = nullptr;
    Gtk::Button *m_amend_push_button = nullptr;

    /*! Set while an amend push is being prepared, so the preview knows it was asked for. */
    bool m_awaiting_force_push_preview = false;

    /*!
     * Cleared once the starting pane positions have been applied.
     *
     * They cannot be set during construction: Gtk::Paned clamps a position against
     * the current allocation, which is still nothing at that point, so the value is
     * silently reduced to the child's minimum and never recovers.
     */
    bool m_panes_positioned = false;
    sigc::connection m_pane_position_connection;
};
