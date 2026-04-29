#pragma once
/**
 * @file ServerLogView.h
 * @brief Server Log View Widget
 */

#include <QWidget>
#include <QPlainTextEdit>
#include <QMutex>
#include <QStringList>

namespace nevo {

class ServerLogView : public QWidget {
    Q_OBJECT

public:
    explicit ServerLogView(QWidget* parent = nullptr);
    ~ServerLogView() override;

public slots:
    void appendLog(const QString& message);
    void clearLog();

private:
    void setupUi();
    void flushBuffer();
    void keyPressEvent(QKeyEvent* event) override;

    QPlainTextEdit* log_edit_;
    QStringList buffer_;
    QMutex buffer_mutex_;
};

} // namespace nevo
