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

#include "assistantchatpanel.h"
#include "assistantagent.h"
#include "assistantmcp.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QIcon>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

namespace {

class ShimmerLabel : public QLabel
{
public:
    explicit ShimmerLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setText(QStringLiteral("Thinking"));
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 0.5);
        f.setBold(true);
        setFont(f);
        setMinimumHeight(22);
    }

    void setPhase(qreal phase)
    {
        m_phase = phase;
        update();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient g(0, 0, width(), 0);
        const qreal x = std::fmod(m_phase, 1.0);
        g.setColorAt(qBound(0.0, x - 0.35, 1.0), QColor(90, 110, 125));
        g.setColorAt(qBound(0.0, x - 0.10, 1.0), QColor(210, 160, 70));
        g.setColorAt(qBound(0.0, x, 1.0), QColor(245, 230, 190));
        g.setColorAt(qBound(0.0, x + 0.10, 1.0), QColor(210, 160, 70));
        g.setColorAt(qBound(0.0, x + 0.35, 1.0), QColor(90, 110, 125));

        p.setPen(QPen(QBrush(g), 0));
        p.setFont(font());
        p.drawText(rect(), alignment(), text());

        const int y = height() - 3;
        QLinearGradient bar(0, 0, width(), 0);
        bar.setColorAt(0.0, Qt::transparent);
        bar.setColorAt(qBound(0.0, x - 0.15, 1.0), Qt::transparent);
        bar.setColorAt(qBound(0.0, x, 1.0), QColor(210, 160, 70, 200));
        bar.setColorAt(qBound(0.0, x + 0.15, 1.0), Qt::transparent);
        bar.setColorAt(1.0, Qt::transparent);
        p.fillRect(0, y, width(), 2, bar);
    }

private:
    qreal m_phase = 0.0;
};

QString bubbleCss(bool user)
{
    if (user) {
        return QStringLiteral(
            "QFrame#chatBubble {"
            "  background-color: #2f4a5c;"
            "  border: 1px solid #3d6178;"
            "  border-radius: 10px;"
            "}"
            "QLabel#chatBody { color: #f2f6f8; padding: 2px; }");
    }
    return QStringLiteral(
        "QFrame#chatBubble {"
        "  background-color: #eceff1;"
        "  border: 1px solid #cfd8dc;"
        "  border-radius: 10px;"
        "}"
        "QLabel#chatBody { color: #243038; padding: 2px; }");
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Returns the MCP-mode tips dialog text (keyword shortcuts + LLM setup hint).
 **********************************************************************************************************************/
QString AssistantChatPanel::tipsText()
{
    return QObject::tr(
        "MCP shortcuts (no LLM)\n"
        "\n"
        "• Enter = send, Shift+Enter = new line\n"
        "• list tools — tool list\n"
        "• load model from: <folder or prefix>\n"
        "• gds: <path>  /  generate model\n"
        "• import ports / add ports / run\n"
        "\n"
        "To understand free-form language, set up the LLM agent:\n"
        "Preferences → Assistant → BASE_URL, MODEL, API_KEY.");
}

/*!*******************************************************************************************************************
 * \brief Constructs the assistant chat dock widget UI.
 *
 * \param parent Optional parent widget.
 **********************************************************************************************************************/
AssistantChatPanel::AssistantChatPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(6);

    auto *header = new QHBoxLayout();
    auto *title = new QLabel(tr("Assistant"), this);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);

    m_infoBtn = new QToolButton(this);
    m_infoBtn->setAutoRaise(true);
    m_infoBtn->setCursor(Qt::PointingHandCursor);
    m_infoBtn->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_infoBtn->setIconSize(QSize(16, 16));
    m_infoBtn->setToolTip(tr("Tips"));
    m_infoBtn->setFixedSize(22, 22);

    m_status = new QLabel(tr("Ready"), this);
    m_status->setStyleSheet(QStringLiteral("color: #6a7a84;"));

    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_status);
    header->addWidget(m_infoBtn);
    root->addLayout(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; }"
        "QWidget#chatThread { background: transparent; }"));

    m_thread = new QWidget(m_scroll);
    m_thread->setObjectName(QStringLiteral("chatThread"));
    m_threadLayout = new QVBoxLayout(m_thread);
    m_threadLayout->setContentsMargins(2, 2, 2, 2);
    m_threadLayout->setSpacing(8);
    m_threadLayout->addStretch(1);
    m_scroll->setWidget(m_thread);
    root->addWidget(m_scroll, 1);

    auto *row = new QHBoxLayout();
    m_input = new QPlainTextEdit(this);
    m_input->setPlaceholderText(tr("Message…"));
    m_input->setTabChangesFocus(true);
    m_input->setMaximumBlockCount(0);
    m_input->setFixedHeight(56);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  border: 1px solid #c0c6cb;"
        "  border-radius: 6px;"
        "  padding: 6px;"
        "  background: palette(base);"
        "}"));
    m_input->installEventFilter(this);

    m_sendBtn = new QToolButton(this);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setFixedSize(26, 26);
    m_sendBtn->setIconSize(QSize(24, 24));
    m_sendBtn->setAutoRaise(true);
    m_sendBtn->setToolTip(tr("Send"));
    m_sendBtn->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  border: none;"
        "  background: transparent;"
        "  padding: 0;"
        "}"
        "QToolButton:hover { background: transparent; }"
        "QToolButton:pressed { background: transparent; }"));
    updateSendStopIcon();
    row->addWidget(m_input, 1);
    row->addWidget(m_sendBtn, 0, Qt::AlignVCenter);
    root->addLayout(row);

    m_shimmerTimer = new QTimer(this);
    m_shimmerTimer->setInterval(30);
    connect(m_shimmerTimer, &QTimer::timeout, this, &AssistantChatPanel::onThinkingTick);

    m_replyDelay = new QTimer(this);
    m_replyDelay->setSingleShot(true);
    connect(m_replyDelay, &QTimer::timeout, this, &AssistantChatPanel::finishHeuristicReply);

    connect(m_sendBtn, &QToolButton::clicked, this, &AssistantChatPanel::onSendClicked);
    connect(m_infoBtn, &QToolButton::clicked, this, &AssistantChatPanel::showTips);
}

/*!*******************************************************************************************************************
 * \brief Attaches the MCP registry for heuristic replies.
 **********************************************************************************************************************/
void AssistantChatPanel::setMcp(AssistantMcp *mcp)
{
    m_mcp = mcp;
}

/*!*******************************************************************************************************************
 * \brief Attaches the LLM agent and wires finished / failed / status signals.
 **********************************************************************************************************************/
void AssistantChatPanel::setAgent(AssistantAgent *agent)
{
    if (m_agent == agent) {
        refreshModeUi();
        return;
    }
    if (m_agent) {
        disconnect(m_agent, nullptr, this, nullptr);
    }
    m_agent = agent;
    if (m_agent) {
        connect(m_agent, &AssistantAgent::finished, this, &AssistantChatPanel::onAgentFinished);
        connect(m_agent, &AssistantAgent::failed, this, &AssistantChatPanel::onAgentFailed);
        connect(m_agent, &AssistantAgent::statusChanged, this, &AssistantChatPanel::onAgentStatus);
    }
    refreshModeUi();
}

/*!*******************************************************************************************************************
 * \brief Shows or hides the tips button depending on LLM configuration.
 **********************************************************************************************************************/
void AssistantChatPanel::refreshModeUi()
{
    const bool llm = m_agent && m_agent->isConfigured();
    if (m_infoBtn)
        m_infoBtn->setVisible(!llm);
    if (!m_busy && m_status) {
        m_status->setText(llm ? tr("LLM agent") : tr("Ready"));
    }
}

/*!*******************************************************************************************************************
 * \brief Shows the MCP keyword tips message box.
 **********************************************************************************************************************/
void AssistantChatPanel::showTips()
{
    QMessageBox::information(this, tr("Assistant tips"), tipsText());
}

/*!*******************************************************************************************************************
 * \brief Clears chat bubbles and agent history.
 **********************************************************************************************************************/
void AssistantChatPanel::clearChat()
{
    if (m_agent)
        m_agent->clearHistory();
    removeThinkingBubble();
    for (Bubble &b : m_bubbles) {
        if (b.frame)
            b.frame->deleteLater();
    }
    m_bubbles.clear();
    setBusy(false);
    m_usingAgent = false;
}

/*!*******************************************************************************************************************
 * \brief Enter sends the message; Shift+Enter inserts a newline (ignored while busy).
 **********************************************************************************************************************/
bool AssistantChatPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (ke->modifiers() & Qt::ShiftModifier)
                return false;
            if (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
                return false;
            if (!m_busy)
                onSend();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the send/stop icon when the application palette changes.
 **********************************************************************************************************************/
void AssistantChatPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        updateSendStopIcon();
}

/*!*******************************************************************************************************************
 * \brief Send when idle; stop the in-flight agent / heuristic reply when busy.
 **********************************************************************************************************************/
void AssistantChatPanel::onSendClicked()
{
    if (m_busy)
        onStop();
    else
        onSend();
}

/*!*******************************************************************************************************************
 * \brief Sends the input text via LLM agent or heuristic MCP router.
 **********************************************************************************************************************/
void AssistantChatPanel::onSend()
{
    if (m_busy)
        return;

    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty())
        return;

    m_input->clear();
    appendBubble(Role::User, text);
    emit userMessageSubmitted(text);

    m_pendingUserText = text;
    setBusy(true);

    removeThinkingBubble();
    m_thinkingFrame = appendBubble(Role::Thinking, QString());
    m_shimmerTimer->start();

    m_usingAgent = (m_agent && m_agent->isConfigured());
    if (m_usingAgent) {
        m_agent->send(text);
    } else {
        m_replyDelay->start(400);
    }
}

/*!*******************************************************************************************************************
 * \brief Cancels the LLM turn or heuristic delay and shows a short "Stopped." bubble.
 **********************************************************************************************************************/
void AssistantChatPanel::onStop()
{
    if (!m_busy)
        return;

    m_replyDelay->stop();
    if (m_usingAgent && m_agent && m_agent->isBusy()) {
        m_agent->cancel(); // emits failed("Stopped.") → onAgentFailed
        return;
    }
    completeAssistantReply(tr("Stopped."));
}

/*!*******************************************************************************************************************
 * \brief Advances the shimmer animation on the thinking bubble.
 **********************************************************************************************************************/
void AssistantChatPanel::onThinkingTick()
{
    m_shimmerPhase += 0.025;
    if (m_shimmerPhase > 1.0)
        m_shimmerPhase -= 1.0;
    if (m_setThinkingPhase)
        m_setThinkingPhase(m_shimmerPhase);
}

/*!*******************************************************************************************************************
 * \brief Completes a non-LLM reply after the short delay timer.
 **********************************************************************************************************************/
void AssistantChatPanel::finishHeuristicReply()
{
    completeAssistantReply(buildHeuristicReply(m_pendingUserText));
}

/*!*******************************************************************************************************************
 * \brief Shows a successful LLM reply in the chat thread.
 **********************************************************************************************************************/
void AssistantChatPanel::onAgentFinished(const QString &reply)
{
    if (!m_usingAgent)
        return;
    completeAssistantReply(reply);
}

/*!*******************************************************************************************************************
 * \brief Shows an LLM / transport error as an assistant bubble.
 **********************************************************************************************************************/
void AssistantChatPanel::onAgentFailed(const QString &error)
{
    if (!m_usingAgent)
        return;
    completeAssistantReply(error);
}

/*!*******************************************************************************************************************
 * \brief Updates the header status label during an agent turn.
 **********************************************************************************************************************/
void AssistantChatPanel::onAgentStatus(const QString &status)
{
    if (!m_busy)
        return;
    m_status->setText(status);
}

/*!*******************************************************************************************************************
 * \brief Removes the thinking bubble and appends the final assistant text.
 **********************************************************************************************************************/
void AssistantChatPanel::completeAssistantReply(const QString &text)
{
    m_shimmerTimer->stop();
    m_replyDelay->stop();
    removeThinkingBubble();
    appendBubble(Role::Assistant, text);
    m_pendingUserText.clear();
    m_usingAgent = false;
    setBusy(false);
}

/*!*******************************************************************************************************************
 * \brief Builds a reply using \c AssistantMcp::handleUserMessage.
 **********************************************************************************************************************/
QString AssistantChatPanel::buildHeuristicReply(const QString &userText) const
{
    if (m_mcp)
        return m_mcp->handleUserMessage(userText);
    return tr("MCP is not connected yet.");
}

/*!*******************************************************************************************************************
 * \brief Appends a user, assistant, or thinking bubble to the thread.
 **********************************************************************************************************************/
QFrame *AssistantChatPanel::appendBubble(Role role, const QString &text)
{
    auto *wrap = new QWidget(m_thread);
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    auto *frame = new QFrame(wrap);
    frame->setObjectName(QStringLiteral("chatBubble"));
    frame->setStyleSheet(bubbleCss(role == Role::User));
    auto *vl = new QVBoxLayout(frame);
    vl->setContentsMargins(10, 8, 10, 8);
    vl->setSpacing(2);

    QLabel *body = nullptr;
    if (role == Role::Thinking) {
        auto *sh = new ShimmerLabel(frame);
        m_thinkingLabel = sh;
        m_setThinkingPhase = [sh](qreal phase) { sh->setPhase(phase); };
        vl->addWidget(sh);
        body = sh;
    } else {
        body = new QLabel(text, frame);
        body->setObjectName(QStringLiteral("chatBody"));
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        vl->addWidget(body);
    }

    const int maxW = qMax(180, int(width() * 0.78));
    frame->setMaximumWidth(maxW);

    if (role == Role::User) {
        hl->addStretch(1);
        hl->addWidget(frame, 0, Qt::AlignRight);
    } else {
        hl->addWidget(frame, 0, Qt::AlignLeft);
        hl->addStretch(1);
    }

    const int stretchIndex = m_threadLayout->count() - 1;
    m_threadLayout->insertWidget(qMax(0, stretchIndex), wrap);

    Bubble b;
    b.role = role;
    b.frame = frame;
    b.body = body;
    m_bubbles.append(b);

    scrollToBottom();
    return frame;
}

/*!*******************************************************************************************************************
 * \brief Enables or disables input while a reply is in progress; swaps send ↔ stop icon.
 **********************************************************************************************************************/
void AssistantChatPanel::setBusy(bool busy)
{
    m_busy = busy;
    m_input->setEnabled(!busy);
    updateSendStopIcon();
    if (m_sendBtn)
        m_sendBtn->setToolTip(busy ? tr("Stop") : tr("Send"));
    m_status->setText(busy ? tr("Thinking…") : tr("Ready"));
    m_status->setStyleSheet(busy
                                ? QStringLiteral("color: #5a6a74; font-weight: 600;")
                                : QStringLiteral("color: #6a7a84;"));
    if (!busy)
        refreshModeUi();
}

/*!*******************************************************************************************************************
 * \brief Paints a circular send (↑) or stop (■) icon; soft gray fill, thin dark glyph.
 **********************************************************************************************************************/
void AssistantChatPanel::updateSendStopIcon()
{
    if (!m_sendBtn)
        return;
    const QPalette pal = palette();
    // Slightly darker than the panel background; thin black arrow (not Windows Highlight blue).
    QColor fill = pal.color(QPalette::Window);
    fill = fill.darker(112);
    const QColor glyph = QColor(QStringLiteral("#1a1a1a"));
    m_sendBtn->setIcon(makeSendStopIcon(m_busy, fill, glyph));
}

/*!*******************************************************************************************************************
 * \brief Builds a circular action icon: thin up-arrow (send) or stop square.
 *
 * \param stop  When true, draw a stop square; otherwise an up-arrow outline.
 * \param fill  Circle fill (soft gray, slightly darker than the panel).
 * \param glyph Arrow / stop stroke color (near-black).
 **********************************************************************************************************************/
QIcon AssistantChatPanel::makeSendStopIcon(bool stop, const QColor &fill, const QColor &glyph)
{
    const int s = 72;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(2, 2, s - 4, s - 4);

    const qreal stroke = qMax(2.0, s * 0.045);
    QPen pen(glyph, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (stop) {
        const qreal pad = s * 0.34;
        p.setBrush(glyph);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(pad, pad, s - 2 * pad, s - 2 * pad), 2.0, 2.0);
    } else {
        // Thin up-arrow: V head + stem (stroke only)
        const qreal cx = s * 0.5;
        const qreal top = s * 0.24;
        const qreal midY = s * 0.46;
        const qreal bot = s * 0.74;
        const qreal headW = s * 0.18;
        QPainterPath path;
        path.moveTo(cx - headW, midY);
        path.lineTo(cx, top);
        path.lineTo(cx + headW, midY);
        path.moveTo(cx, top);
        path.lineTo(cx, bot);
        p.drawPath(path);
    }
    return QIcon(pm);
}

/*!*******************************************************************************************************************
 * \brief Scrolls the chat thread to the latest message.
 **********************************************************************************************************************/
void AssistantChatPanel::scrollToBottom()
{
    QTimer::singleShot(0, this, [this]() {
        if (auto *bar = m_scroll->verticalScrollBar())
            bar->setValue(bar->maximum());
    });
}

/*!*******************************************************************************************************************
 * \brief Removes the shimmer thinking bubble if present.
 **********************************************************************************************************************/
void AssistantChatPanel::removeThinkingBubble()
{
    if (!m_thinkingFrame)
        return;

    for (int i = m_bubbles.size() - 1; i >= 0; --i) {
        if (m_bubbles[i].frame == m_thinkingFrame) {
            if (QWidget *wrap = m_thinkingFrame->parentWidget())
                wrap->deleteLater();
            m_bubbles.removeAt(i);
            break;
        }
    }
    m_thinkingFrame = nullptr;
    m_thinkingLabel = nullptr;
    m_setThinkingPhase = nullptr;
}
