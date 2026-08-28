/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "repositorysidebar.h"

#include "core/refservice.h"
#include "core/repository.h"

#include <giomm/appinfo.h>
#include <giomm/file.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/label.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/separatormenuitem.h>

#include <functional>
#include <map>

namespace
{
/*! Extra indentation per level, on top of the expander's own width. */
constexpr int s_level_indentation = 10;

/*!
 * The first of @p names the icon theme actually has.
 *
 * The vcs-* icons exist in Breeze but not in Adwaita, so a name that looks right
 * on this desktop would be a missing-image square on a plain GTK one.
 */
Glib::ustring first_available_icon(const std::vector<Glib::ustring> &names)
{
    const Glib::RefPtr<Gtk::IconTheme> theme = Gtk::IconTheme::get_default();
    for (const Glib::ustring &name : names) {
        if (theme->has_icon(name)) {
            return name;
        }
    }
    return names.empty() ? Glib::ustring() : names.back();
}

Glib::ustring branch_icon(bool is_head)
{
    static const Glib::ustring head = first_available_icon({"vcs-branch", "media-record-symbolic"});
    static const Glib::ustring other = first_available_icon({"branch", "vcs-branch", "media-playlist-consecutive-symbolic"});
    return is_head ? head : other;
}

Glib::ustring tag_icon()
{
    static const Glib::ustring name = first_available_icon({"tag", "starred-symbolic"});
    return name;
}

Glib::ustring stash_icon()
{
    static const Glib::ustring name = first_available_icon({"vcs-stash", "document-save-symbolic"});
    return name;
}
}

RepositorySidebar::RepositorySidebar(Repository &repository, RefService &ref_service)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
    , m_repository(repository)
    , m_ref_service(ref_service)
{
    build_ui();

    m_ref_service.signal_changed().connect(sigc::mem_fun(*this, &RepositorySidebar::rebuild));
    rebuild();

    show_all_children();
}

RepositorySidebar::~RepositorySidebar()
{
    // The restore runs from an idle, which would otherwise fire into freed memory.
    m_scroll_restore.disconnect();
}

void RepositorySidebar::build_ui()
{
    m_store = Gtk::TreeStore::create(m_columns);
    m_view.set_model(m_store);
    m_view.set_headers_visible(false);
    m_view.set_enable_search(true);
    m_view.set_search_column(m_columns.m_display);
    m_view.set_tooltip_column(m_columns.m_tooltip.index());
    m_view.get_selection()->set_mode(Gtk::SELECTION_SINGLE);

    // Without these the pane reads as a flat list with slightly indented rows.
    // Tree lines and a wider indent are what make the branches under a section, or
    // the branches under a remote, look like they belong to it.
    m_view.set_enable_tree_lines(true);
    m_view.set_level_indentation(s_level_indentation);
    m_view.set_show_expanders(true);

    auto *column = Gtk::manage(new Gtk::TreeViewColumn());
    auto *icon = Gtk::manage(new Gtk::CellRendererPixbuf());
    icon->property_stock_size() = Gtk::ICON_SIZE_MENU;
    column->pack_start(*icon, false);
    column->add_attribute(icon->property_icon_name(), m_columns.m_icon);

    auto *text = Gtk::manage(new Gtk::CellRendererText());
    text->property_ellipsize() = Pango::ELLIPSIZE_MIDDLE;
    column->pack_start(*text, true);
    column->add_attribute(text->property_text(), m_columns.m_display);
    // The checked-out branch and the current worktree are bold: they are the two
    // pieces of state the pane exists to make unmistakable at a glance.
    column->add_attribute(text->property_weight(), m_columns.m_weight);
    column->add_attribute(text->property_sensitive(), m_columns.m_has_contents);

    m_view.append_column(*column);

    m_scroller.add(m_view);
    m_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_scroller.set_shadow_type(Gtk::SHADOW_NONE);
    pack_start(m_scroller, Gtk::PACK_EXPAND_WIDGET);

    // The tree is thrown away and rebuilt on every refresh, and a refresh follows
    // every write and every file the watcher sees change. Without remembering what
    // the user closed, a section reopens a moment after being collapsed.
    m_view.signal_row_expanded().connect([this](const Gtk::TreeModel::iterator &iterator, const Gtk::TreeModel::Path &) {
        on_row_expansion_changed(iterator, true);
    });
    m_view.signal_row_collapsed().connect([this](const Gtk::TreeModel::iterator &iterator, const Gtk::TreeModel::Path &) {
        on_row_expansion_changed(iterator, false);
    });

    m_view.signal_button_press_event().connect(sigc::mem_fun(*this, &RepositorySidebar::on_button_press), false);
    m_view.signal_row_activated().connect(sigc::mem_fun(*this, &RepositorySidebar::on_row_activated));
}

void RepositorySidebar::rebuild()
{

    std::string selected_key;
    if (const Gtk::TreeModel::iterator selected = m_view.get_selection()->get_selected()) {
        selected_key = (*selected)[m_columns.m_key];
    }

    // Built into a fresh store rather than over the live one. Most refreshes
    // change nothing here — a file saved in the working tree moves no ref — and
    // rebuilding regardless is what threw the pane back to the top mid-scroll.
    const Glib::RefPtr<Gtk::TreeStore> store = Gtk::TreeStore::create(m_columns);

    const auto add_section = [this, &store](const Glib::ustring &label) {
        Gtk::TreeModel::Row row = *store->append();
        row[m_columns.m_key] = "section:" + label.raw();
        row[m_columns.m_display] = label;
        row[m_columns.m_kind] = static_cast<int>(NodeKind::Section);
        row[m_columns.m_weight] = PANGO_WEIGHT_BOLD;
        return row;
    };

    Gtk::TreeModel::Row branches = add_section("Branches");
    Gtk::TreeModel::Row remotes = add_section("Remotes");
    Gtk::TreeModel::Row tags = add_section("Tags");
    Gtk::TreeModel::Row stashes = add_section("Stashes");
    Gtk::TreeModel::Row worktrees = add_section("Worktrees");
    Gtk::TreeModel::Row submodules = add_section("Submodules");

    // Remote branches nest under a node per remote, so a repository with several
    // remotes does not turn into one flat list of origin/… and upstream/… mixed.
    std::map<std::string, Gtk::TreeModel::Row> remote_nodes;

    for (const RefRecord &ref : m_ref_service.refs()) {
        switch (ref.m_kind) {
        case RefRecord::Kind::LocalBranch: {
            Gtk::TreeModel::Row row = *store->append(branches.children());
            row[m_columns.m_key] = "branch:" + ref.m_short_name;
            Glib::ustring display = ref.m_short_name;
            if (ref.m_upstream_gone) {
                display += " (gone)";
            } else if (ref.m_ahead > 0 || ref.m_behind > 0) {
                display += Glib::ustring::compose("  ↑%1 ↓%2", ref.m_ahead, ref.m_behind);
            }

            row[m_columns.m_display] = display;
            row[m_columns.m_icon] = branch_icon(ref.m_is_head);
            row[m_columns.m_kind] = static_cast<int>(NodeKind::LocalBranch);
            row[m_columns.m_name] = ref.m_short_name;
            row[m_columns.m_commit] = ref.commit();
            row[m_columns.m_is_head] = ref.m_is_head;
                row[m_columns.m_weight] = ref.m_is_head ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
            row[m_columns.m_has_contents] = true;
            row[m_columns.m_tooltip] = ref.m_upstream_gone
                ? Glib::ustring::compose("%1 — its upstream %2 no longer exists on the remote", ref.m_subject, ref.m_upstream)
                : (ref.m_has_tracking ? Glib::ustring::compose("%1 — tracking %2", ref.m_subject, ref.m_upstream)
                                      : Glib::ustring::compose("%1 — no upstream configured", ref.m_subject));
            break;
        }

        case RefRecord::Kind::RemoteBranch: {
            const std::string remote = ref.remote_name();
            if (remote_nodes.find(remote) == remote_nodes.end()) {
                Gtk::TreeModel::Row node = *store->append(remotes.children());
                node[m_columns.m_key] = "remote:" + remote;
                node[m_columns.m_display] = remote;
                node[m_columns.m_icon] = "network-server-symbolic";
                node[m_columns.m_kind] = static_cast<int>(NodeKind::Remote);
                node[m_columns.m_name] = remote;
                node[m_columns.m_weight] = PANGO_WEIGHT_NORMAL;
                node[m_columns.m_has_contents] = true;
                remote_nodes.emplace(remote, node);
            }

            Gtk::TreeModel::Row row = *store->append(remote_nodes.at(remote).children());
            row[m_columns.m_key] = "remote-branch:" + ref.m_short_name;
            // Already nested under the remote, so the prefix would only repeat it.
            const std::size_t slash = ref.m_short_name.find('/');
            row[m_columns.m_display] = slash == std::string::npos ? ref.m_short_name : ref.m_short_name.substr(slash + 1);
            row[m_columns.m_icon] = branch_icon(false);
            row[m_columns.m_kind] = static_cast<int>(NodeKind::RemoteBranch);
            row[m_columns.m_name] = ref.m_short_name;
            row[m_columns.m_commit] = ref.commit();
            row[m_columns.m_weight] = PANGO_WEIGHT_NORMAL;
            row[m_columns.m_has_contents] = true;
            row[m_columns.m_tooltip] = ref.m_subject;
            break;
        }

        case RefRecord::Kind::Tag: {
            Gtk::TreeModel::Row row = *store->append(tags.children());
            row[m_columns.m_key] = "tag:" + ref.m_short_name;
            row[m_columns.m_display] = ref.m_short_name;
            row[m_columns.m_icon] = tag_icon();
            row[m_columns.m_kind] = static_cast<int>(NodeKind::Tag);
            row[m_columns.m_name] = ref.m_short_name;
            row[m_columns.m_commit] = ref.commit();
            row[m_columns.m_weight] = PANGO_WEIGHT_NORMAL;
            row[m_columns.m_has_contents] = true;
            row[m_columns.m_tooltip] = ref.m_is_annotated_tag ? Glib::ustring::compose("Annotated tag on %1", ref.commit().substr(0, 9))
                                                              : Glib::ustring::compose("Lightweight tag on %1", ref.commit().substr(0, 9));
            break;
        }
        }
    }

    for (const StashRecord &stash : m_ref_service.stashes()) {
        Gtk::TreeModel::Row row = *store->append(stashes.children());
        row[m_columns.m_key] = "stash:" + stash.m_object_name;
        row[m_columns.m_display] = stash.m_message;
        row[m_columns.m_icon] = stash_icon();
        row[m_columns.m_kind] = static_cast<int>(NodeKind::Stash);
        row[m_columns.m_stash_index] = stash.m_index;
        row[m_columns.m_stash_object] = stash.m_object_name;
        row[m_columns.m_weight] = PANGO_WEIGHT_NORMAL;
        row[m_columns.m_has_contents] = true;
        row[m_columns.m_tooltip] = stash.m_date;
    }

    for (const WorktreeRecord &worktree : m_ref_service.worktrees()) {
        Gtk::TreeModel::Row row = *store->append(worktrees.children());
        row[m_columns.m_key] = "worktree:" + worktree.m_path;

        const std::string name = Glib::path_get_basename(worktree.m_path);
        const std::string where = worktree.m_is_detached ? worktree.m_head.substr(0, 9) : worktree.m_branch;

        row[m_columns.m_display] = worktree.m_is_prunable ? Glib::ustring::compose("%1 (missing)", name)
                                                          : Glib::ustring::compose("%1 — %2", name, where);
        row[m_columns.m_icon] = worktree.m_is_current ? "folder-open-symbolic" : "folder-symbolic";
        row[m_columns.m_kind] = static_cast<int>(NodeKind::Worktree);
        row[m_columns.m_path] = worktree.m_path;
        row[m_columns.m_weight] = worktree.m_is_current ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
        row[m_columns.m_has_contents] = true;
        // Neither the main worktree nor the one currently open can be removed: the
        // first is where the repository lives, and the second is underneath us.
        row[m_columns.m_removable] = !worktree.m_is_main && !worktree.m_is_current;
        row[m_columns.m_tooltip] = worktree.m_is_locked ? Glib::ustring::compose("%1 — locked: %2", worktree.m_path, worktree.m_lock_reason)
                                                        : Glib::ustring(worktree.m_path);
    }

    for (const SubmoduleRecord &submodule : m_ref_service.submodules()) {
        Gtk::TreeModel::Row row = *store->append(submodules.children());
        row[m_columns.m_key] = "submodule:" + submodule.m_path;

        row[m_columns.m_display] = submodule.m_is_uninitialised ? Glib::ustring::compose("%1 (not checked out)", submodule.m_path)
            : submodule.m_is_modified                           ? Glib::ustring::compose("%1 (moved)", submodule.m_path)
                                                                : Glib::ustring(submodule.m_path);
        row[m_columns.m_icon] = submodule.m_is_uninitialised ? "folder-remote-symbolic" : "folder-symbolic";
        row[m_columns.m_kind] = static_cast<int>(NodeKind::Submodule);
        row[m_columns.m_path] = submodule.m_path;
        row[m_columns.m_is_head] = submodule.m_is_uninitialised;
        row[m_columns.m_weight] = PANGO_WEIGHT_NORMAL;
        row[m_columns.m_has_contents] = true;
        row[m_columns.m_tooltip] = submodule.m_describe.empty() ? Glib::ustring(submodule.m_commit.substr(0, 9))
                                                                : Glib::ustring::compose("%1 at %2", submodule.m_describe, submodule.m_commit.substr(0, 9));
    }

    const auto label_with_count = [this](Gtk::TreeModel::Row row, const Glib::ustring &label) {
        const std::size_t count = row.children().size();
        row[m_columns.m_display] = count > 0 ? Glib::ustring::compose("%1 (%2)", label, count) : label;
        // An empty section has no expander, so without dimming it is indistinguishable
        // from a collapsed one that does have contents.
        row[m_columns.m_has_contents] = count > 0;
    };

    label_with_count(branches, "Branches");
    label_with_count(remotes, "Remotes");
    label_with_count(tags, "Tags");
    label_with_count(stashes, "Stashes");
    label_with_count(worktrees, "Worktrees");
    label_with_count(submodules, "Submodules");

    const std::string signature = signature_of(store);
    if (signature == m_signature) {

        // Identical to what is already on screen, so the scroll position, the
        // expanders and the selection are all left exactly where the user put them.
        return;
    }
    m_signature = signature;

    const double scroll = m_scroller.get_vadjustment()->get_value();

    m_store = store;
    m_view.set_model(m_store);

    restore_view_state(selected_key);
    restore_scroll(scroll);
}

std::string RepositorySidebar::signature_of(const Glib::RefPtr<Gtk::TreeStore> &store) const
{
    std::string signature;

    const std::function<void(const Gtk::TreeModel::Children &, int)> walk = [&](const Gtk::TreeModel::Children &children, int depth) {
        for (const Gtk::TreeModel::Row &row : children) {
            // Every column the cell renderers read, so anything that would look
            // different on screen counts as a change and anything else does not.
            signature += std::to_string(depth);
            signature += '\x1f' + static_cast<std::string>(row[m_columns.m_key]);
            signature += '\x1f' + static_cast<Glib::ustring>(row[m_columns.m_display]).raw();
            signature += '\x1f' + static_cast<Glib::ustring>(row[m_columns.m_icon]).raw();
            signature += '\x1f' + static_cast<Glib::ustring>(row[m_columns.m_tooltip]).raw();
            signature += '\x1f' + std::to_string(static_cast<int>(row[m_columns.m_weight]));
            signature += '\x1f' + std::to_string(static_cast<bool>(row[m_columns.m_has_contents]));
            signature += '\x1e';
            walk(row.children(), depth + 1);
        }
    };
    walk(store->children(), 0);

    return signature;
}

void RepositorySidebar::restore_scroll(double value)
{
    if (value <= 0.0) {
        return;
    }

    // Setting it now would be clamped away: the view has not been laid out against
    // the new model yet, so the adjustment still describes an empty tree. A default
    // priority idle runs after GTK's resize pass, by which point the range is real.
    m_scroll_restore.disconnect();
    m_scroll_restore = Glib::signal_idle().connect(
        [this, value] {
            const Glib::RefPtr<Gtk::Adjustment> adjustment = m_scroller.get_vadjustment();
            const double furthest = std::max(0.0, adjustment->get_upper() - adjustment->get_page_size());
            adjustment->set_value(std::min(value, furthest));
            return false;
        },
        Glib::PRIORITY_DEFAULT_IDLE);
}

void RepositorySidebar::restore_view_state(const std::string &selected_key)
{
    // Expanding a row emits row-expanded, which would otherwise be read back as the
    // user opening it and erase a collapse they had just made elsewhere.
    m_restoring_view_state = true;

    // Top-down: a child path cannot be expanded before its parent is.
    const std::function<void(const Gtk::TreeModel::Children &)> walk = [&](const Gtk::TreeModel::Children &children) {
        for (const Gtk::TreeModel::Row &row : children) {
            if (row.children().empty()) {
                continue;
            }
            // Anything the user has not explicitly closed starts open: a collapsed
            // pane hides the thing it exists to show.
            const std::string key = row[m_columns.m_key];
            if (m_collapsed.find(key) == m_collapsed.end()) {
                m_view.expand_row(m_store->get_path(row), false);
                walk(row.children());
            }
        }
    };
    walk(m_store->children());

    if (!selected_key.empty()) {
        m_store->foreach_iter([&](const Gtk::TreeModel::iterator &iterator) {
            if (static_cast<std::string>((*iterator)[m_columns.m_key]) != selected_key) {
                return false;
            }
            // Only a row whose ancestors are open can be selected, so a selection
            // inside a section the user has closed is simply dropped.
            const Gtk::TreeModel::Path path = m_store->get_path(iterator);
            // up() on a depth-1 path succeeds and leaves an empty one, which no row
            // can be expanded at, so a top-level row is checked by depth instead.
            Gtk::TreeModel::Path parent = path;
            if (path.size() == 1 || (parent.up() && m_view.row_expanded(parent))) {
                m_view.get_selection()->select(path);
            }
            return true;
        });
    }

    m_restoring_view_state = false;
}

void RepositorySidebar::on_row_expansion_changed(const Gtk::TreeModel::iterator &iterator, bool expanded)
{
    if (m_restoring_view_state) {
        return;
    }

    const std::string key = (*iterator)[m_columns.m_key];
    if (expanded) {
        m_collapsed.erase(key);
    } else {
        m_collapsed.insert(key);
    }
}

void RepositorySidebar::on_row_activated(const Gtk::TreeModel::Path &path, Gtk::TreeViewColumn *)
{
    const Gtk::TreeModel::Row row = *m_store->get_iter(path);
    const auto kind = static_cast<NodeKind>(static_cast<int>(row[m_columns.m_kind]));

    if (kind == NodeKind::LocalBranch && !row[m_columns.m_is_head]) {
        m_ref_service.checkout(row[m_columns.m_name]);
    } else if (kind == NodeKind::Worktree || kind == NodeKind::Submodule) {
        // A worktree or submodule is a repository in its own right, so it gets its
        // own window rather than replacing what is on screen.
        const std::string path_value = row[m_columns.m_path];
        const std::string absolute = Glib::path_is_absolute(path_value) ? path_value : Glib::build_filename(m_repository.toplevel(), path_value);
        m_signal_open_repository_requested.emit(absolute);
    }
}

bool RepositorySidebar::on_button_press(GdkEventButton *event)
{
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) {
        return false;
    }

    Gtk::TreeModel::Path path;
    if (!m_view.get_path_at_pos(static_cast<int>(event->x), static_cast<int>(event->y), path)) {
        return false;
    }

    m_view.get_selection()->select(path);
    show_context_menu(*m_store->get_iter(path), event);
    return true;
}

void RepositorySidebar::show_context_menu(const Gtk::TreeModel::Row &row, GdkEventButton *event)
{
    m_menu.foreach ([this](Gtk::Widget &child) {
        m_menu.remove(child);
    });

    const auto add_item = [this](const Glib::ustring &label, const sigc::slot<void()> &slot) {
        auto *item = Gtk::manage(new Gtk::MenuItem(label, true));
        item->signal_activate().connect(slot);
        m_menu.append(*item);
    };

    const auto kind = static_cast<NodeKind>(static_cast<int>(row[m_columns.m_kind]));
    const std::string name = row[m_columns.m_name];
    const std::string commit = row[m_columns.m_commit];
    const std::string path = row[m_columns.m_path];

    switch (kind) {
    case NodeKind::LocalBranch: {
        const bool is_head = row[m_columns.m_is_head];
        if (!is_head) {
            add_item(Glib::ustring::compose("Switch to “%1”", name), [this, name] {
                m_ref_service.checkout(name);
            });
            add_item(Glib::ustring::compose("Merge “%1” into the current branch…", name), [this, name] {
                if (confirm(Glib::ustring::compose("Merge “%1” into “%2”?", name, m_repository.status().m_branch), {}, "_Merge")) {
                    m_ref_service.merge(name, false);
                }
            });
            m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        }
        add_item("Rename…", [this, name] {
            Glib::ustring new_name;
            if (ask_for_text("Rename Branch", Glib::ustring::compose("New name for “%1”:", name), new_name, name) && new_name != name) {
                m_ref_service.rename_branch(name, new_name);
            }
        });
        if (!is_head) {
            add_item("Delete…", [this, name] {
                if (confirm(Glib::ustring::compose("Delete the branch “%1”?", name),
                            "git will refuse if it holds commits that exist nowhere else.",
                            "_Delete")) {
                    m_ref_service.delete_branch(name);
                }
            });
        }
        break;
    }

    case NodeKind::RemoteBranch: {
        const std::size_t slash = name.find('/');
        const std::string local = slash == std::string::npos ? name : name.substr(slash + 1);
        add_item(Glib::ustring::compose("Check out as “%1”", local), [this, local, name] {
            m_ref_service.create_branch(local, name, true);
        });
        add_item(Glib::ustring::compose("Merge “%1” into the current branch…", name), [this, name] {
            if (confirm(Glib::ustring::compose("Merge “%1” into “%2”?", name, m_repository.status().m_branch), {}, "_Merge")) {
                m_ref_service.merge(name, false);
            }
        });
        break;
    }

    case NodeKind::Tag:
        add_item(Glib::ustring::compose("Check out “%1”", name), [this, commit] {
            // Checking out a tag detaches HEAD; there is no branch to attach it to.
            m_ref_service.checkout_detached(commit);
        });
        add_item("Delete…", [this, name] {
            if (confirm(Glib::ustring::compose("Delete the tag “%1”?", name), {}, "_Delete")) {
                m_ref_service.delete_tag(name);
            }
        });
        break;

    case NodeKind::Stash: {
        const int index = row[m_columns.m_stash_index];
        const std::string object = row[m_columns.m_stash_object];
        add_item("Restore and remove", [this, index, object] {
            m_ref_service.stash_pop(index, object);
        });
        add_item("Restore and keep", [this, index, object] {
            m_ref_service.stash_apply(index, object);
        });
        m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item("Discard…", [this, index, object] {
            if (confirm("Discard this stash?", "This cannot be undone.", "_Discard")) {
                m_ref_service.stash_drop(index, object);
            }
        });
        break;
    }

    case NodeKind::Worktree: {
        const std::string absolute = path;
        add_item("Open in a new window", [this, absolute] {
            m_signal_open_repository_requested.emit(absolute);
        });
        add_item("Show in the file manager", [absolute] {
            try {
                Gio::AppInfo::launch_default_for_uri(Gio::File::create_for_path(absolute)->get_uri());
            } catch (const Glib::Error &) {
                // Nothing sensible to do; the file manager simply does not open.
            }
        });
        if (row[m_columns.m_removable]) {
            m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
            add_item("Remove…", [this, absolute] {
                if (confirm("Remove this worktree?", Glib::ustring::compose("Its directory %1 will be deleted.", absolute), "_Remove")) {
                    m_ref_service.remove_worktree(absolute);
                }
            });
        }
        m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item("Forget missing worktrees", [this] {
            m_ref_service.prune_worktrees();
        });
        break;
    }

    case NodeKind::Submodule: {
        const bool uninitialised = row[m_columns.m_is_head];
        if (uninitialised) {
            add_item("Check out", [this, path] {
                m_ref_service.update_submodule(path);
            });
            break;
        }
        const std::string absolute = Glib::build_filename(m_repository.toplevel(), path);
        add_item("Open in a new window", [this, absolute] {
            m_signal_open_repository_requested.emit(absolute);
        });
        m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item("Reset to the recorded commit", [this, path] {
            m_ref_service.update_submodule(path);
        });
        add_item("Move to the latest commit", [this, path] {
            m_ref_service.update_submodule_to_remote(path);
        });
        break;
    }

    case NodeKind::Section:
    case NodeKind::Remote:
        return;
    }

    if (m_menu.get_children().empty()) {
        return;
    }

    m_menu.show_all();
    m_menu.popup_at_pointer(reinterpret_cast<GdkEvent *>(event));
}

bool RepositorySidebar::ask_for_text(const Glib::ustring &title, const Glib::ustring &prompt, Glib::ustring &value, const Glib::ustring &initial)
{
    auto *parent = dynamic_cast<Gtk::Window *>(get_toplevel());

    Gtk::Dialog dialog(title, *parent, true);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_OK", Gtk::RESPONSE_ACCEPT);
    dialog.set_default_response(Gtk::RESPONSE_ACCEPT);

    Gtk::Box *content = dialog.get_content_area();
    content->set_spacing(6);
    content->set_margin_start(12);
    content->set_margin_end(12);
    content->set_margin_top(12);
    content->set_margin_bottom(6);

    auto *label = Gtk::manage(new Gtk::Label(prompt));
    label->set_xalign(0.0F);
    content->pack_start(*label, Gtk::PACK_SHRINK);

    auto *entry = Gtk::manage(new Gtk::Entry());
    entry->set_text(initial);
    entry->set_activates_default(true);
    content->pack_start(*entry, Gtk::PACK_SHRINK);

    dialog.show_all_children();

    if (dialog.run() != Gtk::RESPONSE_ACCEPT || entry->get_text().empty()) {
        return false;
    }

    value = entry->get_text();
    return true;
}

bool RepositorySidebar::confirm(const Glib::ustring &question, const Glib::ustring &detail, const Glib::ustring &accept_label)
{
    auto *parent = dynamic_cast<Gtk::Window *>(get_toplevel());

    Gtk::MessageDialog dialog(*parent, question, false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
    if (!detail.empty()) {
        dialog.set_secondary_text(detail);
    }
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button(accept_label, Gtk::RESPONSE_ACCEPT);
    dialog.set_default_response(Gtk::RESPONSE_CANCEL);

    return dialog.run() == Gtk::RESPONSE_ACCEPT;
}

void RepositorySidebar::create_branch()
{
    Glib::ustring name;
    if (ask_for_text("New Branch", "Name for a branch starting at the current commit:", name)) {
        m_ref_service.create_branch(name, {}, true);
    }
}

void RepositorySidebar::create_tag()
{
    Glib::ustring name;
    if (!ask_for_text("New Tag", "Name for a tag on the current commit:", name)) {
        return;
    }

    Glib::ustring message;
    // An empty message makes a lightweight tag, which is a meaningful choice rather
    // than a cancelled one, so the dialog being dismissed is handled separately.
    ask_for_text("Tag Message", "Message for the tag. Leave empty for a lightweight tag:", message);
    m_ref_service.create_tag(name, {}, message);
}

void RepositorySidebar::stash_changes()
{
    Glib::ustring message;
    if (ask_for_text("Stash Changes", "Describe what is being set aside:", message)) {
        // Untracked files are included: leaving them behind is the usual way a stash
        // turns out not to have set aside what the user expected.
        m_ref_service.stash_push(message, true);
    }
}

void RepositorySidebar::add_worktree()
{
    auto *parent = dynamic_cast<Gtk::Window *>(get_toplevel());

    Gtk::FileChooserDialog chooser(*parent, "Where to put the new worktree", Gtk::FILE_CHOOSER_ACTION_CREATE_FOLDER);
    chooser.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    chooser.add_button("_Select", Gtk::RESPONSE_ACCEPT);

    if (chooser.run() != Gtk::RESPONSE_ACCEPT) {
        return;
    }

    Glib::ustring branch;
    if (!ask_for_text("New Worktree", "Name for a new branch to check out there. Leave empty to check out the current commit:", branch)) {
        // git refuses to check the same branch out twice, so an empty name detaches
        // rather than failing.
        m_ref_service.add_worktree(chooser.get_filename(), {}, false);
        return;
    }

    m_ref_service.add_worktree(chooser.get_filename(), branch, true);
}
