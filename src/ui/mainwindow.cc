/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "mainwindow.h"

#include "core/repositorylocator.h"
#include "core/settings.h"

#include <giomm/appinfo.h>
#include <giomm/file.h>
#include <glibmm/miscutils.h>
#include <glibmm/spawn.h>

#include <glib/gstdio.h>

#include <fstream>

#include <gtkmm/filechooserdialog.h>
#include <gtkmm/menu.h>
#include <gtkmm/checkmenuitem.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/button.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/separatormenuitem.h>
#include <gtkmm/separatortoolitem.h>

namespace
{
/*! Starting width of the side pane, in pixels. */
constexpr int s_side_pane_width = 230;
/*! Starting width of the file lists, leaving the rest to the diff. */
constexpr int s_file_list_width = 300;
/*! Starting height of the commit panel. */
constexpr int s_commit_panel_height = 210;
}

MainWindow::MainWindow(const std::string &repository_path)
    : m_repository_path(repository_path)
{
    set_default_size(1100, 720);
    set_icon_name("de.synthelicz.Norn");

    if (!m_repository_path.empty()) {
        m_repository = std::make_unique<Repository>(m_repository_path);
        build_services();
    }

    build_ui();

    // "<repository> — Norn", the shape a file manager titles its windows: what you
    // are looking at first, the application second.
    set_title(m_repository ? Glib::ustring::compose("%1 — Norn", m_repository->display_name()) : "Norn");

    if (m_repository) {
        m_repository->open();
    } else {
        show_no_repository_offer();
    }

    update_actions();
}

MainWindow::~MainWindow() = default;

void MainWindow::build_services()
{
    m_index_service = std::make_unique<IndexService>(*m_repository);
    m_commit_service = std::make_unique<CommitService>(*m_repository);
    m_diff_service = std::make_unique<DiffService>(*m_repository);
    m_operation_state = std::make_unique<OperationState>(*m_repository);
    m_conflict_service = std::make_unique<ConflictService>(*m_repository, *m_operation_state);
    m_remote_service = std::make_unique<RemoteService>(*m_repository);
    m_ref_service = std::make_unique<RefService>(*m_repository);
    m_editor_bridge = std::make_unique<EditorBridge>();
    if (!m_editor_bridge->start()) {
        // Not fatal: everything except rewording during a rebase supplies its text
        // another way, and the helper falls back to the user's own editor.
        show_message("Could not open the editor channel, so rewording during a rebase will not be available.");
    }
    m_rebase_service = std::make_unique<RebaseService>(*m_repository, *m_editor_bridge);
    m_watcher = std::make_unique<RepositoryWatcher>(*m_repository);

    const auto report = [this](const Glib::ustring &summary, const Glib::ustring &details) {
        show_message(details.empty() ? summary : summary + " " + details, true);
    };

    m_repository->signal_operation_failed().connect(report);
    m_index_service->signal_failed().connect(report);
    m_commit_service->signal_failed().connect(report);
    m_diff_service->signal_failed().connect(report);
    m_conflict_service->signal_failed().connect(report);
    m_remote_service->signal_failed().connect(report);
    m_ref_service->signal_failed().connect(report);
    m_rebase_service->signal_failed().connect(report);
    m_rebase_service->signal_plan_ready().connect(sigc::mem_fun(*this, &MainWindow::on_rebase_plan_ready));
    m_editor_bridge->signal_message_requested().connect(sigc::mem_fun(*this, &MainWindow::on_editor_message_requested));
    m_editor_bridge->signal_sequence_requested().connect(sigc::mem_fun(*this, &MainWindow::on_editor_sequence_requested));

    m_repository->signal_ready().connect([this] {
        m_watcher->start();
        m_operation_state->refresh();
        m_ref_service->refresh();
    });
    m_watcher->signal_changed().connect([this] {
        m_repository->refresh_status();
    });

    // A merge or rebase that stops on a conflict changes state without any command
    // reporting it as such, so the state is re-derived alongside every status read.
    m_repository->signal_status_changed().connect([this] {
        m_operation_state->refresh();
        // Branch state changes with almost every write, so refs follow the status.
        m_ref_service->refresh();
        update_summary();
        update_actions();
    });

    m_remote_service->signal_force_push_preview_ready().connect(sigc::mem_fun(*this, &MainWindow::on_force_push_preview_ready));
    m_remote_service->signal_remote_message().connect([this](const Glib::ustring &message) {
        show_message(message);
    });
    m_remote_service->signal_progress().connect([this](const Glib::ustring &phase, int current, int total) {
        m_progress.set_text(total > 0 ? Glib::ustring::compose("%1 %2/%3", phase, current, total) : Glib::ustring::compose("%1 %2", phase, current));
    });
    m_remote_service->signal_pushed().connect([this] {
        m_progress.set_text({});
        show_message("Push complete.");
    });
    m_remote_service->signal_fetched().connect([this] {
        m_progress.set_text({});
    });

    m_remote_service->signal_pulled().connect([this] {
        m_progress.set_text({});
        show_message("Pulled.");
    });

    m_operation_state->signal_changed().connect([this] {
        if (!m_operation_state->in_progress()) {
            m_operation_banner.hide();
            return;
        }
        m_operation_banner.show();
    });

    Settings::instance().signal_changed().connect([this] {
        if (m_commit_panel) {
            m_commit_panel->apply_settings();
        }
        if (m_watcher) {
            m_watcher->set_enabled(Settings::instance().auto_refresh());
        }
    });
}

void MainWindow::build_ui()
{
    build_menu();
    build_toolbar();

    m_root.pack_start(m_menu_bar, Gtk::PACK_SHRINK);
    m_root.pack_start(m_toolbar, Gtk::PACK_SHRINK);

    if (m_repository) {
        m_location.set_editable(false);
        m_location.set_can_focus(true);
        m_location.set_icon_from_icon_name("folder-symbolic", Gtk::ENTRY_ICON_PRIMARY);
        m_location.set_text(m_repository->toplevel());

        m_branch.set_margin_start(8);
        m_branch.set_margin_end(4);

        m_location_bar.set_margin_start(4);
        m_location_bar.set_margin_end(4);
        m_location_bar.set_margin_top(3);
        m_location_bar.set_margin_bottom(3);
        m_location_bar.pack_start(m_location, Gtk::PACK_EXPAND_WIDGET);
        m_location_bar.pack_start(m_branch, Gtk::PACK_SHRINK);
        m_root.pack_start(m_location_bar, Gtk::PACK_SHRINK);

        m_root.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)), Gtk::PACK_SHRINK);
    }

    dynamic_cast<Gtk::Container *>(m_message.get_content_area())->add(m_message_label);
    m_message_label.set_line_wrap(true);
    // Shown explicitly: set_no_show_all() on the bar below also suppresses
    // show_all() for its children, which would leave a bar with only buttons.
    m_message_label.show();
    m_message.set_show_close_button(true);
    m_message.set_no_show_all(true);
    m_message.signal_response().connect([this](int) {
        m_message.hide();
    });
    m_root.pack_start(m_message, Gtk::PACK_SHRINK);

    // The operation banner is the single most important safety feature: "what state
    // am I in, and how do I get out of it" is where a Git GUI most often strands
    // people, so it is pinned above everything and cannot be dismissed.
    dynamic_cast<Gtk::Container *>(m_operation_banner.get_content_area())->add(m_operation_label);
    m_operation_label.set_line_wrap(true);
    m_operation_label.show();
    m_operation_banner.set_show_close_button(false);
    m_operation_banner.set_message_type(Gtk::MESSAGE_WARNING);
    m_operation_banner.set_no_show_all(true);

    if (m_conflict_service) {
        m_continue_button = m_operation_banner.add_button("_Continue", 1);
        m_skip_button = m_operation_banner.add_button("_Skip", 2);
        m_operation_banner.add_button("_Abort", 3);
        m_operation_banner.signal_response().connect([this](int response) {
            if (response == 1) {
                m_conflict_service->continue_operation();
            } else if (response == 2) {
                m_conflict_service->skip_operation();
            } else if (response == 3) {
                m_conflict_service->abort_operation();
            }
        });
    }
    m_root.pack_start(m_operation_banner, Gtk::PACK_SHRINK);

    // Side pane against the main area, the way a file manager lays out Places and
    // its view.
    // A width request rather than set_position: Gtk::Paned clamps a position against
    // its current allocation, which during construction is still nothing, so the
    // position is silently reduced to the child's minimum and never recovers. The
    // request doubles as a floor on how narrow the divider can be dragged, which a
    // side pane wants anyway.
    m_side_pane.set_size_request(s_side_pane_width, -1);

    m_pane_splitter.pack1(m_side_pane, false, false);
    m_pane_splitter.pack2(m_tabs, true, false);

    m_tabs.set_scrollable(true);

    if (m_repository) {
        m_sidebar = std::make_unique<RepositorySidebar>(*m_repository, *m_ref_service);
        m_side_pane.pack_start(*m_sidebar, Gtk::PACK_EXPAND_WIDGET);

        m_sidebar->signal_open_repository_requested().connect([](const std::string &path) {
            // One window per repository, so a worktree or submodule gets its own.
            auto *window = new MainWindow(path);
            window->show();
        });

        m_working_copy_view = std::make_unique<WorkingCopyView>(*m_repository, *m_index_service, *m_conflict_service);
        m_diff_view = std::make_unique<DiffView>(*m_diff_service);
        m_commit_panel = std::make_unique<CommitPanel>(*m_repository, *m_commit_service);

        m_files_and_diff.pack1(*m_working_copy_view, false, false);
        m_files_and_diff.pack2(*m_diff_view, true, false);

        m_commit_splitter.pack1(m_files_and_diff, true, false);
        m_commit_splitter.pack2(*m_commit_panel, false, false);

        m_working_copy_page.pack_start(m_commit_splitter, Gtk::PACK_EXPAND_WIDGET);
        m_working_copy_page_num = m_tabs.append_page(m_working_copy_page, "Working Copy");

        m_working_copy_view->signal_current_file_changed().connect(
            [this](const std::string &path, DiffSide side, DiffMode mode, bool conflicted) {
                m_diff_view->show_file(path, side, mode, conflicted);
            });
        m_working_copy_view->signal_selection_changed().connect(sigc::mem_fun(*this, &MainWindow::update_actions));
        m_working_copy_view->signal_edit_requested().connect(sigc::mem_fun(*this, &MainWindow::open_in_editor));
        m_working_copy_view->signal_open_externally_requested().connect([this](const std::string &path) {
            // Through GIO rather than by guessing a command, so the file opens in
            // whatever the desktop is configured to use for it.
            const std::string absolute = Glib::build_filename(m_repository->toplevel(), path);
            try {
                Gio::AppInfo::launch_default_for_uri(Gio::File::create_for_path(absolute)->get_uri());
            } catch (const Glib::Error &error) {
                show_message(error.what(), true);
            }
        });

        // Staging from the file list shifts the diff that is on screen.
        m_repository->signal_status_changed().connect([this] {
            m_diff_view->reload();
        });

        m_commit_panel->signal_commit_and_push_requested().connect(sigc::mem_fun(*this, &MainWindow::push));

        m_history_view = std::make_unique<HistoryView>(*m_repository, *m_ref_service, *m_rebase_service);
        m_tabs.append_page(*m_history_view, "History");
        m_history_view->signal_interactive_rebase_requested().connect([this](const std::string &upstream) {
            m_rebase_service->request_plan(upstream);
        });
        m_history_view->signal_failed().connect([this](const Glib::ustring &summary, const Glib::ustring &details) {
            show_message(details.empty() ? summary : summary + " " + details, true);
        });
    }

    m_root.pack_start(m_pane_splitter, Gtk::PACK_EXPAND_WIDGET);

    m_status_bar.set_margin_start(6);
    m_status_bar.set_margin_end(6);
    m_status_bar.set_margin_top(2);
    m_status_bar.set_margin_bottom(2);
    m_progress.set_xalign(0.0F);
    m_summary.set_xalign(1.0F);
    m_status_bar.pack_start(m_progress, Gtk::PACK_EXPAND_WIDGET);
    m_status_bar.pack_end(m_summary, Gtk::PACK_SHRINK);
    m_root.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)), Gtk::PACK_SHRINK);
    m_root.pack_start(m_status_bar, Gtk::PACK_SHRINK);

    add(m_root);
    show_all_children();

    // Applied on the first real allocation rather than now: Gtk::Paned clamps a
    // position against the current allocation, and during construction that is
    // still nothing, so the value would be reduced to the child's minimum.
    // Reopening on the tab that was last in use, the way the rest of the window
    // state already behaves.
    if (Settings::instance().active_tab() < m_tabs.get_n_pages()) {
        m_tabs.set_current_page(Settings::instance().active_tab());
    }
    m_tabs.signal_switch_page().connect([](Gtk::Widget *, guint page) {
        Settings::instance().set_active_tab(static_cast<int>(page));
        Settings::instance().save();
    });

    m_pane_position_connection = m_pane_splitter.signal_size_allocate().connect([this](Gtk::Allocation &allocation) {
        if (m_panes_positioned || allocation.get_width() < s_side_pane_width * 2) {
            return;
        }
        m_panes_positioned = true;

        m_files_and_diff.set_position(s_file_list_width);
        if (m_commit_splitter.get_allocated_height() > s_commit_panel_height) {
            m_commit_splitter.set_position(m_commit_splitter.get_allocated_height() - s_commit_panel_height);
        }

        m_pane_position_connection.disconnect();
    });

    m_message.hide();
    m_operation_banner.hide();
    if (!m_repository) {
        // An empty strip beside the view reads as breakage rather than as nothing.
        m_side_pane.hide();
    }
}

void MainWindow::build_menu()
{
    const auto add_menu = [this](const Glib::ustring &label) {
        auto *item = Gtk::manage(new Gtk::MenuItem(label, true));
        auto *menu = Gtk::manage(new Gtk::Menu());
        item->set_submenu(*menu);
        m_menu_bar.append(*item);
        return menu;
    };

    const auto add_item = [](Gtk::Menu *menu, const Glib::ustring &label, const sigc::slot<void()> &slot) {
        auto *item = Gtk::manage(new Gtk::MenuItem(label, true));
        item->signal_activate().connect(slot);
        menu->append(*item);
        return item;
    };

    // File / Edit / View / Go, the way a file manager arranges them, rather than
    // Repository / Branch / Remote.
    Gtk::Menu *file = add_menu("_File");
    add_item(file, "_Open Repository…", sigc::mem_fun(*this, &MainWindow::open_repository));
    add_item(file, "_Create Repository…", sigc::mem_fun(*this, &MainWindow::init_repository));
    if (m_sidebar) {
        file->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item(file, "New _Branch…", [this] {
            m_sidebar->create_branch();
        });
        add_item(file, "New _Tag…", [this] {
            m_sidebar->create_tag();
        });
        add_item(file, "Add _Worktree…", [this] {
            m_sidebar->add_worktree();
        });
    }
    file->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
    add_item(file, "_Close", [this] {
        close();
    });

    Gtk::Menu *edit = add_menu("_Edit");
    if (m_working_copy_view) {
        add_item(edit, "_Stage", [this] {
            m_working_copy_view->stage_selected();
        });
        add_item(edit, "_Unstage", [this] {
            m_working_copy_view->unstage_selected();
        });
        edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item(edit, "Stage _All", [this] {
            m_working_copy_view->stage_all();
        });
        add_item(edit, "Unstage A_ll", [this] {
            m_working_copy_view->unstage_all();
        });
        edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item(edit, "_Discard Changes…", [this] {
            m_working_copy_view->discard_selected();
        });
    }
    if (m_sidebar) {
        edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item(edit, "Stas_h Changes…", [this] {
            m_sidebar->stash_changes();
        });
    }

    Gtk::Menu *view = add_menu("_View");
    if (m_repository) {
        auto *side_pane = Gtk::manage(new Gtk::CheckMenuItem("Side _Pane", true));
        side_pane->set_active(true);
        side_pane->signal_toggled().connect([this, side_pane] {
            m_side_pane.set_visible(side_pane->get_active());
        });
        view->append(*side_pane);
        view->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

        add_item(view, "_Reload", [this] {
            m_repository->refresh_status();
        });
    }
    view->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
    add_item(view, "_Preferences…", sigc::mem_fun(*this, &MainWindow::show_settings));

    Gtk::Menu *go = add_menu("_Go");
    if (m_remote_service) {
        add_item(go, "_Fetch", [this] {
            m_remote_service->fetch();
        });
        add_item(go, "_Pull", sigc::mem_fun(*this, &MainWindow::pull));
        add_item(go, "_Push", sigc::mem_fun(*this, &MainWindow::push));
        go->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item(go, "Force Push (_Amend)…", sigc::mem_fun(*this, &MainWindow::amend_push));
    }
}

void MainWindow::build_toolbar()
{
    // Flat and icon-only, so it does not compete with the location bar beneath it.
    m_toolbar.set_toolbar_style(Gtk::TOOLBAR_ICONS);
    m_toolbar.set_icon_size(Gtk::ICON_SIZE_SMALL_TOOLBAR);
    m_toolbar.get_style_context()->add_class("primary-toolbar");

    // Each item is a plain Gtk::Button inside a Gtk::ToolItem rather than a
    // Gtk::ToolButton. ToolButton builds its own button child and does not reliably
    // pass a tooltip down to it, so hovering told the user nothing; a button owns
    // its tooltip directly.
    //
    // The tooltip goes on the wrapping tool item as well. Staging buttons spend most
    // of their life insensitive, waiting for a selection, and that is exactly when
    // the user most wants to know what they would do; the item stays sensitive and
    // answers for the button.
    const auto add_button = [this](const Glib::ustring &icon, const Glib::ustring &tooltip, const sigc::slot<void()> &slot) {
        auto *button = Gtk::manage(new Gtk::Button());
        button->set_image_from_icon_name(icon, Gtk::ICON_SIZE_SMALL_TOOLBAR);
        button->set_relief(Gtk::RELIEF_NONE);
        button->set_focus_on_click(false);
        button->set_tooltip_text(tooltip);
        button->signal_clicked().connect(slot);

        auto *item = Gtk::manage(new Gtk::ToolItem());
        item->set_tooltip_text(tooltip);
        item->add(*button);
        m_toolbar.append(*item);

        return button;
    };

    const auto add_separator = [this] {
        m_toolbar.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
    };

    if (m_repository) {
        // Tooltips say what the action does and what it acts on, since an icon-only
        // toolbar is the only place that information exists.
        m_stage_button = add_button("list-add-symbolic", "Stage the selected files, so the next commit includes them", [this] {
            if (m_working_copy_view) {
                m_working_copy_view->stage_selected();
            }
        });
        m_unstage_button = add_button("list-remove-symbolic", "Unstage the selected files, leaving the working tree alone", [this] {
            if (m_working_copy_view) {
                m_working_copy_view->unstage_selected();
            }
        });
        m_discard_button = add_button("edit-delete-symbolic", "Throw away the selected changes — this cannot be undone", [this] {
            if (m_working_copy_view) {
                m_working_copy_view->discard_selected();
            }
        });

        add_separator();

        add_button("view-refresh-symbolic", "Re-read the repository from disk", [this] {
            m_repository->refresh_status();
        });

        add_separator();

        add_button("document-save-symbolic", "Fetch from every remote, without changing your branch", [this] {
            m_remote_service->fetch();
        });
        m_pull_button = add_button("go-down-symbolic",
                                   "Pull: fetch, then integrate the upstream branch the way your git config says",
                                   sigc::mem_fun(*this, &MainWindow::pull));
        m_push_button = add_button("go-up-symbolic", "Push the current branch to its remote", sigc::mem_fun(*this, &MainWindow::push));
        m_amend_push_button = add_button("go-top-symbolic",
                                         "Force push, replacing the remote branch — shows what would be destroyed first",
                                         sigc::mem_fun(*this, &MainWindow::amend_push));

        add_separator();

        add_button("vcs-branch", "Create a branch at the current commit", [this] {
            if (m_sidebar) {
                m_sidebar->create_branch();
            }
        });
        add_button("document-save-as-symbolic", "Set the working tree aside as a stash", [this] {
            if (m_sidebar) {
                m_sidebar->stash_changes();
            }
        });
    }

}

void MainWindow::update_summary()
{
    if (!m_repository) {
        return;
    }

    const StatusSnapshot &status = m_repository->status();

    Glib::ustring branch;
    if (status.m_is_detached) {
        branch = Glib::ustring::compose("detached at %1", status.m_head_oid.substr(0, 9));
    } else if (status.m_is_unborn) {
        branch = Glib::ustring::compose("%1 (empty)", status.m_branch);
    } else {
        branch = status.m_branch;
    }

    if (status.m_has_ahead_behind && (status.m_ahead > 0 || status.m_behind > 0)) {
        branch += Glib::ustring::compose("  ↑%1 ↓%2", status.m_ahead, status.m_behind);
    }

    m_branch.set_text(branch);
    m_branch.set_tooltip_text(status.m_upstream.empty() ? "No upstream branch is configured."
                                                        : Glib::ustring::compose("Tracking %1", status.m_upstream));

    int staged = 0;
    int unstaged = 0;
    int conflicted = 0;
    for (const StatusEntry &entry : status.m_entries) {
        if (entry.is_conflicted()) {
            ++conflicted;
            continue;
        }
        if (entry.is_staged()) {
            ++staged;
        }
        if (entry.is_unstaged()) {
            ++unstaged;
        }
    }

    // A plain count, the way a file manager reports what it is showing.
    if (conflicted > 0) {
        m_summary.set_text(conflicted == 1 ? "1 conflicting file" : Glib::ustring::compose("%1 conflicting files", conflicted));
    } else if (staged == 0 && unstaged == 0) {
        m_summary.set_text("No changes");
    } else {
        m_summary.set_text(Glib::ustring::compose("%1 staged, %2 unstaged", staged, unstaged));
    }

    if (m_operation_state->in_progress()) {
        Glib::ustring text = m_operation_state->description();
        text += conflicted > 0 ? " Resolve the conflicting files, then continue." : " Nothing is conflicting any more.";
        m_operation_label.set_text(text);
        m_operation_banner.set_message_type(conflicted > 0 ? Gtk::MESSAGE_WARNING : Gtk::MESSAGE_INFO);

        // Continuing with conflicts still in the index is not possible, so the button
        // says so by being unavailable rather than by failing when pressed.
        if (m_continue_button != nullptr) {
            m_continue_button->set_sensitive(conflicted == 0);
        }
        // A merge is a single step; there is nothing to skip past.
        if (m_skip_button != nullptr) {
            m_skip_button->set_visible(m_conflict_service->can_skip());
            m_skip_button->set_no_show_all(!m_conflict_service->can_skip());
        }
        m_operation_banner.show();
    } else {
        m_operation_banner.hide();
    }
}

void MainWindow::update_actions()
{
    if (!m_repository) {
        return;
    }

    const StatusSnapshot &status = m_repository->status();

    if (m_stage_button && m_working_copy_view) {
        m_stage_button->set_sensitive(m_working_copy_view->has_unstaged_selection());
        m_unstage_button->set_sensitive(m_working_copy_view->has_staged_selection());
        m_discard_button->set_sensitive(m_working_copy_view->has_unstaged_selection());
    }

    // Nothing to push from a branch with no commits, and no branch name to push to
    // when HEAD is detached.
    const bool pushable = !status.m_is_unborn && !status.m_is_detached && !status.m_branch.empty();
    if (m_push_button) {
        m_push_button->set_sensitive(pushable);
        m_amend_push_button->set_sensitive(pushable);
    }
    if (m_pull_button) {
        // Unlike a push, a pull cannot invent an upstream: there is nothing to
        // integrate until one is configured.
        m_pull_button->set_sensitive(pushable && !status.m_upstream.empty());
    }
}

void MainWindow::start_in_commit_mode()
{
    if (m_working_copy_page_num < 0 || !m_commit_panel) {
        // No repository was found, so the window is showing the offer to open or
        // initialise one; there is nothing to commit to yet.
        return;
    }

    m_tabs.set_current_page(m_working_copy_page_num);
    m_commit_panel->focus_message();
}

void MainWindow::show_message(const Glib::ustring &text, bool is_error)
{
    m_message.set_message_type(is_error ? Gtk::MESSAGE_ERROR : Gtk::MESSAGE_INFO);
    m_message_label.set_text(text);
    m_message.show();
}

void MainWindow::show_no_repository_offer()
{
    // An empty window with a sentence in it leaves the user to work out what to do
    // next. The two things they could want are right here instead.
    m_message.set_message_type(Gtk::MESSAGE_INFO);
    m_message_label.set_text("No Git repository is open.");

    m_message.add_button("_Open…", 10);
    m_message.add_button("_Create Here…", 11);
    m_message.signal_response().connect([this](int response) {
        if (response == 10) {
            open_repository();
        } else if (response == 11) {
            init_repository();
        }
    });

    m_message.show();
}

void MainWindow::on_rebase_plan_ready(const std::string &upstream, const std::vector<RebaseStep> &steps)
{
    if (steps.empty()) {
        show_message("There is nothing to rewrite: no commits come after that one.");
        return;
    }

    RebaseTodoDialog dialog(*this, upstream, steps);
    if (dialog.run() == Gtk::RESPONSE_ACCEPT) {
        m_rebase_service->start_interactive_rebase(upstream, dialog.steps());
    }
}

void MainWindow::on_editor_message_requested(const std::string &file)
{
    MessageEditorDialog dialog(*this, file);
    if (dialog.run() == Gtk::RESPONSE_ACCEPT && dialog.save()) {
        m_editor_bridge->accept();
    } else {
        // Rejecting makes the helper exit non-zero, which tells git to abort this
        // step rather than commit a message the user did not approve.
        m_editor_bridge->reject();
    }
}

void MainWindow::on_editor_sequence_requested(const std::string &file)
{
    std::ifstream source(file, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
    source.close();

    RebaseTodoDialog dialog(*this, "the base", RebaseTodo::parse(contents));
    if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
        m_editor_bridge->reject();
        return;
    }

    const std::string todo = RebaseTodo::render(dialog.steps());
    const std::string temporary = file + ".norn.tmp";

    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            m_editor_bridge->reject();
            return;
        }
        out.write(todo.data(), static_cast<std::streamsize>(todo.size()));
    }

    if (g_rename(temporary.c_str(), file.c_str()) != 0) {
        g_unlink(temporary.c_str());
        m_editor_bridge->reject();
        return;
    }

    m_editor_bridge->accept();
}

void MainWindow::show_settings()
{
    SettingsDialog dialog(*this);

    // Apply without closing, so the effect can be seen before committing to it.
    while (dialog.run() == Gtk::RESPONSE_APPLY) {
        dialog.apply();
    }
}

void MainWindow::open_in_editor(const std::string &path)
{
    const std::string absolute = Glib::build_filename(m_repository->toplevel(), path);

    // Already open: focus that tab rather than opening the file twice.
    for (std::size_t i = 0; i < m_editors.size(); ++i) {
        if (m_editors[i]->path() == absolute) {
            m_tabs.set_current_page(m_tabs.page_num(*m_editors[i]));
            return;
        }
    }

    auto editor = std::make_unique<EditorView>(absolute);
    EditorView *view = editor.get();
    m_editors.push_back(std::move(editor));

    const std::string name = Glib::path_get_basename(absolute);

    // A tab with its own close button; the two permanent tabs have none.
    auto *label_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
    auto *label = Gtk::manage(new Gtk::Label(name));
    auto *close = Gtk::manage(new Gtk::Button());
    close->set_image_from_icon_name("window-close-symbolic", Gtk::ICON_SIZE_MENU);
    close->set_relief(Gtk::RELIEF_NONE);
    close->set_focus_on_click(false);
    label_box->pack_start(*label, Gtk::PACK_SHRINK);
    label_box->pack_start(*close, Gtk::PACK_SHRINK);
    label_box->show_all();

    const int page = m_tabs.append_page(*view, *label_box);
    m_tabs.set_current_page(page);

    view->signal_modified_changed().connect([label, name](bool modified) {
        // A trailing marker rather than a different icon: the tab bar is narrow and
        // the name is what the user is scanning for.
        label->set_text(modified ? name + " *" : name);
    });

    // Saving changes the working tree, so the status is immediately stale.
    view->signal_saved().connect([this] {
        m_repository->refresh_status();
    });

    close->signal_clicked().connect([this, view] {
        if (view->is_modified()) {
            Gtk::MessageDialog dialog(*this,
                                      Glib::ustring::compose("“%1” has unsaved changes.", Glib::path_get_basename(view->path())),
                                      false,
                                      Gtk::MESSAGE_WARNING,
                                      Gtk::BUTTONS_NONE,
                                      true);
            dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
            dialog.add_button("_Discard", Gtk::RESPONSE_REJECT);
            dialog.add_button("_Save", Gtk::RESPONSE_ACCEPT);
            dialog.set_default_response(Gtk::RESPONSE_ACCEPT);

            const int response = dialog.run();
            if (response == Gtk::RESPONSE_CANCEL) {
                return;
            }
            if (response == Gtk::RESPONSE_ACCEPT && !view->save()) {
                show_message("Could not save the file.", true);
                return;
            }
        }

        m_tabs.remove_page(*view);
        std::erase_if(m_editors, [view](const std::unique_ptr<EditorView> &owned) {
            return owned.get() == view;
        });
    });
}

void MainWindow::open_repository()
{
    Gtk::FileChooserDialog dialog(*this, "Open Repository", Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Open", Gtk::RESPONSE_ACCEPT);

    if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
        return;
    }

    const RepositoryLocator locator = RepositoryLocator::locate(dialog.get_filename());
    if (!locator.is_found()) {
        show_message(locator.error_text(), true);
        return;
    }

    auto *window = new MainWindow(locator.toplevel());
    window->show();
    close();
}

void MainWindow::init_repository()
{
    Gtk::FileChooserDialog dialog(*this, "Create a Repository In", Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Create", Gtk::RESPONSE_ACCEPT);

    if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
        return;
    }

    const std::string directory = dialog.get_filename();

    // No repository yet, so there is no Repository or GitRunner to go through.
    std::string out;
    std::string err;
    int status = 0;

    try {
        Glib::spawn_sync(directory, std::vector<std::string>{"git", "init"}, Glib::SPAWN_SEARCH_PATH, Glib::SlotSpawnChildSetup(), &out, &err, &status);
    } catch (const Glib::Error &error) {
        show_message(error.what(), true);
        return;
    }

    if (status != 0) {
        show_message(Glib::ustring::compose("Could not create a repository there. %1", err), true);
        return;
    }

    auto *window = new MainWindow(directory);
    window->show();
    close();
}

void MainWindow::push()
{
    const StatusSnapshot &status = m_repository->status();
    if (status.m_branch.empty()) {
        return;
    }

    // With no upstream yet, the first push should establish one.
    const bool set_upstream = status.m_upstream.empty();
    const std::string remote = status.m_upstream.empty() ? "origin" : status.m_upstream.substr(0, status.m_upstream.find('/'));

    m_remote_service->push(remote, status.m_branch, set_upstream, false);
}

void MainWindow::pull()
{
    const StatusSnapshot &status = m_repository->status();
    if (status.m_upstream.empty()) {
        show_message("This branch has no upstream branch to pull from.", true);
        return;
    }

    const std::string remote = status.m_upstream.substr(0, status.m_upstream.find('/'));
    m_progress.set_text("Pulling from " + remote + "…");
    m_remote_service->pull(remote, status.m_upstream);
}

void MainWindow::amend_push()
{
    const StatusSnapshot &status = m_repository->status();
    if (status.m_branch.empty()) {
        return;
    }

    const std::string remote = status.m_upstream.empty() ? "origin" : status.m_upstream.substr(0, status.m_upstream.find('/'));

    m_awaiting_force_push_preview = true;
    m_progress.set_text("Checking what the remote holds…");
    m_remote_service->prepare_force_push(remote, status.m_branch);
}

void MainWindow::on_force_push_preview_ready(const ForcePushPreview &preview)
{
    if (!m_awaiting_force_push_preview) {
        return;
    }
    m_awaiting_force_push_preview = false;
    m_progress.set_text({});

    if (!preview.m_remote_branch_exists) {
        // Nothing to overwrite, so this is an ordinary first push.
        m_remote_service->push(preview.m_remote, preview.m_branch, true, false);
        return;
    }

    if (preview.m_added.empty() && preview.m_removed.empty()) {
        show_message(Glib::ustring::compose("%1/%2 already matches your local branch.", preview.m_remote, preview.m_branch));
        return;
    }

    // The point of this dialog is the "would be lost" list. A force push that only
    // adds commits is routine; one that removes commits is destroying work that
    // exists on the remote, and the user has to see that before it happens.
    Glib::ustring detail;
    if (preview.is_destructive()) {
        detail += preview.m_removed.size() == 1
            ? Glib::ustring::compose("1 commit would be permanently removed from %1/%2:\n", preview.m_remote, preview.m_branch)
            : Glib::ustring::compose("%1 commits would be permanently removed from %2/%3:\n", preview.m_removed.size(), preview.m_remote, preview.m_branch);
        for (const std::string &line : preview.m_removed) {
            detail += "  " + line + "\n";
        }
        detail += "\n";
    }

    if (!preview.m_added.empty()) {
        detail += "These commits would be added:\n";
        for (const std::string &line : preview.m_added) {
            detail += "  " + line + "\n";
        }
        detail += "\n";
    }

    detail += Glib::ustring::compose("The push is protected by a lease on %1, so it will be refused if the remote has changed since this was checked.",
                                     preview.m_expected_oid.substr(0, 9));

    Gtk::MessageDialog dialog(*this,
                              preview.is_destructive() ? "This force push will destroy commits on the remote." : "Force push to move the remote branch?",
                              false,
                              preview.is_destructive() ? Gtk::MESSAGE_WARNING : Gtk::MESSAGE_QUESTION,
                              Gtk::BUTTONS_NONE,
                              true);
    dialog.set_secondary_text(detail);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Force _Push", Gtk::RESPONSE_ACCEPT);
    // Cancel keeps the focus, so an absent-minded Return destroys nothing.
    dialog.set_default_response(Gtk::RESPONSE_CANCEL);

    if (dialog.run() == Gtk::RESPONSE_ACCEPT) {
        m_remote_service->force_push(preview);
    }
}
