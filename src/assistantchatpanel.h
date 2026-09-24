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

#ifndef ASSISTANTCHATPANEL_H
#define ASSISTANTCHATPANEL_H

#include <QWidget>
#include <QVector>
#include <QIcon>
#include <functional>

class QScrollArea;
class QVBoxLayout;
class QPlainTextEdit;
class QToolButton;
class QLabel;
class QFrame;
class QTimer;
class AssistantMcp;
class AssistantAgent;

/*!*******************************************************************************************************************
 * \class AssistantChatPanel
 * \brief Bottom-dock assistant chat with shimmer "thinking" and LLM / MCP tools.
 *
 * When an \c AssistantAgent is configured (Preferences → Assistant), messages go to
 * the LLM with MCP tool calling. Otherwise a keyword heuristic router
 * (\c AssistantMcp::handleUserMessage) is used; the tips (ⓘ) button is shown only
 * in that mode.
 **********************************************************************************************************************/
class AssistantChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssistantChatPanel(QWidget *parent = nullptr);

    /*! \brief Returns whether a reply is being prepared (input disabled). */
    bool isBusy() const { return m_busy; }

    /*! \brief Attaches the MCP registry used by the heuristic fallback router. */
    void setMcp(AssistantMcp *mcp);

    /*! \brief Attaches the LLM agent; connects finished / failed / status signals. */
    void setAgent(AssistantAgent *agent);

    /*! \brief Updates tip-button visibility and status text from agent configuration. */
    void refreshModeUi();

public slots:
    /*! \brief Clears bubbles and agent conversation history. */
    void clearChat();

signals:
    /*! \brief Emitted when the user sends a message (also logged by MainWindow). */
    void userMessageSubmitted(const QString &text);

private slots:
    void onSendClicked();
    void onSend();
    void onStop();
    void onThinkingTick();
    void finishHeuristicReply();
    void onAgentFinished(const QString &reply);
    void onAgentFailed(const QString &error);
    void onAgentStatus(const QString &status);
    void showTips();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    enum class Role { User, Assistant, Thinking };

    struct Bubble {
        Role role = Role::User;
        QFrame *frame = nullptr;
        QLabel *body = nullptr;
    };

    QFrame *appendBubble(Role role, const QString &text);
    void setBusy(bool busy);
    void updateSendStopIcon();
    void scrollToBottom();
    void removeThinkingBubble();
    void completeAssistantReply(const QString &text);
    QString buildHeuristicReply(const QString &userText) const;
    static QString tipsText();
    static QIcon makeSendStopIcon(bool stop, const QColor &fill, const QColor &glyph);

    QScrollArea *m_scroll = nullptr;
    QWidget *m_thread = nullptr;
    QVBoxLayout *m_threadLayout = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QToolButton *m_sendBtn = nullptr;
    QToolButton *m_infoBtn = nullptr;
    QLabel *m_status = nullptr;

    QVector<Bubble> m_bubbles;
    QFrame *m_thinkingFrame = nullptr;
    QLabel *m_thinkingLabel = nullptr;
    std::function<void(qreal)> m_setThinkingPhase;
    QTimer *m_shimmerTimer = nullptr;
    qreal m_shimmerPhase = 0.0;

    bool m_busy = false;
    bool m_usingAgent = false;
    QString m_pendingUserText;
    QTimer *m_replyDelay = nullptr;
    AssistantMcp *m_mcp = nullptr;
    AssistantAgent *m_agent = nullptr;
};

#endif // ASSISTANTCHATPANEL_H
