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

#include "assistantmcp.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

/*!*******************************************************************************************************************
 * \brief Constructs an empty MCP tool registry.
 *
 * \param parent Optional QObject parent.
 **********************************************************************************************************************/
AssistantMcp::AssistantMcp(QObject *parent)
    : QObject(parent)
{
}

/*!*******************************************************************************************************************
 * \brief Registers a tool in the in-process MCP registry.
 *
 * \param name         Unique tool name.
 * \param description  Human-readable description.
 * \param inputSchema  JSON Schema for arguments.
 * \param handler      Callback returning a JSON result object.
 **********************************************************************************************************************/
void AssistantMcp::registerTool(const QString &name,
                                const QString &description,
                                const QJsonObject &inputSchema,
                                ToolHandler handler)
{
    Tool t;
    t.name = name;
    t.description = description;
    t.inputSchema = inputSchema;
    t.handler = std::move(handler);
    m_tools.append(t);
}

/*!*******************************************************************************************************************
 * \brief Builds a JSON manifest of registered tools for clients and LLM agents.
 *
 * \return Array of objects with \c name, \c description, and \c inputSchema.
 **********************************************************************************************************************/
QJsonArray AssistantMcp::toolsManifest() const
{
    QJsonArray arr;
    for (const Tool &t : m_tools) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), t.name);
        o.insert(QStringLiteral("description"), t.description);
        o.insert(QStringLiteral("inputSchema"), t.inputSchema);
        arr.append(o);
    }
    return arr;
}

/*!*******************************************************************************************************************
 * \brief Checks whether a tool is registered.
 *
 * \param name Tool name.
 * \return \c true if found.
 **********************************************************************************************************************/
bool AssistantMcp::hasTool(const QString &name) const
{
    for (const Tool &t : m_tools) {
        if (t.name == name)
            return true;
    }
    return false;
}

/*!*******************************************************************************************************************
 * \brief Invokes a registered tool by name.
 *
 * \param name Tool name.
 * \param args Argument object (may be empty).
 * \return Handler result or an error object.
 **********************************************************************************************************************/
QJsonObject AssistantMcp::callTool(const QString &name, const QJsonObject &args) const
{
    for (const Tool &t : m_tools) {
        if (t.name != name)
            continue;
        if (!t.handler)
            return errResult(QStringLiteral("Tool has no handler: %1").arg(name));
        return t.handler(args);
    }
    return errResult(QStringLiteral("Unknown tool: %1").arg(name));
}

/*!*******************************************************************************************************************
 * \brief Routes a chat utterance through the heuristic keyword engine.
 *
 * \param userText Free-form user message.
 * \return Formatted reply string.
 **********************************************************************************************************************/
QString AssistantMcp::handleUserMessage(const QString &userText) const
{
    return routeAndExecute(userText);
}

/*!*******************************************************************************************************************
 * \brief Copies \a data and forces \c ok = true.
 **********************************************************************************************************************/
QJsonObject AssistantMcp::okResult(const QJsonObject &data)
{
    QJsonObject o = data;
    o.insert(QStringLiteral("ok"), true);
    return o;
}

/*!*******************************************************************************************************************
 * \brief Builds a standard error result object.
 *
 * \param message Error text.
 **********************************************************************************************************************/
QJsonObject AssistantMcp::errResult(const QString &message)
{
    QJsonObject o;
    o.insert(QStringLiteral("ok"), false);
    o.insert(QStringLiteral("error"), message);
    return o;
}

/*!*******************************************************************************************************************
 * \brief Pretty-prints a JSON value for chat bubbles.
 **********************************************************************************************************************/
QString AssistantMcp::formatValue(const QJsonValue &v, int indent)
{
    const QString pad = QString(indent, QLatin1Char(' '));
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        return QString::number(v.toDouble());
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Array: {
        const QJsonArray a = v.toArray();
        if (a.isEmpty())
            return QStringLiteral("[]");
        QStringList lines;
        lines << QStringLiteral("[");
        for (const QJsonValue &item : a)
            lines << pad + QStringLiteral("  - ") + formatValue(item, indent + 2);
        lines << pad + QStringLiteral("]");
        return lines.join(QLatin1Char('\n'));
    }
    case QJsonValue::Object:
        return formatObjectLines(v.toObject(), indent);
    default:
        return QStringLiteral("null");
    }
}

/*!*******************************************************************************************************************
 * \brief Pretty-prints a JSON object (skipping the \c ok field).
 **********************************************************************************************************************/
QString AssistantMcp::formatObjectLines(const QJsonObject &obj, int indent)
{
    const QString pad = QString(indent, QLatin1Char(' '));
    QStringList lines;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.key() == QLatin1String("ok"))
            continue;
        if (it.key() == QLatin1String("preview") && it.value().isString()) {
            const QString preview = it.value().toString();
            lines << pad + QStringLiteral("preview:");
            const QStringList plines = preview.split(QLatin1Char('\n'));
            const int maxLines = qMin(24, plines.size());
            for (int i = 0; i < maxLines; ++i)
                lines << pad + QStringLiteral("  | ") + plines.at(i);
            if (plines.size() > maxLines)
                lines << pad + QStringLiteral("  | …");
            continue;
        }
        const QString val = formatValue(it.value(), indent + 2);
        if (val.contains(QLatin1Char('\n'))) {
            lines << pad + it.key() + QLatin1Char(':');
            for (const QString &line : val.split(QLatin1Char('\n')))
                lines << pad + QStringLiteral("  ") + line;
        } else {
            lines << pad + it.key() + QStringLiteral(": ") + val;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

/*!*******************************************************************************************************************
 * \brief Human-readable list of registered MCP tools.
 **********************************************************************************************************************/
QString AssistantMcp::formatListToolsReply() const
{
    QStringList lines;
    lines << QStringLiteral("Available MCP tools:");
    lines << QString();
    for (const Tool &t : m_tools)
        lines << QStringLiteral("• %1 — %2").arg(t.name, t.description);
    lines << QString();
    lines << QStringLiteral("Examples:");
    lines << QStringLiteral("  prefs · state · list tools");
    lines << QStringLiteral("  gds: …/cmim/cmim_2u3_flat.gds");
    lines << QStringLiteral("  xml: …/cmim/SG13G2_200um.xml");
    lines << QStringLiteral("  generate model");
    lines << QStringLiteral("  import ports   |   add ports");
    lines << QStringLiteral("  run");
    return lines.join(QLatin1Char('\n'));
}

/*!*******************************************************************************************************************
 * \brief Formats a single tool call result for the chat UI.
 *
 * \param toolName Tool that was invoked.
 * \param result   JSON result from the handler.
 **********************************************************************************************************************/
QString AssistantMcp::formatToolReply(const QString &toolName, const QJsonObject &result) const
{
    if (toolName == QLatin1String("list_tools"))
        return formatListToolsReply();

    const bool ok = result.value(QStringLiteral("ok")).toBool(false);
    QStringList lines;
    lines << (ok ? QStringLiteral("%1 — ok").arg(toolName)
                 : QStringLiteral("%1 — failed").arg(toolName));

    if (!ok) {
        const QString err = result.value(QStringLiteral("error")).toString();
        if (!err.isEmpty())
            lines << QStringLiteral("error: %1").arg(err);
        return lines.join(QLatin1Char('\n'));
    }

    const QString body = formatObjectLines(result);
    if (!body.isEmpty()) {
        lines << QString();
        lines << body;
    }
    return lines.join(QLatin1Char('\n'));
}

/*!*******************************************************************************************************************
 * \brief Resolves GDS / XML / model paths from free text (including truncated folders).
 *
 * \param text    User text containing paths.
 * \param gds     Out: GDS path (optional).
 * \param xml     Out: substrate XML path (optional).
 * \param cell    Out: top-cell hint (optional).
 * \param modelPy Out: Python model path (optional).
 **********************************************************************************************************************/
void AssistantMcp::extractLayoutPaths(const QString &text,
                                      QString *gds,
                                      QString *xml,
                                      QString *cell,
                                      QString *modelPy)
{
    if (gds)
        gds->clear();
    if (xml)
        xml->clear();
    if (cell)
        cell->clear();
    if (modelPy)
        modelPy->clear();

    // Heal accidental line breaks after drive letters: "C:\n\Users\..." → "C:\Users\..."
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral("([A-Za-z]:)\\s*[\\r\\n]+\\s*([\\\\/])")),
                       QStringLiteral("\\1\\2"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    auto cleanPath = [](QString p) {
        p = p.trimmed();
        while (p.startsWith(QLatin1Char('"')) || p.startsWith(QLatin1Char('\'')))
            p = p.mid(1);
        while (p.endsWith(QLatin1Char('"')) || p.endsWith(QLatin1Char('\''))
               || p.endsWith(QLatin1Char(',')) || p.endsWith(QLatin1Char(';')))
            p.chop(1);
        p = p.trimmed();
        p.replace(QLatin1Char('/'), QLatin1Char('\\'));
        return QDir::cleanPath(p);
    };

    // If path missing, uniquely expand a truncated folder name: …/palace/induc → …/inductor_500pH
    auto resolvePath = [&](QString p) -> QString {
        p = cleanPath(p);
        if (p.isEmpty() || QFileInfo::exists(p))
            return p;

        const QFileInfo fi(p);
        const QDir parent = fi.dir();
        const QString prefix = fi.fileName();
        if (!parent.exists() || prefix.isEmpty())
            return p;

        QStringList matches;
        const QStringList entries =
            parent.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : entries) {
            if (name.startsWith(prefix, Qt::CaseInsensitive))
                matches << parent.filePath(name);
        }
        if (matches.size() == 1)
            return matches.first();
        return p;
    };

    auto pickFromDir = [&](const QString &dirPath) {
        const QDir d(dirPath);
        if (gds && gds->isEmpty()) {
            const QStringList gdsFiles =
                d.entryList(QStringList() << QStringLiteral("*.gds") << QStringLiteral("*.gdsii"),
                            QDir::Files, QDir::Name);
            if (!gdsFiles.isEmpty()) {
                QString pick = d.filePath(gdsFiles.first());
                for (const QString &f : gdsFiles) {
                    if (f.contains(QStringLiteral("flat"), Qt::CaseInsensitive)
                        || f.contains(QStringLiteral("with_ports"), Qt::CaseInsensitive)) {
                        pick = d.filePath(f);
                        break;
                    }
                }
                *gds = pick;
            }
        }
        if (xml && xml->isEmpty()) {
            const QStringList xmlFiles =
                d.entryList(QStringList() << QStringLiteral("*.xml"), QDir::Files, QDir::Name);
            QString pick;
            for (const QString &f : xmlFiles) {
                if (f.contains(QStringLiteral("SG13"), Qt::CaseInsensitive)
                    && !f.contains(QStringLiteral("openems"), Qt::CaseInsensitive)) {
                    pick = d.filePath(f);
                    break;
                }
            }
            if (pick.isEmpty() && !xmlFiles.isEmpty())
                pick = d.filePath(xmlFiles.first());
            if (!pick.isEmpty())
                *xml = pick;
        }
        if (modelPy && modelPy->isEmpty()) {
            const QStringList pyFiles =
                d.entryList(QStringList() << QStringLiteral("*.py"), QDir::Files, QDir::Name);
            if (!pyFiles.isEmpty()) {
                QString pick = d.filePath(pyFiles.first());
                for (const QString &f : pyFiles) {
                    if (f.contains(QStringLiteral("2port"), Qt::CaseInsensitive)
                        || f.contains(QStringLiteral("with_ports"), Qt::CaseInsensitive)
                        || f.startsWith(QStringLiteral("palace_"), Qt::CaseInsensitive)) {
                        pick = d.filePath(f);
                        break;
                    }
                }
                // Prefer a single obvious model file if only one .py
                if (pyFiles.size() == 1)
                    pick = d.filePath(pyFiles.first());
                *modelPy = pick;
            }
        }
    };

    auto assignPath = [&](const QString &raw) {
        const QString p = resolvePath(raw);
        if (p.isEmpty())
            return;
        if (p.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)) {
            if (xml && xml->isEmpty())
                *xml = p;
            return;
        }
        if (p.endsWith(QLatin1String(".gds"), Qt::CaseInsensitive)
            || p.endsWith(QLatin1String(".gdsii"), Qt::CaseInsensitive)) {
            if (gds && gds->isEmpty())
                *gds = p;
            return;
        }
        if (p.endsWith(QLatin1String(".py"), Qt::CaseInsensitive)) {
            if (modelPy && modelPy->isEmpty())
                *modelPy = p;
            // Also pull sibling layout from the model folder.
            pickFromDir(QFileInfo(p).absolutePath());
            return;
        }
        if (!QFileInfo(p).isDir())
            return;

        pickFromDir(p);
        if ((gds && !gds->isEmpty()) || (xml && !xml->isEmpty())
            || (modelPy && !modelPy->isEmpty()))
            return;

        const QStringList subdirs =
            QDir(p).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QStringList projectDirs;
        for (const QString &sd : subdirs) {
            const QString sub = QDir(p).filePath(sd);
            if (!QDir(sub).entryList(QStringList() << QStringLiteral("*.gds"), QDir::Files).isEmpty())
                projectDirs << sub;
        }
        if (projectDirs.size() == 1)
            pickFromDir(projectDirs.first());
    };

    const QString keyAlts =
        QStringLiteral("gds|xml|stack|substrate|stackup|layout|project|model|folder|example");

    // Keyed: "load model from: …", "gds: …"
    QRegularExpression keyed(
        QStringLiteral(
            R"((?:^|\n)\s*(?:(?:load|open|set)\s+)?(%1)(?:\s+from)?\s*[:=]\s*(.+?)(?=\n\s*(?:(?:load|open|set)\s+)?(?:%1|top\s*cell|generate|создай)(?:\s+from)?\s*[:=]|\n\s*\n|$))")
            .arg(keyAlts),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    auto kit = keyed.globalMatch(normalized);
    while (kit.hasNext()) {
        const auto m = kit.next();
        QString val = m.captured(2).trimmed();
        val.replace(QRegularExpression(QStringLiteral("\\s*[\\r\\n]+\\s*")), QString());
        assignPath(val);
    }

    // "load/open model from <path>" (colon optional)
    QRegularExpression fromPhrase(
        QStringLiteral(
            R"((?:load|open|take|set)\s+(?:gds|xml|stack|layout|project|folder|model|example)?\s*from\s*:?\s*([^\n]+))"),
        QRegularExpression::CaseInsensitiveOption);
    auto fit = fromPhrase.globalMatch(normalized);
    while (fit.hasNext()) {
        QString val = fit.next().captured(1).trimmed();
        val.replace(QRegularExpression(QStringLiteral("\\s*[\\r\\n]+\\s*")), QString());
        const int cut = val.indexOf(QRegularExpression(
            QStringLiteral("\\s+(?:and|generate|создай|import|add)\\b"),
            QRegularExpression::CaseInsensitiveOption));
        if (cut > 0)
            val = val.left(cut).trimmed();
        assignPath(val);
    }

    QRegularExpression quoted(QStringLiteral("\"([^\"]+)\"|'([^']+)'"));
    auto qit = quoted.globalMatch(normalized);
    while (qit.hasNext()) {
        const auto m = qit.next();
        assignPath(m.captured(1).isEmpty() ? m.captured(2) : m.captured(1));
    }

    QRegularExpression bare(
        QStringLiteral(
            R"((?:[A-Za-z]:[\\/]|\\\\|~?/|/)[^\s\"']+\.(?:gds|gdsii|xml|py))"),
        QRegularExpression::CaseInsensitiveOption);
    auto bit = bare.globalMatch(normalized);
    while (bit.hasNext())
        assignPath(bit.next().captured(0));

    if ((gds && gds->isEmpty()) || (xml && xml->isEmpty())
        || (modelPy && modelPy->isEmpty())) {
        QRegularExpression bareDir(
            QStringLiteral(R"((?:[A-Za-z]:[\\/]|\\\\|~?/|/)[^\s\"']+)"),
            QRegularExpression::CaseInsensitiveOption);
        auto dit = bareDir.globalMatch(normalized);
        while (dit.hasNext()) {
            const QString raw = dit.next().captured(0);
            if (raw.endsWith(QLatin1String(".gds"), Qt::CaseInsensitive)
                || raw.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)
                || raw.endsWith(QLatin1String(".py"), Qt::CaseInsensitive))
                continue;
            const QString resolved = resolvePath(raw);
            if (QFileInfo(resolved).isDir())
                assignPath(resolved);
        }
    }

    QRegularExpression cellRe(
        QStringLiteral(R"((?:top\s*cell|cell|topcell)\s*[:=]\s*([A-Za-z_][\w]*))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto cm = cellRe.match(normalized);
    if (cm.hasMatch() && cell)
        *cell = cm.captured(1);
}

/*!*******************************************************************************************************************
 * \brief Keyword / path heuristic that selects and runs MCP tool(s).
 *
 * \param userText User utterance.
 * \return Reply for the assistant bubble.
 **********************************************************************************************************************/
QString AssistantMcp::routeAndExecute(const QString &userText) const
{
    const QString q = userText.trimmed();
    const QString ql = q.toLower();

    auto runTool = [this](const QString &name, const QJsonObject &args) -> QString {
        return formatToolReply(name, callTool(name, args));
    };

    if (ql.contains(QStringLiteral("list tool")) || ql.contains(QStringLiteral("mcp"))
        || ql == QStringLiteral("tools") || ql.contains(QStringLiteral("что умеешь"))
        || ql.contains(QStringLiteral("what can"))) {
        // Refresh via tool call (keeps registry as source of truth) then pretty-print.
        callTool(QStringLiteral("list_tools"));
        return formatListToolsReply();
    }

    QString gds;
    QString xml;
    QString cell;
    QString modelPy;
    extractLayoutPaths(q, &gds, &xml, &cell, &modelPy);

    const bool wantsGenerate = ql.contains(QStringLiteral("generat"))
                               || ql.contains(QStringLiteral("default model"))
                               || ql.contains(QStringLiteral("шаблон"))
                               || ql.contains(QStringLiteral("создай модел"))
                               || ql.contains(QStringLiteral("сгенери"));

    const bool wantsModel = (ql.contains(QStringLiteral("load model"))
                             || ql.contains(QStringLiteral("open model"))
                             || ql.contains(QStringLiteral("загрузи модел"))
                             || (ql.contains(QStringLiteral("model"))
                                 && (ql.contains(QStringLiteral("load"))
                                     || ql.contains(QStringLiteral("open"))
                                     || ql.contains(QStringLiteral("from")))));

    if (wantsModel && !modelPy.isEmpty()) {
        QJsonObject args;
        args.insert(QStringLiteral("path"), modelPy);
        return runTool(QStringLiteral("load_model"), args);
    }

    const bool wantsLayout = (!gds.isEmpty() || !xml.isEmpty())
                             && (ql.contains(QStringLiteral("set_layout"))
                                 || ql.contains(QStringLiteral("take "))
                                 || ql.contains(QStringLiteral("возьми"))
                                 || ql.contains(QStringLiteral("load"))
                                 || ql.contains(QStringLiteral("open"))
                                 || ql.contains(QStringLiteral("from"))
                                 || ql.contains(QStringLiteral("gds"))
                                 || ql.contains(QStringLiteral("stack"))
                                 || ql.contains(QStringLiteral("xml"))
                                 || ql.contains(QStringLiteral("layout"))
                                 || ql.contains(QStringLiteral("project"))
                                 || ql.contains(QStringLiteral("model"))
                                 || wantsGenerate);

    if (wantsLayout && (!gds.isEmpty() || !xml.isEmpty())) {
        QJsonObject layoutArgs;
        if (!gds.isEmpty())
            layoutArgs.insert(QStringLiteral("gds"), gds);
        if (!xml.isEmpty())
            layoutArgs.insert(QStringLiteral("substrate_xml"), xml);
        if (!cell.isEmpty())
            layoutArgs.insert(QStringLiteral("top_cell"), cell);

        QStringList parts;
        parts << runTool(QStringLiteral("set_layout"), layoutArgs);

        // "load model" without a .py still falls back to layout; if a .py was found, load it.
        if (wantsModel && !modelPy.isEmpty()) {
            QJsonObject args;
            args.insert(QStringLiteral("path"), modelPy);
            parts << QString();
            parts << runTool(QStringLiteral("load_model"), args);
        } else if (wantsGenerate) {
            QJsonObject genArgs;
            genArgs.insert(QStringLiteral("apply"), true);
            parts << QString();
            parts << runTool(QStringLiteral("generate_default_model"), genArgs);
        }
        return parts.join(QLatin1Char('\n'));
    }

    if (wantsModel && modelPy.isEmpty() && gds.isEmpty() && xml.isEmpty()) {
        return QStringLiteral(
            "Couldn't resolve a model folder/file.\n"
            "Try a full folder name or a unique prefix:\n\n"
            "load model from: C:/Users/anton/Documents/EMStudio/examples/palace/inductor_500pH\n"
            "load model from: C:/Users/anton/Documents/EMStudio/examples/palace/induc");
    }

    if ((ql.contains(QStringLiteral("gds")) || ql.contains(QStringLiteral("xml"))
         || ql.contains(QStringLiteral("stack")) || ql.contains(QStringLiteral("load"))
         || ql.contains(QStringLiteral("from")))
        && gds.isEmpty() && xml.isEmpty() && modelPy.isEmpty()) {
        return QStringLiteral(
            "Couldn't resolve a .gds / .xml / model path.\n"
            "Examples:\n\n"
            "load model from: C:/Users/anton/Documents/EMStudio/examples/palace/inductor_500pH\n"
            "load gds from: C:/Users/anton/Documents/EMStudio/examples/palace/induc\n"
            "(prefix induc → inductor_500pH)\n\n"
            "gds: C:/…/file.gds\n"
            "xml: C:/…/stack.xml\n"
            "generate model");
    }

    {
        QRegularExpression re(
            QStringLiteral(
                R"((?:set\s+preference|set\s+pref|set)\s+([A-Za-z0-9_./ -]+?)\s*=\s*(.+)$)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(q);
        if (m.hasMatch()) {
            QJsonObject args;
            args.insert(QStringLiteral("key"), m.captured(1).trimmed());
            args.insert(QStringLiteral("value"), m.captured(2).trimmed());
            return runTool(QStringLiteral("set_preference"), args);
        }
    }

    if (ql.contains(QStringLiteral("pref")) || ql.contains(QStringLiteral("настройк")))
        return runTool(QStringLiteral("get_preferences"), {});

    if (wantsGenerate) {
        QJsonObject args;
        const bool apply = ql.contains(QStringLiteral("apply"))
                           || ql.contains(QStringLiteral("в редактор"))
                           || ql.contains(QStringLiteral("insert"));
        args.insert(QStringLiteral("apply"), apply);
        return runTool(QStringLiteral("generate_default_model"), args);
    }

    if (ql.contains(QStringLiteral("import port")) || ql.contains(QStringLiteral("ports from script"))
        || ql.contains(QStringLiteral("импорт порт")))
        return runTool(QStringLiteral("import_ports"), {});

    if (ql.contains(QStringLiteral("add port")) || ql.contains(QStringLiteral("ports from layout"))
        || ql.contains(QStringLiteral("порты из")) || ql == QStringLiteral("add ports")
        || ql.contains(QStringLiteral("добавь порт")))
        return runTool(QStringLiteral("add_ports_from_layout"), {});

    if (ql.contains(QStringLiteral("run sim")) || ql.contains(QStringLiteral("запусти"))
        || ql.contains(QStringLiteral("start sim")) || ql == QStringLiteral("run"))
        return runTool(QStringLiteral("run_simulation"), {});

    if (ql.contains(QStringLiteral("calc")) || ql.contains(QStringLiteral("калькул"))
        || ql.contains(QStringLiteral("s11")) || ql.contains(QStringLiteral("z11"))) {
        QJsonObject args;
        args.insert(QStringLiteral("expression"), q);
        return runTool(QStringLiteral("eval_rf"), args);
    }

    if (ql.contains(QStringLiteral("state")) || ql.contains(QStringLiteral("status"))
        || ql.contains(QStringLiteral("что открыто")) || ql.contains(QStringLiteral("current")))
        return runTool(QStringLiteral("get_app_state"), {});

    return QStringLiteral(
               "I didn't understand that as an MCP shortcut.\n\n"
               "Keyword examples: list tools · prefs · state · "
               "load model from: <folder> · import ports · run\n\n"
               "For natural-language requests, enable the LLM agent:\n"
               "  Preferences → Assistant →\n"
               "    ASSISTANT_BASE_URL  (e.g. https://api.openai.com/v1\n"
               "                        or http://localhost:11434/v1)\n"
               "    ASSISTANT_MODEL     (e.g. gpt-4o-mini)\n"
               "    ASSISTANT_API_KEY   (OpenAI key; optional for local Ollama)\n\n"
               "You said: “%1”")
        .arg(q);
}
