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

#ifndef ASSISTANTAGENT_H
#define ASSISTANTAGENT_H

#include <QJsonArray>
#include <QObject>
#include <QString>

class AssistantMcp;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

/*!*******************************************************************************************************************
 * \class AssistantAgent
 * \brief OpenAI-compatible LLM agent that drives \c AssistantMcp tools via chat completions.
 *
 * Configuration comes from Preferences:
 *  - \c ASSISTANT_API_KEY (stored encrypted via DPAPI),
 *  - \c ASSISTANT_BASE_URL,
 *  - \c ASSISTANT_MODEL.
 *
 * Works with OpenAI, local Ollama (\c http://localhost:11434/v1), LM Studio, etc.
 * HTTPS uses system curl (Schannel) when Qt TLS/OpenSSL is unavailable — common on MinGW builds.
 **********************************************************************************************************************/
class AssistantAgent : public QObject
{
    Q_OBJECT

public:
    explicit AssistantAgent(QObject *parent = nullptr);

    /*! \brief Attaches the MCP tool registry used for function calls. */
    void setMcp(AssistantMcp *mcp);

    /*!*******************************************************************************************************************
     * \brief Applies LLM endpoint settings from Preferences (or elsewhere).
     *
     * \param apiKey  Bearer token (optional for local servers).
     * \param baseUrl Chat Completions base URL (e.g. \c https://api.openai.com/v1).
     * \param model   Model id (e.g. \c gpt-4o-mini).
     **********************************************************************************************************************/
    void configure(const QString &apiKey, const QString &baseUrl, const QString &model);

    /*! \brief Returns true when base URL + model are set (and API key for non-local hosts). */
    bool isConfigured() const;

    /*! \brief Returns whether a request / tool loop is in progress. */
    bool isBusy() const { return m_busy; }

    /*! \brief Clears conversation history (keeps configuration). */
    void clearHistory();

    /*! \brief Aborts an in-flight HTTP / curl request and emits \c failed("Stopped."). */
    void cancel();

    /*!*******************************************************************************************************************
     * \brief Sends a user message to the LLM and runs the tool-calling loop.
     *
     * Emits \c finished with the final assistant text, or \c failed on errors.
     *
     * \param userText User chat message.
     **********************************************************************************************************************/
    void send(const QString &userText);

signals:
    /*! \brief Final assistant reply after the tool loop completes. */
    void finished(const QString &reply);

    /*! \brief Fatal / transport / configuration error message. */
    void failed(const QString &error);

    /*! \brief Short status for the chat header (Calling model… / Tool: …). */
    void statusChanged(const QString &status);

private slots:
    void onNetworkFinished();

private:
    QString chatCompletionsUrl() const;
    QJsonArray openaiToolsManifest() const;
    void postNextCompletion();
    void postViaQt(const QByteArray &bodyJson);
    void postViaCurl(const QByteArray &bodyJson);
    void handleHttpPayload(const QByteArray &raw, const QString &transportError);
    void handleChoiceMessage(const QJsonObject &message);
    void finishWithText(const QString &text);
    void failWith(const QString &error);
    static bool qtSslWorks();
    static QString findCurl();
    QString buildSystemPrompt() const;

    AssistantMcp *m_mcp = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QProcess *m_curl = nullptr;
    QString m_curlBodyPath;

    QString m_apiKey;
    QString m_baseUrl;
    QString m_model;

    QJsonArray m_messages;
    int m_round = 0;
    bool m_busy = false;

    static constexpr int kMaxRounds = 10;
};

#endif // ASSISTANTAGENT_H
