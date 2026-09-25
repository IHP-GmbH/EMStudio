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
 ************************************************************************/

#ifndef TOOLAUTODETECT_H
#define TOOLAUTODETECT_H

#include <QMap>
#include <QString>
#include <QVariant>

/*!*******************************************************************************************************************
 * \brief Host/PATH autodetection for External tools Preferences (About probes).
 *
 * Fills only empty keys so an intentional blank preference is not overwritten after the user clears it.
 * Safe to call on every startup; first launch benefits most.
 **********************************************************************************************************************/
namespace ToolAutoDetect {

/*! Fill empty tool preference keys from PATH and common install locations. */
int fillEmptyPreferences(QMap<QString, QVariant> &prefs);

QString findHostPython();
QString findKlayoutExe();
QString findElmerSolver();
QString findPalaceInstallRoot();
QString findOpenemsInstallRoot();
#ifdef Q_OS_WIN
QString findWslPython(const QString &distro);
#endif

} // namespace ToolAutoDetect

#endif // TOOLAUTODETECT_H
