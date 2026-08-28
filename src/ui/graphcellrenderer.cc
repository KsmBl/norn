/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "graphcellrenderer.h"

#include <algorithm>
#include <cmath>

namespace
{
/*! Lane spacing as a fraction of the row height, so it scales with the font. */
constexpr double s_lane_pitch_ratio = 0.85;
/*! Dot radius as a fraction of the row height. */
constexpr double s_dot_radius_ratio = 0.17;
constexpr double s_line_width = 1.6;

/*!
 * Lane colours.
 *
 * Fixed rather than taken from the theme: these have to stay distinguishable from
 * each other, which a theme's foreground colours are not chosen to be, and they are
 * mid-tone enough to read against both a light and a dark background.
 */
struct Colour {
    double m_red;
    double m_green;
    double m_blue;
};

constexpr Colour s_lane_colours[] = {
    {0.20, 0.52, 0.89}, // blue
    {0.18, 0.64, 0.41}, // green
    {0.88, 0.35, 0.28}, // red
    {0.90, 0.62, 0.20}, // amber
    {0.60, 0.40, 0.80}, // violet
    {0.20, 0.68, 0.72}, // teal
    {0.85, 0.45, 0.65}, // pink
    {0.55, 0.55, 0.30}, // olive
};
}

GraphCellRenderer::GraphCellRenderer()
    : Glib::ObjectBase(typeid(GraphCellRenderer))
    , Gtk::CellRenderer()
{
}

int GraphCellRenderer::lane_pitch(Gtk::Widget &widget)
{
    const Pango::FontDescription font = widget.get_style_context()->get_font(Gtk::STATE_FLAG_NORMAL);
    const int size = font.get_size() / PANGO_SCALE;
    return std::max(10, static_cast<int>(size * 1.6 * s_lane_pitch_ratio));
}

void GraphCellRenderer::set_lane_colour(const Cairo::RefPtr<Cairo::Context> &context, int colour_index)
{
    const Colour &colour = s_lane_colours[static_cast<std::size_t>(colour_index) % std::size(s_lane_colours)];
    context->set_source_rgb(colour.m_red, colour.m_green, colour.m_blue);
}

void GraphCellRenderer::get_preferred_width_vfunc(Gtk::Widget &widget, int &minimum_width, int &natural_width) const
{
    const int pitch = lane_pitch(widget);

    natural_width = pitch * std::max(1, m_graph_row.m_lane_count) + pitch / 2;
    minimum_width = natural_width;
}

void GraphCellRenderer::render_vfunc(const Cairo::RefPtr<Cairo::Context> &context,
                                     Gtk::Widget &widget,
                                     const Gdk::Rectangle &,
                                     const Gdk::Rectangle &cell_area,
                                     Gtk::CellRendererState)
{
    const GraphRow &row = m_graph_row;
    const int pitch = lane_pitch(widget);

    const double top = cell_area.get_y();
    const double bottom = cell_area.get_y() + cell_area.get_height();
    const double middle = top + cell_area.get_height() / 2.0;
    const double radius = std::max(3.0, cell_area.get_height() * s_dot_radius_ratio);

    const auto lane_x = [&](int lane) {
        return cell_area.get_x() + pitch / 2.0 + lane * pitch;
    };

    context->save();
    context->set_line_width(s_line_width);
    context->set_line_cap(Cairo::LINE_CAP_ROUND);

    // Lanes running straight past this commit.
    for (const auto &lane : row.m_pass_through) {
        set_lane_colour(context, lane.second);
        const double x = lane_x(lane.first);
        context->move_to(x, top);
        context->line_to(x, bottom);
        context->stroke();
    }

    const double my_x = lane_x(row.m_lane);

    // This commit's own lane: a line up to it, and one down from it unless it is a
    // root, which is where history ends.
    set_lane_colour(context, row.m_color_index);
    context->move_to(my_x, top);
    context->line_to(my_x, middle);
    context->stroke();

    if (!row.m_is_root) {
        context->move_to(my_x, middle);
        context->line_to(my_x, bottom);
        context->stroke();
    }

    // Children converging into this commit from other lanes.
    for (const auto &edge : row.m_edges_in) {
        set_lane_colour(context, edge.second);
        const double from_x = lane_x(edge.first);
        context->move_to(from_x, top);
        context->curve_to(from_x, middle, my_x, top, my_x, middle);
        context->stroke();
    }

    // Merge parents branching away below.
    for (const auto &edge : row.m_edges_out) {
        set_lane_colour(context, edge.second);
        const double to_x = lane_x(edge.first);
        context->move_to(my_x, middle);
        context->curve_to(my_x, bottom, to_x, middle, to_x, bottom);
        context->stroke();
    }

    // The commit itself. A merge is drawn hollow so it is distinguishable at a
    // glance from an ordinary commit.
    set_lane_colour(context, row.m_color_index);
    context->arc(my_x, middle, radius, 0.0, 2.0 * M_PI);

    if (row.m_is_merge) {
        const Gdk::RGBA background = widget.get_style_context()->get_background_color(Gtk::STATE_FLAG_NORMAL);
        context->save();
        context->set_source_rgb(background.get_red(), background.get_green(), background.get_blue());
        context->fill_preserve();
        context->restore();
        context->stroke();
    } else {
        context->fill();
    }

    context->restore();
}
