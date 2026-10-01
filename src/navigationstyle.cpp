/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/

#include "navigationstyle.h"

#include <QCoreApplication>
#include <QStringList>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("NavigationStyle", text);
}

} // namespace

namespace NavigationStyle {

/*!*******************************************************************************************************************
 * \brief Preference key that holds the navigation style id.
 * \return \c VIEWER_NAV_STYLE
 **********************************************************************************************************************/
QString preferenceKey()
{
    return QStringLiteral("VIEWER_NAV_STYLE");
}

/*!*******************************************************************************************************************
 * \brief Style id as stored in preferences and passed to field_viewer.py.
 * \param style Navigation style.
 * \return "emstudio" or "setupem".
 **********************************************************************************************************************/
QString id(NavStyle style)
{
    return style == NavStyle::SetupEM ? QStringLiteral("setupem") : QStringLiteral("emstudio");
}

/*!*******************************************************************************************************************
 * \brief Parses a style id (case-insensitive).
 * \param id "emstudio" or "setupem"; anything else gives EMStudio.
 * \return The style.
 **********************************************************************************************************************/
NavStyle fromId(const QString &id)
{
    return id.trimmed().compare(QLatin1String("setupem"), Qt::CaseInsensitive) == 0
            ? NavStyle::SetupEM : NavStyle::EMStudio;
}

/*!*******************************************************************************************************************
 * \brief Reads the navigation style from the application preferences.
 * \param preferences MainWindow preferences map.
 * \return The configured style, EMStudio by default.
 **********************************************************************************************************************/
NavStyle fromPreferences(const QMap<QString, QVariant> &preferences)
{
    return fromId(preferences.value(preferenceKey()).toString());
}

/*!*******************************************************************************************************************
 * \brief User-visible style name.
 * \param style Navigation style.
 * \return "EMStudio" or "setupEM".
 **********************************************************************************************************************/
QString displayName(NavStyle style)
{
    return style == NavStyle::SetupEM ? QStringLiteral("setupEM") : QStringLiteral("EMStudio");
}

/*!*******************************************************************************************************************
 * \brief One-sentence summary of a style.
 * \param style Navigation style.
 * \return Description text.
 **********************************************************************************************************************/
QString description(NavStyle style)
{
    if (style == NavStyle::SetupEM)
        return tr("VTK trackball, as in setupEM: the wheel zooms, left drag orbits (3D) or pans (2D), "
                  "right drag zooms.");
    return tr("Trackpad friendly: scrolling orbits (3D) or pans (2D), Ctrl+wheel zooms, "
              "left or right drag orbits.");
}

/*!*******************************************************************************************************************
 * \brief Complete binding reference for one style.
 *
 * This table is the single source for the Key Bindings dialog and the viewer tooltips;
 * keep it in sync with LayoutView and scripts/field_viewer.py.
 *
 * \param style Navigation style (changes the mouse rows only).
 * \return Rows grouped by viewer.
 **********************************************************************************************************************/
QVector<BindingRow> bindingTable(NavStyle style)
{
    const bool vtk = (style == NavStyle::SetupEM);
    const QString global = tr("Global");
    const QString l2d = tr("Layout 2D");
    const QString l3d = tr("Layout 3D");
    const QString layout = tr("Layout");
    const QString field = tr("Layout Field");
    const QString viewer = tr("3D field viewer");

    QVector<BindingRow> rows;

    rows << BindingRow{global, tr("Load Python model"), QStringLiteral("Ctrl+O")}
         << BindingRow{global, tr("Save"), QStringLiteral("Ctrl+S")}
         << BindingRow{global, tr("Save As"), QStringLiteral("Ctrl+Shift+S")}
         << BindingRow{global, tr("Run simulation"), QStringLiteral("F5")}
         << BindingRow{global, tr("Preferences"), QStringLiteral("Ctrl+,")}
         << BindingRow{global, tr("About EMStudio"), QStringLiteral("F1")}
         << BindingRow{global, tr("Switch tab (Main, Substrate, Python, Ports, Simulate, Results)"),
                       QStringLiteral("Ctrl+1 … Ctrl+6")}
         << BindingRow{global, tr("Quit"), QStringLiteral("Ctrl+Q")};

    // Layout 2D mouse
    rows << BindingRow{l2d, tr("Zoom under the cursor"),
                       vtk ? tr("Wheel · Ctrl+wheel · pinch") : tr("Mouse wheel · Ctrl+wheel · pinch")}
         << BindingRow{l2d, tr("Pan"),
                       vtk ? tr("Left drag · Middle drag · Shift/Alt+left drag")
                           : tr("Left drag · Middle drag · Shift/Alt+left drag · trackpad scroll")};
    if (vtk)
        rows << BindingRow{l2d, tr("Zoom (drag up = in)"), tr("Right drag")};
    rows << BindingRow{l2d, tr("Select layer (click again: next layer below)"), tr("Left click")};

    // Layout 3D mouse and keys
    rows << BindingRow{l3d, tr("Orbit"),
                       vtk ? tr("Left drag · Ctrl+left drag")
                           : tr("Left drag · Right drag · Ctrl+left drag · wheel / trackpad scroll")}
         << BindingRow{l3d, tr("Zoom under the cursor"), vtk ? tr("Wheel · Ctrl+wheel · pinch")
                                                             : tr("Ctrl+wheel · pinch")}
         << BindingRow{l3d, tr("Pan"), tr("Middle drag · Shift/Alt+left drag")};
    if (vtk)
        rows << BindingRow{l3d, tr("Zoom (drag up = in)"), tr("Right drag")};
    rows << BindingRow{l3d, tr("Select layer"), tr("Left click")}
         << BindingRow{l3d, tr("Reset camera"), QStringLiteral("R")}
         << BindingRow{l3d, tr("Isometric view"), QStringLiteral("I")}
         << BindingRow{l3d, tr("View from +X / +Y (Shift: −X / −Y)"), QStringLiteral("X · Y")}
         << BindingRow{l3d, tr("Top view (2D)"), QStringLiteral("Z")}
         << BindingRow{l3d, tr("View from below"), QStringLiteral("Shift+Z")};

    // Layout keys (2D and 3D)
    rows << BindingRow{layout, tr("Top view (2D) / 3D view"), QStringLiteral("2 · 3")}
         << BindingRow{layout, tr("Fit layout"), QStringLiteral("F")}
         << BindingRow{layout, tr("Fit everything (full Field domain)"), QStringLiteral("Home")}
         << BindingRow{layout, tr("Zoom in / out"), QStringLiteral("+ · −")}
         << BindingRow{layout, tr("Pan"), tr("Arrow keys")}
         << BindingRow{layout, tr("Measure ruler on/off (then click start and end)"), QStringLiteral("M")}
         << BindingRow{layout, tr("Measure point (any time)"), tr("Ctrl+Shift+left click")}
         << BindingRow{layout, tr("Clear selection, ruler and probe"), QStringLiteral("Esc")}
         << BindingRow{layout, tr("Copy view as image"), QStringLiteral("Ctrl+C")};

    // Layout Field keys
    rows << BindingRow{field, tr("Field on/off"), QStringLiteral("Shift+F")}
         << BindingRow{field, tr("Open the 3D field viewer"), QStringLiteral("3")}
         << BindingRow{field, tr("Move the Z slice up / down"), QStringLiteral("PgUp · PgDn")}
         << BindingRow{field, tr("Log scale on/off"), QStringLiteral("L")}
         << BindingRow{field, tr("Probe on/off"), QStringLiteral("P")}
         << BindingRow{field, tr("Probe the value"), tr("Left click (Probe on)")};

    // 3D field viewer (PyVista)
    rows << BindingRow{viewer, tr("Orbit"),
                       vtk ? tr("Left drag") : tr("Left drag · Right drag · Ctrl+left drag · wheel / trackpad scroll")};
    if (vtk)
        rows << BindingRow{viewer, tr("Roll around the view axis"), tr("Ctrl+left drag")};
    rows << BindingRow{viewer, tr("Zoom"), vtk ? tr("Wheel · Right drag") : tr("Ctrl+wheel")}
         << BindingRow{viewer, tr("Pan"), tr("Middle drag · Shift/Alt+left drag")}
         << BindingRow{viewer, tr("Copy view (menu)"), tr("Right click")}
         << BindingRow{viewer, tr("Reset camera"), QStringLiteral("R · F · Home")}
         << BindingRow{viewer, tr("Isometric view"), QStringLiteral("I")}
         << BindingRow{viewer, tr("View from +X / +Y / +Z (Shift: negative side)"), QStringLiteral("X · Y · Z")}
         << BindingRow{viewer, tr("Zoom in / out"), QStringLiteral("+ · −")}
         << BindingRow{viewer, tr("Pan"), tr("Arrow keys")}
         << BindingRow{viewer, tr("Parallel / perspective projection"), QStringLiteral("O")}
         << BindingRow{viewer, tr("Find max."), QStringLiteral("M")}
         << BindingRow{viewer, tr("Arrows on/off"), QStringLiteral("A")}
         << BindingRow{viewer, tr("Move the clip plane"), QStringLiteral("PgUp · PgDn")}
         << BindingRow{viewer, tr("Copy view as image"), QStringLiteral("Ctrl+C")};

    return rows;
}

/*!*******************************************************************************************************************
 * \brief Formats the rows of one viewer as "binding: action" lines.
 * \param style  Navigation style.
 * \param viewer Viewer column value, e.g. "Layout 2D".
 * \return Multi-line text, empty if the viewer has no rows.
 **********************************************************************************************************************/
QString tooltipFor(NavStyle style, const QString &viewer)
{
    QStringList lines;
    for (const BindingRow &row : bindingTable(style)) {
        if (row.viewer == viewer)
            lines << QStringLiteral("%1: %2").arg(row.binding, row.action);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace NavigationStyle
