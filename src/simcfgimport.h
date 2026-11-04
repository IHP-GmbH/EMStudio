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

#ifndef SIMCFGIMPORT_H
#define SIMCFGIMPORT_H

#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

/*!*******************************************************************************************************************
 * \brief Reads a setupEM settings file (*.simcfg), as written by setupEM and by the KLayout plug-in
 *        klayout_em_export, into what a gds2palace model script needs. No GUI here.
 *
 * A .simcfg is JSON: \c application = "setupEM", \c saved_values (GdsFile, SubstrateFile, cellname and the
 * gds2palace settings) and \c ports (the arguments of simulation_port(...)). setupEM keeps frequencies in GHz;
 * the model script uses Hz, so fstart/fstop/fstep/fpoint/fdump are converted, as setupEM does when it writes a
 * script.
 **********************************************************************************************************************/
namespace SimcfgImport {

/*! One port of the .simcfg: the arguments of simulation_port(...). */
struct Port
{
    int                             portnumber = 0;
    int                             sourceLayer = 0;    //!< GDS layer of the port shape
    double                          voltage = 1.0;
    double                          z0 = 50.0;
    QString                         fromLayer;          //!< via port (direction z)
    QString                         toLayer;            //!< via port (direction z)
    QString                         targetLayer;        //!< in-plane port (x, y)
    QString                         direction;          //!< as written, e.g. Z, -Z, X
};

/*! Content of a .simcfg. */
struct Data
{
    bool                            ok = false;
    QString                         error;              //!< why ok is false
    QString                         gdsFile;
    QString                         substrateFile;
    QString                         cellname;
    bool                            elmerMode = false;  //!< saved by setupEM in Elmer mode
    QVector<QPair<QString, QString>> settings;          //!< key -> Python literal (all of them: a Palace model)
    QJsonObject                     values;             //!< saved_values as read, for settingsFor()
    QVector<Port>                   ports;
};

Data                                read(const QString &path);
QVector<QPair<QString, QString>>    settingsFor(const Data &data, const QString &simKey, QStringList *skipped = nullptr);
QString                             pythonLiteral(const QString &key, const QJsonValue &value);

/*! The .simcfg keys that are not model settings: files and cell, and setupEM's own GUI state. */
bool                                isSkippedKey(const QString &key);

} // namespace SimcfgImport

#endif // SIMCFGIMPORT_H
