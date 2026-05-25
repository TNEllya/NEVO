# -*- coding: utf-8 -*-
"""
FileUploadDialog — 文件/图片选择对话框

封装 QFileDialog，支持文件类型过滤和大小校验。
"""

import os
from PyQt5.QtWidgets import QFileDialog, QMessageBox
from PyQt5.QtCore import QDir


# 允许的文件类型
IMAGE_FILTERS = (
    "Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg);;"
    "All Files (*.*)"
)

FILE_FILTERS = (
    "All Files (*.*)"
)

# 默认最大文件大小 (100 MB)
DEFAULT_MAX_SIZE_MB = 100


def select_file(parent=None, max_size_mb=DEFAULT_MAX_SIZE_MB):
    """打开文件选择对话框，返回 (file_path, file_size) 或 (None, 0)

    Args:
        parent: Qt 父窗口
        max_size_mb: 允许的最大文件大小 (MB)

    Returns:
        tuple: (file_path: str, file_size: int) 成功时
               (None, 0) 用户取消时
    """
    dialog = QFileDialog(
        parent,
        "Select File",
        str(QDir.homePath()),
        FILE_FILTERS,
    )
    dialog.setFileMode(QFileDialog.ExistingFile)
    dialog.setViewMode(QFileDialog.Detail)

    if dialog.exec_() != QFileDialog.Accepted:
        return None, 0

    file_path = dialog.selectedFiles()[0]
    if not file_path:
        return None, 0

    file_size = os.path.getsize(file_path)
    max_bytes = max_size_mb * 1024 * 1024

    if file_size > max_bytes:
        QMessageBox.warning(
            parent,
            "File Too Large",
            f"File '{os.path.basename(file_path)}' exceeds "
            f"the {max_size_mb} MB limit ({file_size / 1024 / 1024:.1f} MB).",
        )
        return None, 0

    return file_path, file_size


def select_image(parent=None, max_size_mb=DEFAULT_MAX_SIZE_MB):
    """打开图片选择对话框，返回 (file_path, file_size) 或 (None, 0)

    Args:
        parent: Qt 父窗口
        max_size_mb: 允许的最大文件大小 (MB)

    Returns:
        tuple: (file_path: str, file_size: int) 成功时
               (None, 0) 用户取消时
    """
    dialog = QFileDialog(
        parent,
        "Select Image",
        str(QDir.homePath()),
        IMAGE_FILTERS,
    )
    dialog.setFileMode(QFileDialog.ExistingFile)
    dialog.setViewMode(QFileDialog.Thumbnail)

    if dialog.exec_() != QFileDialog.Accepted:
        return None, 0

    file_path = dialog.selectedFiles()[0]
    if not file_path:
        return None, 0

    file_size = os.path.getsize(file_path)
    max_bytes = max_size_mb * 1024 * 1024

    if file_size > max_bytes:
        QMessageBox.warning(
            parent,
            "File Too Large",
            f"Image '{os.path.basename(file_path)}' exceeds "
            f"the {max_size_mb} MB limit ({file_size / 1024 / 1024:.1f} MB).",
        )
        return None, 0

    return file_path, file_size
