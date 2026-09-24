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

#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>
#include <QMap>
#include <QProcess>
#include <QString>
#include <QVariant>
#include <QVector>

class QLabel;

namespace Ui {
class AboutDialog;
}

/*!*******************************************************************************************************************
 * \class AboutDialog
 * \brief Modal About dialog with EMStudio metadata and async external-tool versions.
 *
 * Tool rows show “(loading…)” immediately; versions are probed in the background.
 * Where possible, installed versions are compared to upstream latest (PyPI / GitHub / endoflife.date).
 **********************************************************************************************************************/
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(const QMap<QString, QVariant> &preferences,
                         QWidget *parent = nullptr);
    ~AboutDialog() override;

private slots:
    void onProbeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onLatestFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    enum class ProbeKind {
        ExeVersion,
        PythonVersion,
        PythonModule
    };

    enum class LatestKind {
        None,
        Pypi,      ///< latestRef = PyPI package name
        Github,    ///< latestRef = "owner/repo"
        PythonEol  ///< CPython latest via endoflife.date
    };

    struct ToolRow {
        QString name;
        ProbeKind kind = ProbeKind::ExeVersion;
        QString program;
        QString module;
        bool viaWsl = false;
        bool skipProbe = false;
        LatestKind latestKind = LatestKind::None;
        QString latestRef;
        QString installedVersion;
        QLabel *valueLabel = nullptr;
    };

    void initUi();
    void buildToolRows();
    void addStaticRow(const QString &name, const QString &value, const QString &tip = {});
    void addProbeRow(const QString &name,
                     ProbeKind kind,
                     const QString &program,
                     const QString &module = {},
                     bool viaWsl = false,
                     LatestKind latestKind = LatestKind::None,
                     const QString &latestRef = {});
    void startProbes();
    void runNextProbe();
    void finishCurrent(const QString &text);
    void maybeStartLatestCheck(const QString &installed);
    void startLatestCheck();
    bool startExeAttempt();
    QStringList pythonPrefixArgs(const ToolRow &row) const;

    static QString extractVersion(const QString &toolName, const QByteArray &raw);
    static QString extractSemver(const QString &text);
    static QString extractGithubTag(const QByteArray &raw);
    static QString extractPythonEolLatest(const QByteArray &raw);
    static bool isJunkLine(const QString &line);
    static QString shorten(const QString &s, int maxLen = 64);
    static bool looksLinuxPath(const QString &path);
    /*! True only on Windows when \a path is a Linux/WSL path (probe via wsl.exe). */
    static bool probeViaWsl(const QString &path);
    static QString cleanExe(QString path);
    static QString resolveKlayoutExe(const QString &configured);
    static QString findCurl();
    QString pref(const QString &key) const;

    Ui::AboutDialog *m_ui = nullptr;
    QMap<QString, QVariant> m_preferences;
    QVector<ToolRow> m_tools;
    int m_probeIndex = -1;
    QProcess *m_proc = nullptr;
    QStringList m_versionFlags;
    bool m_awaitingLatest = false;

#ifdef EMSTUDIO_TESTING
public:
    /*! Deterministic viaWsl matrix for golden comparison (Windows vs Linux host). */
    static QString testProbeViaWslReport();
    /*! Snapshot of tool rows right after construction (before/while probes run). */
    QString testToolProbePlanReport() const;
    /*! Process events until all version probes (and optional latest checks) finish. */
    bool testWaitForProbesIdle(int timeoutMs = 45000);
    /*! name\\tvalue lines from the External tools form after probes settle. */
    QString testToolsStatusReport() const;
#endif
};

#endif // ABOUTDIALOG_H
