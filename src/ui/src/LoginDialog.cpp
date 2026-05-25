/**
 * @file LoginDialog.cpp
 * @brief LoginDialog 实现 - 登录对话框
 */

#include "nevo/ui/LoginDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFont>
#include <QPushButton>
#include <QLineEdit>

namespace nevo {

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent)
    , username_edit_(nullptr)
{
    setWindowTitle(tr("NEVO - Login"));
    setModal(true);
    setMinimumWidth(360);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(16);

    // Title label
    QLabel* title_label = new QLabel(tr("Connect to Server"), this);
    QFont title_font = title_label->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label->setFont(title_font);
    title_label->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(title_label);

    // Subtitle
    QLabel* subtitle = new QLabel(
        tr("Enter your username to join the voice server"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color: #c4c6d0; font-size: 12px;"));
    main_layout->addWidget(subtitle);

    main_layout->addSpacing(8);

    // Form layout for inputs
    QFormLayout* form_layout = new QFormLayout();
    form_layout->setSpacing(12);
    form_layout->setLabelAlignment(Qt::AlignLeft);

    username_edit_ = new QLineEdit(this);
    username_edit_->setPlaceholderText(tr("Enter username"));
    username_edit_->setMinimumHeight(32);
    form_layout->addRow(tr("Username:"), username_edit_);

    main_layout->addLayout(form_layout);
    main_layout->addSpacing(8);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
    buttons->button(QDialogButtonBox::Ok)->setMinimumHeight(36);
    buttons->button(QDialogButtonBox::Cancel)->setMinimumHeight(36);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    main_layout->addWidget(buttons);
}

QString LoginDialog::username() const
{
    return username_edit_->text();
}

} // namespace nevo
