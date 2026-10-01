#ifndef PYTHONPARSER_H
/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
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

#define PYTHONPARSER_H

#include <QMap>
#include <QSet>
#include <QVariant>
#include <QString>

class PythonParser
{
public:
    enum class SettingWriteMode {
        Unknown = 0,
        TopLevel,
        DictAssign
    };

    struct Result
    {
        bool                        ok = false;
        QMap<QString, QVariant>     settings;
        QString                     error;

        QString                     simPath;
        QString                     cellName;
        QString                     gdsFilename;
        QString                     xmlFilename;

        QMap<QString, QVariant>     topLevel;
        QMap<QString, QString>      settingTips;

        QString                     gdsSettingKey;
        QString                     xmlSettingKey;
        QString                     gdsLegacyVar;
        QString                     xmlLegacyVar;

        QHash<QString,
              SettingWriteMode>     writeMode;
        //! Keys whose value is a quoted string literal in the script (written back quoted);
        //! other text values are raw expressions (lists, references) written verbatim.
        QSet<QString>               quotedStrings;

        QString getCellName()    const { return cellName; }
        QString getGdsFilename() const { return gdsFilename; }
        QString getXmlFilename() const { return xmlFilename; }

        QString getSettingTip(const QString &key) const { return settingTips.value(key); }
        bool hasSettingTip(const QString &key) const { return settingTips.contains(key); }
    };

    /*!
     * \brief The \c cellname argument of the script's first \c read_gds(...) call.
     *
     * This is the cell the script actually simulates; other cell variables in the script
     * may be stale. Without the argument gds2palace / gds2openEMS load the GDS top cell.
     */
    struct ReadGdsCellRef
    {
        bool                        found = false;    //!< A read_gds( call exists
        QString                     variable;         //!< cellname=<variable>
        QString                     settingsKey;      //!< cellname=<dict>['<key>']
        bool                        hasLiteral = false;
        QString                     literal;          //!< cellname="<literal>"
        int                         literalStart = -1; //!< Offset of the quoted literal in the script
        int                         literalLength = 0; //!< Length incl. quotes
    };

    static ReadGdsCellRef readGdsCellRef(const QString &script);

    static Result parseSettings(const QString &filePath);
    static Result parseSettingsFromText(const QString &content,
                                        const QString &scriptDir = QString(),
                                        const QString &baseName  = QString());
};

#endif // PYTHONPARSER_H
