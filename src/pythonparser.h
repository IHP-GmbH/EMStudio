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
#include <QHash>
#include <QVector>
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
        //! Loose variable -> keyword of the workflow parameter it is passed to (only where the name
        //! differs and the binding is unambiguous, see bindWorkflowCalls). Display only.
        QHash<QString, QString>     keywordAlias;

        QString getCellName()    const { return cellName; }
        QString getGdsFilename() const { return gdsFilename; }
        QString getXmlFilename() const { return xmlFilename; }

        QString getSettingTip(const QString &key) const { return settingTips.value(key); }
        bool hasSettingTip(const QString &key) const { return settingTips.contains(key); }
    };

    /*! A call \c name(...) in the script; offsets are absolute. */
    struct CallSite
    {
        int                         start = -1;       //!< Offset of the (possibly dotted) name
        int                         argsStart = -1;   //!< Offset after '('
        int                         argsEnd = -1;     //!< Offset of the closing ')'
        QString                     args;             //!< Text between the parentheses
    };

    /*! One argument of a call. */
    struct CallArg
    {
        QString                     keyword;          //!< Keyword name; empty for a positional argument
        QString                     text;             //!< Value text, trimmed
        int                         start = -1;       //!< Offset of the value in the script
        int                         length = 0;
    };

    /*!
     * \brief What a call passes as one keyword argument.
     *
     * For \c read_gds(..., cellname=...) this is the cell the script actually simulates; without
     * the argument gds2palace / gds2openEMS load the GDS top cell.
     */
    struct CallArgRef
    {
        bool                        found = false;    //!< The call exists
        bool                        hasArgument = false; //!< It passes the keyword
        QString                     variable;         //!< keyword=<variable>
        QString                     settingsKey;      //!< keyword=<dict>['<key>']
        bool                        hasLiteral = false;
        bool                        isDictLiteral = false; //!< The literal is a {...} dict
        QString                     literal;          //!< Quoted literal without quotes, or the dict text
        int                         literalStart = -1; //!< Offset of the literal in the script
        int                         literalLength = 0; //!< Length incl. quotes / braces
    };
    using ReadGdsCellRef = CallArgRef;

    /*! One parameter of a workflow function (keywords/workflow_signatures.csv). */
    struct WorkflowParam
    {
        QString                     function;   //!< e.g. "setupSimulation"
        int                         index = -1; //!< Positional index
        QString                     param;      //!< Parameter name (keyword argument)
        QString                     keyword;    //!< Keyword-file name
    };
    /*! Signatures used by every parse (set once by MainWindow, or by tests). */
    static void                     setWorkflowSignatures(const QVector<WorkflowParam> &signatures);
    static QVector<WorkflowParam>   workflowSignatures();
    static QHash<QString, QString>  bindWorkflowCalls(const QString &script,
                                                      const QVector<WorkflowParam> &signatures);

    static QVector<CallSite>        findCalls(const QString &script, const QString &funcName);
    /*! Offset just past the end of the statement that starts at \a pos (past its newline):
     *  open brackets, strings and backslash continuations span lines. */
    static int                      statementEnd(const QString &script, int pos);
    static QVector<CallArg>         splitCallArgs(const QString &script, const CallSite &call);
    static CallArgRef               callArgumentRef(const QString &script, const QString &funcName,
                                                    const QString &keyword);
    static ReadGdsCellRef           readGdsCellRef(const QString &script);
    /*! GDS datatypes (purposes) that the model's read_gds call passes as \c purposelist. */
    struct GdsPurposes
    {
        bool                        known = false;    //!< The list was resolved to integers
        QSet<int>                   purposes;
        QString                     settingsKey;      //!< purposelist=<dict>['<key>']
        QString                     variable;         //!< purposelist=<variable>
    };
    static GdsPurposes              readGdsPurposes(const QString &script);
    /*! Integers of a flat list literal such as "[0, 2]"; false for anything else. */
    static bool                     parseIntList(const QString &text, QSet<int> *out);

    static Result parseSettings(const QString &filePath);
    static Result parseSettingsFromText(const QString &content,
                                        const QString &scriptDir = QString(),
                                        const QString &baseName  = QString());
};

#endif // PYTHONPARSER_H
