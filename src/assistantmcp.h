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

#ifndef ASSISTANTMCP_H
#define ASSISTANTMCP_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

/*!*******************************************************************************************************************
 * \class AssistantMcp
 * \brief In-process MCP-style tool registry used by the EMStudio Assistant chat.
 *
 * Tools are registered with a name, human description, JSON schema (informational),
 * and a handler. The chat routes user intents to \c callTool(); a future stdio MCP
 * server can reuse the same registry. The LLM agent (\c AssistantAgent) also drives
 * these tools via OpenAI-compatible function calling.
 **********************************************************************************************************************/
class AssistantMcp : public QObject
{
    Q_OBJECT

public:
    using ToolHandler = std::function<QJsonObject(const QJsonObject &args)>;

    /*!*******************************************************************************************************************
     * \struct Tool
     * \brief One registered MCP tool: name, description, input schema, and handler.
     **********************************************************************************************************************/
    struct Tool {
        QString name;
        QString description;
        QJsonObject inputSchema;
        ToolHandler handler;
    };

    explicit AssistantMcp(QObject *parent = nullptr);

    /*!*******************************************************************************************************************
     * \brief Registers a tool in the in-process MCP registry.
     *
     * \param name         Unique tool name (e.g. \c load_model).
     * \param description  Human-readable description shown in manifests / LLM prompts.
     * \param inputSchema  JSON Schema object describing arguments (informational).
     * \param handler      Callback invoked by \c callTool(); returns a JSON result object.
     **********************************************************************************************************************/
    void registerTool(const QString &name,
                      const QString &description,
                      const QJsonObject &inputSchema,
                      ToolHandler handler);

    /*! \brief Returns a copy of all registered tools. */
    QVector<Tool> tools() const { return m_tools; }

    /*! \brief JSON array of {name, description, inputSchema} for clients / agents. */
    QJsonArray toolsManifest() const;

    /*! \brief Returns whether a tool with \a name is registered. */
    bool hasTool(const QString &name) const;

    /*!*******************************************************************************************************************
     * \brief Invokes a registered tool by name.
     *
     * \param name Tool name.
     * \param args JSON object of arguments (may be empty).
     * \return Handler result, or an error object if the tool is missing / has no handler.
     **********************************************************************************************************************/
    QJsonObject callTool(const QString &name, const QJsonObject &args = QJsonObject()) const;

    /*!*******************************************************************************************************************
     * \brief Heuristic router: pick tool(s) from a user utterance and return a reply string.
     *
     * Used when the LLM agent is not configured. Unrecognized input suggests enabling
     * Preferences → Assistant.
     *
     * \param userText Free-form chat message.
     * \return Formatted assistant reply (tool output or help text).
     **********************************************************************************************************************/
    QString handleUserMessage(const QString &userText) const;

    /*!*******************************************************************************************************************
     * \brief Resolve GDS / XML / model .py from free text or a (possibly truncated) folder path.
     *
     * Supports phrases like \c "load model from: …/palace/induc" and unique prefix expansion
     * of folder names under the parent directory.
     *
     * \param text    User text containing paths.
     * \param gds     Out: resolved GDS path (may be null).
     * \param xml     Out: resolved substrate XML path (may be null).
     * \param cell    Out: optional top-cell hint (may be null).
     * \param modelPy Out: resolved Python model path (may be null).
     **********************************************************************************************************************/
    static void extractLayoutPaths(const QString &text,
                                   QString *gds,
                                   QString *xml,
                                   QString *cell,
                                   QString *modelPy = nullptr);

private:
    static QJsonObject okResult(const QJsonObject &data);
    static QJsonObject errResult(const QString &message);
    static QString formatValue(const QJsonValue &v, int indent = 0);
    static QString formatObjectLines(const QJsonObject &obj, int indent = 0);
    QString formatToolReply(const QString &toolName, const QJsonObject &result) const;
    QString formatListToolsReply() const;
    QString routeAndExecute(const QString &userText) const;

    QVector<Tool> m_tools;
};

#endif // ASSISTANTMCP_H
