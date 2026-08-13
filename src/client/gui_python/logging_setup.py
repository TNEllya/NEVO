# -*- coding: utf-8 -*-
"""
客户端日志引导（main.py / main_v2.py 共用，消除重复实现）

职责：
  1. 计算可写的日志目录（打包安装后 cwd 可能不可写 → 优先 APPDATA）
  2. 初始化 logger（文件 + 控制台 handler）
  3. TeeStream 将 stdout/stderr 重定向到日志文件
  4. 全局异常钩子：未捕获异常写 crash 日志后交还原 excepthook

安全约束：所有文件写入一律通过 logging.FileHandler 完成（不直接 open()）；
日志/崩溃文件路径在模块加载时一次性计算并校验为日志目录内的固定常量
（文件名均为字面量，不接收任何外部路径输入）。
"""
import logging
import os
import sys
import traceback
from datetime import datetime


def log_directory() -> str:
    """返回日志目录：打包运行时用 APPDATA（用户可写），开发时用脚本目录。"""
    if getattr(sys, "frozen", False):
        base = os.environ.get("APPDATA", os.path.dirname(sys.executable))
        return os.path.join(base, "nevo-web-client")
    return os.path.dirname(os.path.abspath(__file__))


# 模块级：一次性计算并校验固定日志路径（文件名均为字面量，禁止外部输入）
_LOG_DIR = os.path.realpath(log_directory())
_LOG_FILES = {
    "v1": os.path.realpath(os.path.join(_LOG_DIR, "nevo_client.log")),
    "v2": os.path.realpath(os.path.join(_LOG_DIR, "nevo_client_v2.log")),
}
_CRASH_FILES = {
    "v1": os.path.realpath(os.path.join(_LOG_DIR, "crash.log")),
    "v2": os.path.realpath(os.path.join(_LOG_DIR, "crash_v2.log")),
}
for _p in list(_LOG_FILES.values()) + list(_CRASH_FILES.values()):
    if not _p.startswith(_LOG_DIR + os.sep):
        raise ValueError("日志路径越界: {}".format(_p))

# stdout/stderr 重定向与崩溃记录专用 logger（仅 FileHandler，不接 StreamHandler，
# 避免重定向后的 stdout/stderr 与日志写入互相递归）
_tee_logger = logging.getLogger("nevo_stdout_tee")
_tee_logger.propagate = False
_crash_logger = logging.getLogger("nevo_crash")
_crash_logger.propagate = False


class TeeStream:
    """同时将输出写入原始流和日志文件（原始流可能为 None）。"""

    def __init__(self, original_stream):
        self.original_stream = original_stream

    def write(self, text):
        if self.original_stream is not None:
            try:
                self.original_stream.write(text)
            except Exception:
                pass
        try:
            for line in text.splitlines(True):
                if line.endswith("\n") and line.strip():
                    _tee_logger.info("%s", line.rstrip("\n"))
        except Exception:
            pass
        return len(text)

    def flush(self):
        if self.original_stream is not None:
            try:
                self.original_stream.flush()
            except Exception:
                pass


def setup_client_logging(variant: str = "v1"):
    """初始化日志并返回 logger。

    :param variant: "v1" 或 "v2"（仅用于选择模块内固定路径常量）
    :return: 配置好的 logger（名称 "nevo_client"）
    """
    os.makedirs(_LOG_DIR, exist_ok=True)
    log_file = _LOG_FILES.get(variant, _LOG_FILES["v1"])
    crash_file = _CRASH_FILES.get(variant, _CRASH_FILES["v1"])

    # stdout/stderr 重定向日志（mode="w"：每次启动截断重建）
    if not _tee_logger.handlers:
        _tee_handler = logging.FileHandler(log_file, mode="w", encoding="utf-8")
        _tee_handler.setLevel(logging.DEBUG)
        _tee_handler.setFormatter(logging.Formatter("%(asctime)s %(message)s",
                                                    datefmt="%H:%M:%S"))
        _tee_logger.addHandler(_tee_handler)

    # 崩溃日志
    if not _crash_logger.handlers:
        _crash_handler = logging.FileHandler(crash_file, mode="a", encoding="utf-8")
        _crash_handler.setLevel(logging.CRITICAL)
        _crash_handler.setFormatter(logging.Formatter(
            "%(asctime)s [%(levelname)s] %(message)s", datefmt="%Y-%m-%d %H:%M:%S"))
        _crash_logger.addHandler(_crash_handler)

    _tee_logger.info("=== NEVO Client Log Started at %s ===",
                     datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

    logger = logging.getLogger("nevo_client")
    logger.setLevel(logging.DEBUG)

    if not logger.handlers:
        file_handler = logging.FileHandler(log_file, mode="a", encoding="utf-8")
        file_handler.setLevel(logging.DEBUG)
        file_handler.setFormatter(logging.Formatter(
            "%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S"))
        logger.addHandler(file_handler)

        # 控制台处理器（仅在 stdout 非 None 时添加，避免 PyInstaller console=False 崩溃）
        if sys.stdout is not None:
            console_handler = logging.StreamHandler(sys.stdout)
            console_handler.setLevel(logging.DEBUG)
            console_handler.setFormatter(logging.Formatter("[%(levelname)s] %(message)s"))
            logger.addHandler(console_handler)

    # 重定向 stdout/stderr 到日志（先建好 handler 再重定向，避免递归）
    if sys.stdout is not None and not isinstance(sys.stdout, TeeStream):
        sys.stdout = TeeStream(sys.stdout)
    if sys.stderr is not None and not isinstance(sys.stderr, TeeStream):
        sys.stderr = TeeStream(sys.stderr)

    # 全局异常钩子
    def _global_exception_hook(exc_type, exc_value, exc_tb):
        msg = "".join(traceback.format_exception(exc_type, exc_value, exc_tb))
        try:
            print("[FATAL] Unhandled exception:\n{}".format(msg))
        except Exception:
            pass
        logger.critical("Unhandled exception:\n%s", msg)
        _crash_logger.critical("=== CRASH ===\n%s", msg)
        sys.__excepthook__(exc_type, exc_value, exc_tb)

    sys.excepthook = _global_exception_hook

    return logger
