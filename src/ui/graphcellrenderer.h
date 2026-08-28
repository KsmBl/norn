/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/commitrecord.h"

#include <gtkmm/cellrenderer.h>

/*!
 * Draws the commit graph column: lane lines, the commit dot, and the curves where
 * branches leave and merges converge.
 *
 * A custom renderer rather than text art, because the curves are what make a
 * branchy history readable and no arrangement of characters draws them.
 */
class GraphCellRenderer : public Gtk::CellRenderer
{
public:
    GraphCellRenderer();

    /*!
     * The row to draw, set from the column's cell data function before each render.
     *
     * A plain setter rather than a Glib::Property: GraphRow is ordinary C++ data
     * with no GType behind it, and registering one just to move a struct into a
     * renderer that is called synchronously would be pure ceremony.
     */
    void set_graph_row(const GraphRow &row)
    {
        m_graph_row = row;
    }

protected:
    void get_preferred_width_vfunc(Gtk::Widget &widget, int &minimum_width, int &natural_width) const override;
    void render_vfunc(const Cairo::RefPtr<Cairo::Context> &context,
                      Gtk::Widget &widget,
                      const Gdk::Rectangle &background_area,
                      const Gdk::Rectangle &cell_area,
                      Gtk::CellRendererState flags) override;

private:
    static void set_lane_colour(const Cairo::RefPtr<Cairo::Context> &context, int colour_index);
    static int lane_pitch(Gtk::Widget &widget);

    GraphRow m_graph_row;
};
