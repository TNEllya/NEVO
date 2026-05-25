#pragma once
/**
 * @file LoginDialog.h
 * @brief 登录对话框
 */

#include <QDialog>
#include <QLineEdit>

namespace nevo {

/**
 * @class LoginDialog
 * @brief 登录对话框，输入用户名
 *
 * 简单的模态对话框，包含用户名输入框和确认/取消按钮。
 */
class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QWidget* parent = nullptr);

    /// 获取输入的用户名
    QString username() const;

private:
    QLineEdit* username_edit_;
};

} // namespace nevo
