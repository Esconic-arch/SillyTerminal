#pragma once

#include "accountstore.h"
#include <QFutureWatcher>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QTextEdit;

class LoginWidget : public QWidget {
public:
    explicit LoginWidget(const QString &runtimeDirectory, QWidget *parent = nullptr);
    ~LoginWidget() override;
    void reset();
    void focusInput();
    std::function<void(const LoginResult &)> onAuthenticated;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Stage { UserName, Password, NewPassword, ConfirmPassword, Waiting, Retry };
    QString m_directory;
    QString m_transcript;
    QString m_userName;
    QString m_newPassword;
    Stage m_stage = Stage::Retry;
    QTextEdit *m_log;
    QLabel *m_prompt;
    QLineEdit *m_input;
    QWidget *m_inputRow;
    int m_lineSpacing = 115;
    QFutureWatcher<LoginResult> m_worker;

    void append(const QString &text);
    void updateLog();
    void layoutLog();
    void prompt(const QString &label, Stage stage, bool secret);
    void submit();
    void startAttempt(QString password, bool setup);
    void finishAttempt();
};
