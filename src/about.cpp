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

#include "about.h"
#include "ui_about.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

/*!*******************************************************************************************************************
 * \brief Constructs the About dialog and starts async tool-version probes.
 *
 * \param preferences Application Preferences map (tool paths, WSL distro, …).
 * \param parent      Optional parent widget.
 **********************************************************************************************************************/
AboutDialog::AboutDialog(const QMap<QString, QVariant> &preferences, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::AboutDialog)
    , m_preferences(preferences)
{
    m_ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    initUi();
    buildToolRows();

    resize(qMax(480, width()), sizeHint().height());

    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    QTimer::singleShot(0, this, &AboutDialog::startProbes);
}

/*!*******************************************************************************************************************
 * \brief Destroys the dialog and aborts any in-flight version probe.
 **********************************************************************************************************************/
AboutDialog::~AboutDialog()
{
    if (m_proc) {
        disconnect(m_proc, nullptr, this, nullptr);
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    delete m_ui;
}

/*!*******************************************************************************************************************
 * \brief Trims and strips surrounding quotes from an executable path.
 **********************************************************************************************************************/
QString AboutDialog::cleanExe(QString path)
{
    path = path.trimmed();
    if (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')) && path.size() >= 2)
        path = path.mid(1, path.size() - 2).trimmed();
    return path;
}

/*!*******************************************************************************************************************
 * \brief Reads a cleaned Preferences string value by key.
 **********************************************************************************************************************/
QString AboutDialog::pref(const QString &key) const
{
    return cleanExe(m_preferences.value(key).toString());
}

/*!*******************************************************************************************************************
 * \brief Returns whether \a path looks like a WSL / Linux filesystem path.
 **********************************************************************************************************************/
bool AboutDialog::looksLinuxPath(const QString &path)
{
    return path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('~'))
           || path.startsWith(QStringLiteral("\\\\wsl$"))
           || path.startsWith(QStringLiteral("\\\\wsl.localhost"));
}

/*!*******************************************************************************************************************
 * \brief Resolves a KLayout launcher (.bat/.cmd) to klayout_app.exe when possible.
 **********************************************************************************************************************/
QString AboutDialog::resolveKlayoutExe(const QString &configured)
{
    const QString path = cleanExe(configured);
    if (path.isEmpty())
        return {};

    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    const bool isLauncher = (suffix == QLatin1String("bat") || suffix == QLatin1String("cmd")
                             || suffix == QLatin1String("sh"));

    auto existsExe = [](const QString &p) -> bool {
        return !p.isEmpty() && QFileInfo::exists(p) && QFileInfo(p).isFile();
    };

    if (!isLauncher && existsExe(path))
        return path;

    const QStringList candidates = {
        QStringLiteral("klayout_app.exe"),
        QStringLiteral("klayout.exe"),
        QStringLiteral("klayout"),
    };

    if (isLauncher) {
        const QDir dir = fi.dir();
        for (const QString &name : candidates) {
            const QString cand = dir.filePath(name);
            if (existsExe(cand))
                return cand;
        }
        QDir up = dir;
        if (up.cdUp()) {
            for (const QString &sub :
                 {QStringLiteral("bin"), QStringLiteral("."), QStringLiteral("bin-release")}) {
                for (const QString &name : candidates) {
                    const QString cand = QDir(up.filePath(sub)).filePath(name);
                    if (existsExe(cand))
                        return cand;
                }
            }
        }

        // Same fallbacks as IHP start_klayout_sg13g2.bat
        QStringList known;
        const QString envExe = cleanExe(qEnvironmentVariable("KLAYOUT_EXE"));
        if (!envExe.isEmpty())
            known << envExe;
#ifdef Q_OS_WIN
        known << QDir::fromNativeSeparators(
            qEnvironmentVariable("APPDATA") + QStringLiteral("/KLayout/klayout_app.exe"));
        known << QDir::fromNativeSeparators(
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/KLayout/klayout_app.exe"));
        known << QDir::fromNativeSeparators(
            qEnvironmentVariable("ProgramFiles") + QStringLiteral("/KLayout/klayout_app.exe"));
        known << QDir::fromNativeSeparators(
            qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/KLayout/klayout_app.exe"));
#endif
        for (const QString &cand : known) {
            if (existsExe(cand))
                return cand;
        }
    }

    return path;
}

/*!*******************************************************************************************************************
 * \brief Fills EMStudio version / Qt / build labels and the logo.
 **********************************************************************************************************************/
void AboutDialog::initUi()
{
    m_ui->lblLogo->setPixmap(QPixmap(QStringLiteral(":/logo")));

    m_ui->lblVersion->setText(
        QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("dev")
            : QCoreApplication::applicationVersion());

    m_ui->lblQt->setText(QString::fromLatin1(qVersion()));

#ifdef QT_DEBUG
    const QString buildType = QStringLiteral("Debug");
#else
    const QString buildType = QStringLiteral("Release");
#endif

    m_ui->lblBuild->setText(
        QStringLiteral("%1 | %2").arg(buildType, QStringLiteral(EMSTUDIO_GIT_DATE_STR)));
}

/*!*******************************************************************************************************************
 * \brief Adds a non-probed tools row (e.g. not set / WSL distro name).
 **********************************************************************************************************************/
void AboutDialog::addStaticRow(const QString &name, const QString &value, const QString &tip)
{
    auto *nameLbl = new QLabel(name + QLatin1Char(':'), this);
    auto *valLbl = new QLabel(value, this);
    valLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valLbl->setWordWrap(true);
    valLbl->setStyleSheet(QStringLiteral("color: gray;"));
    if (!tip.isEmpty())
        valLbl->setToolTip(tip);

    const int row = m_ui->toolsForm->rowCount();
    m_ui->toolsForm->setWidget(row, QFormLayout::LabelRole, nameLbl);
    m_ui->toolsForm->setWidget(row, QFormLayout::FieldRole, valLbl);

    ToolRow t;
    t.name = name;
    t.skipProbe = true;
    t.valueLabel = valLbl;
    m_tools.append(t);
}

/*!*******************************************************************************************************************
 * \brief Adds a tools row that will be version-probed asynchronously.
 **********************************************************************************************************************/
void AboutDialog::addProbeRow(const QString &name,
                              ProbeKind kind,
                              const QString &program,
                              const QString &module,
                              bool viaWsl,
                              LatestKind latestKind,
                              const QString &latestRef)
{
    auto *nameLbl = new QLabel(name + QLatin1Char(':'), this);
    auto *valLbl = new QLabel(QStringLiteral("(loading…)"), this);
    valLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valLbl->setWordWrap(true);
    valLbl->setStyleSheet(QStringLiteral("color: gray;"));
    valLbl->setToolTip(program);

    const int row = m_ui->toolsForm->rowCount();
    m_ui->toolsForm->setWidget(row, QFormLayout::LabelRole, nameLbl);
    m_ui->toolsForm->setWidget(row, QFormLayout::FieldRole, valLbl);

    ToolRow t;
    t.name = name;
    t.kind = kind;
    t.program = program;
    t.module = module;
    t.viaWsl = viaWsl;
    t.latestKind = latestKind;
    t.latestRef = latestRef;
    t.valueLabel = valLbl;
    m_tools.append(t);
}

/*!*******************************************************************************************************************
 * \brief Builds the External tools list from Preferences paths.
 **********************************************************************************************************************/
void AboutDialog::buildToolRows()
{
    // KLayout
    {
        const QString configured = pref(QStringLiteral("KLAYOUT_EXE"));
        if (configured.isEmpty()) {
            addStaticRow(QStringLiteral("KLayout"), QStringLiteral("not set"));
        } else {
            const QString exe = resolveKlayoutExe(configured);
            addProbeRow(QStringLiteral("KLayout"),
                        ProbeKind::ExeVersion,
                        exe,
                        {},
                        looksLinuxPath(exe),
                        LatestKind::Github,
                        QStringLiteral("KLayout/klayout"));
            m_tools.last().valueLabel->setToolTip(
                exe == configured ? configured
                                 : tr("%1\n(resolved from %2)")
                                       .arg(QDir::toNativeSeparators(exe),
                                            QDir::toNativeSeparators(configured)));
        }
    }

    // Palace — prefer real binary under install path (not launcher echo)
    {
        const QString install = pref(QStringLiteral("PALACE_INSTALL_PATH"));
        const QString script = pref(QStringLiteral("PALACE_RUN_SCRIPT"));
        QString palaceExe;
        bool viaWsl = false;

        if (!install.isEmpty()) {
#ifdef Q_OS_WIN
            const QString winExe = QDir(install).filePath(QStringLiteral("bin/palace.exe"));
            const QString nixExe = QDir(install).filePath(QStringLiteral("bin/palace"));
            if (QFileInfo::exists(winExe)) {
                palaceExe = winExe;
            } else if (looksLinuxPath(install)) {
                palaceExe = install.endsWith(QLatin1Char('/'))
                                ? install + QStringLiteral("bin/palace")
                                : install + QStringLiteral("/bin/palace");
                viaWsl = true;
            } else if (QFileInfo::exists(nixExe)) {
                palaceExe = nixExe;
            } else {
                palaceExe = nixExe;
                viaWsl = looksLinuxPath(nixExe);
            }
#else
            palaceExe = QDir(install).filePath(QStringLiteral("bin/palace"));
#endif
        } else if (!script.isEmpty()) {
            palaceExe = script;
            viaWsl = looksLinuxPath(script);
        }

        if (palaceExe.isEmpty())
            addStaticRow(QStringLiteral("Palace"), QStringLiteral("not set"));
        else
            addProbeRow(QStringLiteral("Palace"),
                        ProbeKind::ExeVersion,
                        palaceExe,
                        {},
                        viaWsl,
                        LatestKind::Github,
                        QStringLiteral("awslabs/palace"));
    }

    // ElmerSolver + ElmerGrid
    {
        const QString solver = pref(QStringLiteral("ELMER_SOLVER_PATH"));
        if (solver.isEmpty()) {
            addStaticRow(QStringLiteral("ElmerSolver"), QStringLiteral("not set"));
            addStaticRow(QStringLiteral("ElmerGrid"), QStringLiteral("not set"));
        } else {
            addProbeRow(QStringLiteral("ElmerSolver"),
                        ProbeKind::ExeVersion,
                        solver,
                        {},
                        looksLinuxPath(solver),
                        LatestKind::Github,
                        QStringLiteral("ElmerCSC/elmerfem"));

            QFileInfo fi(solver);
#ifdef Q_OS_WIN
            QString grid = fi.dir().filePath(QStringLiteral("ElmerGrid.exe"));
            if (!QFileInfo::exists(grid))
                grid = fi.dir().filePath(QStringLiteral("ElmerGrid"));
#else
            const QString grid = fi.dir().filePath(QStringLiteral("ElmerGrid"));
#endif
            addProbeRow(QStringLiteral("ElmerGrid"),
                        ProbeKind::ExeVersion,
                        grid,
                        {},
                        looksLinuxPath(grid),
                        LatestKind::Github,
                        QStringLiteral("ElmerCSC/elmerfem"));
        }
    }

    auto addPython = [this](const QString &label, const QString &key) -> QString {
        const QString py = pref(key);
        if (py.isEmpty()) {
            addStaticRow(label, QStringLiteral("not set"));
            return {};
        }
        addProbeRow(label,
                    ProbeKind::PythonVersion,
                    py,
                    {},
                    looksLinuxPath(py),
                    LatestKind::PythonEol);
        return py;
    };

    const QString openemsPy = addPython(QStringLiteral("OpenEMS Python"),
                                          QStringLiteral("Python Path"));
    const QString palacePy = addPython(QStringLiteral("Palace Python"),
                                         QStringLiteral("PALACE_PYTHON"));
    const QString elmerPy = addPython(QStringLiteral("Elmer Python"),
                                        QStringLiteral("ELMER_PYTHON"));
    const QString fieldPy = addPython(QStringLiteral("Field Viewer Python"),
                                        QStringLiteral("FIELD_VIEWER_PYTHON"));

    QString gdsPy = !palacePy.isEmpty() ? palacePy
                    : !elmerPy.isEmpty()  ? elmerPy
                                          : openemsPy;
    if (gdsPy.isEmpty()) {
        addStaticRow(QStringLiteral("gds2palace"), QStringLiteral("no Python configured"));
    } else {
        addProbeRow(QStringLiteral("gds2palace"),
                    ProbeKind::PythonModule,
                    gdsPy,
                    QStringLiteral("gds2palace"),
                    looksLinuxPath(gdsPy),
                    LatestKind::Pypi,
                    QStringLiteral("gds2palace"));
        m_tools.last().valueLabel->setToolTip(
            tr("Probed via: %1").arg(QDir::toNativeSeparators(gdsPy)));
    }

    QString snpPy;
    for (const QString &cand : {openemsPy, elmerPy, fieldPy, palacePy}) {
        if (!cand.isEmpty() && !looksLinuxPath(cand)) {
            snpPy = cand;
            break;
        }
    }
    if (snpPy.isEmpty()) {
        for (const QString &cand : {openemsPy, elmerPy, fieldPy, palacePy}) {
            if (!cand.isEmpty()) {
                snpPy = cand;
                break;
            }
        }
    }
    if (snpPy.isEmpty()) {
        addStaticRow(QStringLiteral("snp2le"), QStringLiteral("no Python configured"));
    } else {
        addProbeRow(QStringLiteral("snp2le"),
                    ProbeKind::PythonModule,
                    snpPy,
                    QStringLiteral("snp2le"),
                    looksLinuxPath(snpPy),
                    LatestKind::Pypi,
                    QStringLiteral("snp2le"));
        m_tools.last().valueLabel->setToolTip(
            tr("Probed via: %1").arg(QDir::toNativeSeparators(snpPy)));
    }

#ifdef Q_OS_WIN
    const QString distro = pref(QStringLiteral("WSL_DISTRO"));
    addStaticRow(QStringLiteral("WSL distro"),
                 distro.isEmpty() ? QStringLiteral("default / not set") : distro);
#endif
}

/*!*******************************************************************************************************************
 * \brief Filters separator / env-dump lines from tool --version output.
 **********************************************************************************************************************/
bool AboutDialog::isJunkLine(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return true;
    // Separators / shell echoes / env dumps from launcher scripts
    if (t.contains(QRegularExpression(QStringLiteral("^[-=*#/>]{3,}$"))))
        return true;
    if (t.startsWith(QLatin1String(">>")))
        return true;
    if (t.contains(QLatin1String("PDK_ROOT"), Qt::CaseInsensitive))
        return true;
    if (t.contains(QLatin1String("KLAYOUT_PATH"), Qt::CaseInsensitive)
        && !t.contains(QLatin1String("version"), Qt::CaseInsensitive))
        return true;
    if (t.startsWith(QLatin1String("STARTED AT:"), Qt::CaseInsensitive))
        return true;
    if (t.contains(QStringLiteral("Close any already-running"), Qt::CaseInsensitive))
        return true;
    if (t.startsWith(QLatin1String("File -"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("Instance -"), Qt::CaseInsensitive))
        return true;
    if (t.startsWith(QLatin1String("TECH"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("ERROR:"), Qt::CaseInsensitive))
        return true;
    return false;
}

/*!*******************************************************************************************************************
 * \brief Extracts the first dotted version number from \a text.
 **********************************************************************************************************************/
QString AboutDialog::extractSemver(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"((?i)(?:version[:\s]*)?v?\s*(\d+\.\d+(?:\.\d+)?(?:\.\d+)?))"));
    const auto m = re.match(text);
    if (m.hasMatch())
        return m.captured(1).trimmed();
    return {};
}

/*!*******************************************************************************************************************
 * \brief Parses a clean version string from raw tool output for \a toolName.
 **********************************************************************************************************************/
QString AboutDialog::extractVersion(const QString &toolName, const QByteArray &raw)
{
    const QString s = QString::fromUtf8(raw);
    const QStringList lines =
        s.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);

    QStringList candidates;

    // Tool-specific patterns first (scan all non-junk lines)
    QRegularExpression toolRe;
    if (toolName.contains(QStringLiteral("KLayout"), Qt::CaseInsensitive)) {
        toolRe.setPattern(QStringLiteral(R"((?i)KLayout\s+(?:version\s+)?v?(\d+\.\d+(?:\.\d+)?))"));
    } else if (toolName.contains(QStringLiteral("Elmer"), Qt::CaseInsensitive)) {
        toolRe.setPattern(
            QStringLiteral(R"((?i)ELMER\s*(?:SOLVER|GRID)?[^\d]*v?\s*(\d+\.\d+(?:\.\d+)?))"));
    } else if (toolName.contains(QStringLiteral("Palace"), Qt::CaseInsensitive)) {
        toolRe.setPattern(QStringLiteral(R"((?i)palace\s+(?:version\s+)?v?(\d+\.\d+(?:\.\d+)?))"));
    } else if (toolName.contains(QStringLiteral("Python"), Qt::CaseInsensitive)) {
        toolRe.setPattern(QStringLiteral(R"((?i)Python\s+(\d+\.\d+\.\d+))"));
    }

    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (isJunkLine(t))
            continue;

        if (toolRe.isValid() && !toolRe.pattern().isEmpty()) {
            const auto tm = toolRe.match(t);
            if (tm.hasMatch())
                return tm.captured(1);
        }

        if (toolName.contains(QStringLiteral("Elmer"), Qt::CaseInsensitive)) {
            const auto em = QRegularExpression(
                                QStringLiteral(R"((?i)\(\s*v\s*(\d+\.\d+(?:\.\d+)?)\s*\))"))
                                .match(t);
            if (em.hasMatch())
                return em.captured(1);
        }

        const QString ver = extractSemver(t);
        if (!ver.isEmpty())
            candidates << ver;

        if (candidates.isEmpty() && t.size() < 80 && !t.contains(QLatin1Char('='))
            && !t.contains(QLatin1String("mpirun")))
            candidates << t;
    }

    if (!candidates.isEmpty()) {
        for (const QString &c : candidates) {
            if (extractSemver(c) == c || c.contains(QLatin1Char('.')))
                return extractSemver(c).isEmpty() ? c : extractSemver(c);
        }
        return candidates.first();
    }

    const QString any = extractSemver(s);
    if (!any.isEmpty())
        return any;

    return {};
}

/*!*******************************************************************************************************************
 * \brief Truncates a label string with an ellipsis when too long.
 **********************************************************************************************************************/
QString AboutDialog::shorten(const QString &s, int maxLen)
{
    if (s.size() <= maxLen)
        return s;
    return s.left(maxLen - 1) + QChar(0x2026);
}

/*!*******************************************************************************************************************
 * \brief Begins the sequential async version-probe queue.
 **********************************************************************************************************************/
void AboutDialog::startProbes()
{
    m_probeIndex = -1;
    m_awaitingLatest = false;
    runNextProbe();
}

/*!*******************************************************************************************************************
 * \brief Builds argv prefix for a host or WSL Python invocation.
 **********************************************************************************************************************/
QStringList AboutDialog::pythonPrefixArgs(const ToolRow &row) const
{
    QStringList args;
    if (row.viaWsl) {
        const QString distro = pref(QStringLiteral("WSL_DISTRO"));
        if (!distro.isEmpty())
            args << QStringLiteral("-d") << distro;
        args << QStringLiteral("--") << row.program;
    } else {
        const QString base = QFileInfo(row.program).fileName();
        if (base.compare(QLatin1String("py"), Qt::CaseInsensitive) == 0
            || base.compare(QLatin1String("py.exe"), Qt::CaseInsensitive) == 0)
            args << QStringLiteral("-3");
    }
    return args;
}

/*!*******************************************************************************************************************
 * \brief Starts the next pending tool version probe.
 **********************************************************************************************************************/
void AboutDialog::runNextProbe()
{
    ++m_probeIndex;
    while (m_probeIndex < m_tools.size() && m_tools.at(m_probeIndex).skipProbe)
        ++m_probeIndex;

    if (m_probeIndex >= m_tools.size())
        return;

    const ToolRow &row = m_tools.at(m_probeIndex);
    m_awaitingLatest = false;

    if (m_proc) {
        disconnect(m_proc, nullptr, this, nullptr);
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &AboutDialog::onProbeFinished);

    QString program;
    QStringList args;

    if (row.kind == ProbeKind::PythonVersion || row.kind == ProbeKind::PythonModule) {
        if (row.viaWsl)
            program = QStringLiteral("wsl.exe");
        else
            program = row.program;
        args = pythonPrefixArgs(row);

        if (row.kind == ProbeKind::PythonVersion) {
            args << QStringLiteral("--version");
        } else {
            const QString code = QStringLiteral(
                                     "import importlib; m=importlib.import_module('%1'); "
                                     "print(getattr(m,'__version__', "
                                     "getattr(m,'version','installed')))")
                                     .arg(row.module);
            args << QStringLiteral("-c") << code;
        }
        m_versionFlags.clear();
        m_proc->start(program, args);
    } else {
        if (row.name.contains(QStringLiteral("KLayout"), Qt::CaseInsensitive)) {
            m_versionFlags = QStringList{QStringLiteral("-v"),
                                         QStringLiteral("--version"),
                                         QStringLiteral("-version")};
        } else if (row.name.contains(QStringLiteral("Elmer"), Qt::CaseInsensitive)) {
            m_versionFlags = QStringList{QStringLiteral("--version"),
                                         QStringLiteral("-v"),
                                         QStringLiteral("-h"),
                                         QStringLiteral("--help")};
        } else if (row.name.contains(QStringLiteral("Palace"), Qt::CaseInsensitive)) {
            m_versionFlags = QStringList{QStringLiteral("--version"),
                                         QStringLiteral("-version"),
                                         QStringLiteral("-v")};
        } else {
            m_versionFlags = QStringList{QStringLiteral("--version"),
                                         QStringLiteral("-v"),
                                         QStringLiteral("-V"),
                                         QStringLiteral("-version")};
        }
        if (!startExeAttempt())
            finishCurrent(QStringLiteral("not found"));
        return;
    }

    if (!m_proc->waitForStarted(4000))
        finishCurrent(QStringLiteral("not found"));
}

/*!*******************************************************************************************************************
 * \brief Tries the next --version / -v flag for an executable probe.
 **********************************************************************************************************************/
bool AboutDialog::startExeAttempt()
{
    if (m_probeIndex < 0 || m_probeIndex >= m_tools.size() || m_versionFlags.isEmpty())
        return false;

    const ToolRow &row = m_tools.at(m_probeIndex);
    const QString flag = m_versionFlags.takeFirst();

    if (m_proc) {
        disconnect(m_proc, nullptr, this, nullptr);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &AboutDialog::onProbeFinished);

    QString program;
    QStringList args;
    if (row.viaWsl) {
        program = QStringLiteral("wsl.exe");
        const QString distro = pref(QStringLiteral("WSL_DISTRO"));
        if (!distro.isEmpty())
            args << QStringLiteral("-d") << distro;
        args << QStringLiteral("--") << row.program << flag;
    } else {
        program = row.program;
        args << flag;
    }

    m_proc->start(program, args);
    return m_proc->waitForStarted(4000);
}

/*!*******************************************************************************************************************
 * \brief Handles completion of a version probe process.
 **********************************************************************************************************************/
void AboutDialog::onProbeFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess *proc = qobject_cast<QProcess *>(sender());
    const QByteArray raw = proc ? proc->readAll() : QByteArray();
    if (proc && proc == m_proc)
        m_proc = nullptr;
    if (proc)
        proc->deleteLater();

    if (m_probeIndex < 0 || m_probeIndex >= m_tools.size())
        return;

    ToolRow &row = m_tools[m_probeIndex];
    const QString parsed = extractVersion(row.name, raw);
    const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);

    if (!parsed.isEmpty()
        && (ok || row.kind == ProbeKind::ExeVersion || row.kind == ProbeKind::PythonVersion
            || row.kind == ProbeKind::PythonModule)) {
        if (!isJunkLine(parsed) && parsed != QLatin1String("=")
            && !parsed.startsWith(QLatin1String(">>"))
            && !parsed.contains(QStringLiteral("Close any"), Qt::CaseInsensitive)) {
            maybeStartLatestCheck(parsed);
            return;
        }
    }

    if (row.kind == ProbeKind::ExeVersion && !m_versionFlags.isEmpty()) {
        if (startExeAttempt())
            return;
    }

    if (row.kind == ProbeKind::PythonModule)
        finishCurrent(QStringLiteral("not installed"));
    else if (!parsed.isEmpty() && !isJunkLine(parsed)
             && !parsed.contains(QStringLiteral("Close any"), Qt::CaseInsensitive))
        finishCurrent(parsed);
    else
        finishCurrent(QStringLiteral("not found"));
}

/*!*******************************************************************************************************************
 * \brief Shows installed version and optionally starts an upstream latest lookup.
 **********************************************************************************************************************/
void AboutDialog::maybeStartLatestCheck(const QString &installed)
{
    if (m_probeIndex < 0 || m_probeIndex >= m_tools.size())
        return;

    ToolRow &row = m_tools[m_probeIndex];
    row.installedVersion = installed;

    if (row.latestKind == LatestKind::None) {
        finishCurrent(installed);
        return;
    }

    if (row.valueLabel) {
        row.valueLabel->setText(installed + QStringLiteral(" (latest…)"));
        row.valueLabel->setStyleSheet(QString());
    }
    startLatestCheck();
}

/*!*******************************************************************************************************************
 * \brief Locates the system curl executable for HTTPS latest lookups.
 **********************************************************************************************************************/
QString AboutDialog::findCurl()
{
    const QString curl = QStandardPaths::findExecutable(QStringLiteral("curl"));
    if (!curl.isEmpty())
        return curl;
#ifdef Q_OS_WIN
    const QString sys = QStringLiteral("C:/Windows/System32/curl.exe");
    if (QFileInfo::exists(sys))
        return sys;
#endif
    return {};
}

/*!*******************************************************************************************************************
 * \brief Parses GitHub releases/latest JSON for tag_name.
 **********************************************************************************************************************/
QString AboutDialog::extractGithubTag(const QByteArray &raw)
{
    const auto m =
        QRegularExpression(QStringLiteral(R"re("tag_name"\s*:\s*"([^"]+)")re"))
            .match(QString::fromUtf8(raw));
    if (!m.hasMatch())
        return {};
    QString tag = m.captured(1).trimmed();
    if (tag.startsWith(QLatin1Char('v')) || tag.startsWith(QLatin1Char('V')))
        tag = tag.mid(1);
    const QString sem = extractSemver(tag);
    return sem.isEmpty() ? tag : sem;
}

/*!*******************************************************************************************************************
 * \brief Picks the newest CPython "latest" field from endoflife.date JSON.
 **********************************************************************************************************************/
QString AboutDialog::extractPythonEolLatest(const QByteArray &raw)
{
    QString chosen;
    int bestKey = -1;
    auto it = QRegularExpression(QStringLiteral(R"re("latest"\s*:\s*"(\d+)\.(\d+)\.(\d+)")re"))
                  .globalMatch(QString::fromUtf8(raw));
    while (it.hasNext()) {
        const auto m = it.next();
        const int key =
            m.captured(1).toInt() * 10000 + m.captured(2).toInt() * 100 + m.captured(3).toInt();
        if (key > bestKey) {
            bestKey = key;
            chosen = m.captured(1) + QLatin1Char('.') + m.captured(2) + QLatin1Char('.')
                     + m.captured(3);
        }
    }
    return chosen;
}

/*!*******************************************************************************************************************
 * \brief Queries PyPI / GitHub / endoflife.date for the latest upstream version.
 **********************************************************************************************************************/
void AboutDialog::startLatestCheck()
{
    if (m_probeIndex < 0 || m_probeIndex >= m_tools.size())
        return;

    const ToolRow &row = m_tools.at(m_probeIndex);
    m_awaitingLatest = true;

    if (m_proc) {
        disconnect(m_proc, nullptr, this, nullptr);
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    if (row.latestKind == LatestKind::Pypi) {
        QString mod = row.latestRef.isEmpty() ? row.module : row.latestRef;
        mod.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QString());

        const QString script =
            QStringLiteral(
                "import sys\n"
                "mod='%1'\n"
                "latest=''\n"
                "try:\n"
                " import json,urllib.request\n"
                " url='https://pypi.org/pypi/'+mod+'/json'\n"
                " with urllib.request.urlopen(url, timeout=6) as r:\n"
                "  latest=json.load(r).get('info',{}).get('version','') or ''\n"
                "except Exception:\n"
                " latest=''\n"
                "print(latest)\n")
                .arg(mod);

        QString program;
        QStringList args;
        if (row.viaWsl) {
            program = QStringLiteral("wsl.exe");
            args = pythonPrefixArgs(row);
            args << QStringLiteral("-c") << script;
        } else {
            program = row.program;
            args = pythonPrefixArgs(row);
            args << QStringLiteral("-c") << script;
        }

        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                &AboutDialog::onLatestFinished);
        m_proc->start(program, args);
        if (!m_proc->waitForStarted(4000)) {
            m_awaitingLatest = false;
            finishCurrent(row.installedVersion);
        }
        return;
    }

    const QString curl = findCurl();
    if (curl.isEmpty()) {
        m_awaitingLatest = false;
        finishCurrent(row.installedVersion);
        return;
    }

    QString url;
    if (row.latestKind == LatestKind::Github) {
        url = QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(row.latestRef);
    } else if (row.latestKind == LatestKind::PythonEol) {
        url = QStringLiteral("https://endoflife.date/api/python.json");
    } else {
        m_awaitingLatest = false;
        finishCurrent(row.installedVersion);
        return;
    }

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &AboutDialog::onLatestFinished);
    m_proc->start(curl,
                  {QStringLiteral("-sL"),
                   QStringLiteral("--max-time"),
                   QStringLiteral("8"),
                   QStringLiteral("-H"),
                   QStringLiteral("Accept: application/vnd.github+json"),
                   QStringLiteral("-H"),
                   QStringLiteral("User-Agent: EMStudio-About"),
                   url});
    if (!m_proc->waitForStarted(4000)) {
        m_awaitingLatest = false;
        finishCurrent(row.installedVersion);
    }
}

/*!*******************************************************************************************************************
 * \brief Applies installed vs latest package version to the tools label.
 **********************************************************************************************************************/
void AboutDialog::onLatestFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess *proc = qobject_cast<QProcess *>(sender());
    const QByteArray raw = proc ? proc->readAll() : QByteArray();
    if (proc && proc == m_proc)
        m_proc = nullptr;
    if (proc)
        proc->deleteLater();

    m_awaitingLatest = false;

    if (m_probeIndex < 0 || m_probeIndex >= m_tools.size())
        return;

    const ToolRow &row = m_tools.at(m_probeIndex);
    const QString installed = row.installedVersion;
    QString latest;

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        if (row.latestKind == LatestKind::Github)
            latest = extractGithubTag(raw);
        else if (row.latestKind == LatestKind::PythonEol)
            latest = extractPythonEolLatest(raw);
        else {
            latest = QString::fromUtf8(raw).trimmed();
            const int nl = latest.indexOf(QLatin1Char('\n'));
            if (nl >= 0)
                latest = latest.left(nl).trimmed();
            const QString sem = extractSemver(latest);
            if (!sem.isEmpty())
                latest = sem;
        }
    }

    QString text = installed;
    if (!latest.isEmpty()) {
        if (latest == installed)
            text = installed + QStringLiteral(" (up to date)");
        else
            text = installed + QStringLiteral(" (latest %1)").arg(latest);
    }
    finishCurrent(text);
}

/*!*******************************************************************************************************************
 * \brief Writes the probe result to the current row and continues the queue.
 **********************************************************************************************************************/
void AboutDialog::finishCurrent(const QString &text)
{
    if (m_probeIndex >= 0 && m_probeIndex < m_tools.size() && m_tools[m_probeIndex].valueLabel) {
        m_tools[m_probeIndex].valueLabel->setText(shorten(text, 72));
        m_tools[m_probeIndex].valueLabel->setStyleSheet(QString());
        if (!m_tools[m_probeIndex].program.isEmpty()
            && m_tools[m_probeIndex].valueLabel->toolTip().isEmpty())
            m_tools[m_probeIndex].valueLabel->setToolTip(m_tools[m_probeIndex].program);
    }
    m_versionFlags.clear();
    m_awaitingLatest = false;
    QTimer::singleShot(0, this, &AboutDialog::runNextProbe);
}
