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

#ifndef NAVIGATIONSTYLE_H
#define NAVIGATIONSTYLE_H

#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

/*!*******************************************************************************************************************
 * \brief Mouse navigation preset shared by the Layout preview and the 3D field viewer.
 *
 * - \c EMStudio: wheel / trackpad scroll orbits in 3D and pans in 2D, Ctrl+wheel zooms,
 *   right drag orbits.
 * - \c SetupEM: VTK trackball as in setupEM: wheel zooms, right drag zooms (dolly).
 *
 * Both: left drag orbits (3D) / pans (2D), middle, Shift+left or Alt+left pans.
 * Stored as preference \c VIEWER_NAV_STYLE ("emstudio" / "setupem").
 **********************************************************************************************************************/
enum class NavStyle
{
    EMStudio,
    SetupEM
};

/*! One line of the key binding reference (Setup → Key Bindings, viewer tooltips). */
struct BindingRow
{
    QString             viewer;  //!< "Global", "Layout", "Layout 3D", "Layout Field", "3D field viewer"
    QString             action;  //!< What the binding does
    QString             binding; //!< Key or mouse gesture, e.g. "Ctrl+wheel"
};

namespace NavigationStyle {

/*! Preference key holding the style id. */
QString                 preferenceKey();
/*! Style id stored in preferences and passed to field_viewer.py (\c --nav-style). */
QString                 id(NavStyle style);
/*! Parses a style id; unknown or empty ids give \c NavStyle::EMStudio. */
NavStyle                fromId(const QString &id);
/*! Reads \c VIEWER_NAV_STYLE from the preferences map (default EMStudio). */
NavStyle                fromPreferences(const QMap<QString, QVariant> &preferences);
/*! User-visible name ("EMStudio" / "setupEM"). */
QString                 displayName(NavStyle style);
/*! One-sentence summary shown under the style combo. */
QString                 description(NavStyle style);
/*! Full binding reference for \a style; mouse rows depend on the style, key rows don't. */
QVector<BindingRow>     bindingTable(NavStyle style);
/*! Compact multi-line help for the rows of one viewer (used in tooltips). */
QString                 tooltipFor(NavStyle style, const QString &viewer);

} // namespace NavigationStyle

#endif // NAVIGATIONSTYLE_H
