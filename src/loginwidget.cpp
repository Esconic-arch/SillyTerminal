#include "loginwidget.h"
#include "terminalappearance.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <cmath>

LoginWidget::LoginWidget(const QString &runtimeDirectory, QWidget *parent)
    : QWidget(parent), m_directory(runtimeDirectory) {
    setObjectName("loginPage");
    setStyleSheet("#loginPage { background: #1c1c1f; }");
    const auto appearance = TerminalAppearance::load();
    setFont(appearance.font);
    m_lineSpacing = appearance.lineSpacing;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(0);
    m_log = new QTextEdit(this);
    m_log->setObjectName("loginTranscript");
    m_log->setReadOnly(true);
    m_log->setAcceptRichText(false);
    m_log->setAcceptDrops(false);
    m_log->setFocusPolicy(Qt::NoFocus);
    m_log->setFrameStyle(QFrame::NoFrame);
    m_log->setFont(font());
    m_log->document()->setDefaultFont(font());
    m_log->document()->setDocumentMargin(0);
    m_log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_log->setStyleSheet("QTextEdit { background: transparent; color: #dedee5; border: none; selection-background-color: #45465b; }");
    layout->addWidget(m_log);
    m_inputRow = new QWidget(this);
    auto *row = new QHBoxLayout(m_inputRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    m_prompt = new QLabel(m_inputRow);
    m_prompt->setObjectName("loginPrompt");
    m_prompt->setTextFormat(Qt::PlainText);
    m_prompt->setFont(font());
    m_prompt->setStyleSheet("QLabel { color: #dedee5; }");
    row->addWidget(m_prompt);
    m_input = new QLineEdit(m_inputRow);
    m_input->setObjectName("loginInput");
    m_input->setFont(font());
    m_input->setFrame(false);
    m_input->setTextMargins(0, 0, 0, 0);
    m_input->setAcceptDrops(false);
    m_input->setContextMenuPolicy(Qt::NoContextMenu);
    m_input->setStyleSheet("QLineEdit { background: transparent; color: #dedee5; border: none; padding: 0; selection-background-color: #45465b; }");
    m_input->installEventFilter(this);
    row->addWidget(m_input, 1);
    m_inputRow->setFixedHeight(static_cast<int>(std::ceil(QFontMetricsF(font()).lineSpacing() * m_lineSpacing / 100.0)));
    layout->addWidget(m_inputRow);
    layout->addStretch();
    setFocusProxy(m_input);
    connect(m_input, &QLineEdit::returnPressed, this, [this] { submit(); });
    connect(&m_worker, &QFutureWatcher<LoginResult>::finished, this, [this] { finishAttempt(); });
    connect(m_log->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
        this, [this] { layoutLog(); });
    reset();
}

LoginWidget::~LoginWidget() {
    // The worker captures values only. Let an atomic account write finish when
    // the window is closed rather than interrupting it partway through.
    m_worker.waitForFinished();
}

void LoginWidget::reset() {
    if (m_worker.isRunning()) return;
    const auto appearance = TerminalAppearance::load();
    setFont(appearance.font);
    m_lineSpacing = appearance.lineSpacing;
    m_log->setFont(font());
    m_log->document()->setDefaultFont(font());
    m_prompt->setFont(font());
    m_input->setFont(font());
    m_inputRow->setFixedHeight(static_cast<int>(std::ceil(QFontMetricsF(font()).lineSpacing() * m_lineSpacing / 100.0)));
    m_newPassword.clear();
    m_userName.clear();
    const AccountStore account(m_directory);
    m_transcript = account.identity().hostName + " tty1\n\n";
    if (account.state() == AccountStore::State::NeedsSetup) {
        m_transcript += "Local account setup\nAccount: " + account.identity().userName + "\n\n";
        prompt("New password: ", Stage::NewPassword, true);
    } else if (account.state() == AccountStore::State::Ready) {
        prompt(account.identity().hostName + " login: ", Stage::UserName, false);
    } else {
        m_transcript += "login: " + account.error() + "\n\n";
        prompt("Press Enter to retry: ", Stage::Retry, true);
    }
    updateLog();
}

void LoginWidget::focusInput() { m_input->setFocus(); }

void LoginWidget::prompt(const QString &label, Stage stage, bool secret) {
    m_stage = stage;
    m_prompt->setText(label);
    m_input->clear();
    m_input->setReadOnly(false);
    m_input->setMaxLength(secret ? 1024 : 64);
    m_input->setEchoMode(secret ? QLineEdit::NoEcho : QLineEdit::Normal);
    m_input->setInputMethodHints(secret
        ? Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText : Qt::ImhNone);
    m_input->setAccessibleName(secret ? "Password" : "Username");
    m_inputRow->show();
    layoutLog();
    focusInput();
}

void LoginWidget::append(const QString &text) {
    m_transcript += text;
    if (m_transcript.size() > 16384)
        m_transcript.remove(0, m_transcript.indexOf('\n', m_transcript.size() - 12288) + 1);
    updateLog();
}

void LoginWidget::updateLog() {
    m_log->setPlainText(m_transcript.endsWith('\n') ? m_transcript.chopped(1) : m_transcript);
    QTextCursor all(m_log->document());
    all.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(m_lineSpacing, QTextBlockFormat::ProportionalHeight);
    format.setTopMargin(0);
    format.setBottomMargin(0);
    all.mergeBlockFormat(format);
    layoutLog();
}

void LoginWidget::layoutLog() {
    const int available = qMax(0, height() - 48 - (m_inputRow->isHidden() ? 0 : m_inputRow->height()));
    const int wanted = static_cast<int>(std::ceil(m_log->document()->size().height()));
    const int logHeight = qMin(available, wanted);
    if (m_log->height() != logHeight) m_log->setFixedHeight(logHeight);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void LoginWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutLog();
}

bool LoginWidget::eventFilter(QObject *object, QEvent *event) {
    if (object == m_input && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_C && key->modifiers().testFlag(Qt::ControlModifier)) {
            if (m_stage != Stage::Waiting) reset();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void LoginWidget::submit() {
    if (m_stage == Stage::Waiting) return;
    if (m_stage == Stage::Retry) { reset(); return; }
    QString value = m_input->text();
    m_input->clear();
    if (m_stage == Stage::UserName) {
        m_userName = value.trimmed();
        if (m_userName.isEmpty()) return;
        append(m_prompt->text() + m_userName + '\n');
        prompt("Password: ", Stage::Password, true);
    } else if (m_stage == Stage::NewPassword) {
        append(m_prompt->text() + '\n');
        if (value.isEmpty()) {
            append("Password cannot be empty.\n\n");
            prompt("New password: ", Stage::NewPassword, true);
            return;
        }
        m_newPassword = value;
        prompt("Retype new password: ", Stage::ConfirmPassword, true);
    } else if (m_stage == Stage::ConfirmPassword) {
        append(m_prompt->text() + '\n');
        if (value != m_newPassword) {
            m_newPassword.clear();
            append("Passwords do not match.\n\n");
            prompt("New password: ", Stage::NewPassword, true);
            return;
        }
        m_newPassword.clear();
        startAttempt(std::move(value), true);
    } else if (m_stage == Stage::Password) {
        append(m_prompt->text() + '\n');
        startAttempt(std::move(value), false);
    }
}

void LoginWidget::startAttempt(QString password, bool setup) {
    m_stage = Stage::Waiting;
    m_input->setReadOnly(true);
    m_inputRow->hide();
    layoutLog();
    m_worker.setFuture(QtConcurrent::run([directory = m_directory, user = m_userName,
        password = std::move(password), setup] {
        AccountStore account(directory);
        return setup ? account.createAndLogin(password) : account.login(user, password);
    }));
}

void LoginWidget::finishAttempt() {
    const auto result = m_worker.result();
    if (result.authenticated) {
        if (onAuthenticated) onAuthenticated(result);
        return;
    }
    if (result.rejected) {
        append("\nLogin incorrect\n\n");
        // Keep the short retry delay in the event loop so the window responds.
        QTimer::singleShot(1000, this, [this] {
            prompt(LocalIdentity{}.hostName + " login: ", Stage::UserName, false);
        });
    } else {
        append("\nlogin: " + result.error + "\n\n");
        prompt("Press Enter to retry: ", Stage::Retry, true);
    }
}
