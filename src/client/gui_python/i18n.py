"""
Global i18n setup for NEVO Python GUIs.

Monkey-patches QWidget.tr() to use JSON-based translations
and provides a language switching API.
"""

import os
import json
from PyQt5.QtWidgets import QWidget

# Global translator state
_current_strings: dict[str, str] = {}
_current_lang = "en"
_translations_dir = ""


def init_translations(translations_dir: str, default_lang: str = "en"):
    """Initialize the translation system."""
    global _translations_dir
    _translations_dir = translations_dir
    load_language(default_lang)

    # Monkey-patch QWidget.tr() to use our translator
    _original_tr = QWidget.tr

    def _patched_tr(self, text, *args, **kwargs):
        return _translate(text)

    QWidget.tr = _patched_tr


def load_language(lang: str) -> bool:
    """Load a translation file. Returns True on success."""
    global _current_strings, _current_lang

    if lang == "en":
        _current_strings = {}
        _current_lang = lang
        return True

    path = os.path.join(_translations_dir, f"{lang}.json")
    if not os.path.exists(path):
        return False

    try:
        with open(path, "r", encoding="utf-8") as f:
            _current_strings = json.load(f)
        _current_lang = lang
        return True
    except Exception:
        return False


def _translate(text: str) -> str:
    """Translate a string. Falls back to original."""
    return _current_strings.get(text, text)


def current_language() -> str:
    return _current_lang


def available_languages() -> list[str]:
    """List available language codes."""
    langs = ["en"]
    if os.path.isdir(_translations_dir):
        for f in os.listdir(_translations_dir):
            if f.endswith(".json"):
                code = f[:-5]
                if code not in langs:
                    langs.append(code)
    return langs


def language_display_name(lang: str) -> str:
    """Get display name for a language code."""
    names = {
        "en": "English",
        "zh_CN": "\u7b80\u4f53\u4e2d\u6587",
        "zh_TW": "\u7e41\u9ad4\u4e2d\u6587",
    }
    return names.get(lang, lang)


def save_language_preference(lang: str, app_name: str):
    """Save language preference to QSettings."""
    from PyQt5.QtCore import QSettings
    settings = QSettings("NEVO", app_name)
    settings.setValue("language", lang)


def load_language_preference(app_name: str) -> str:
    """Load language preference from QSettings."""
    from PyQt5.QtCore import QSettings
    settings = QSettings("NEVO", app_name)
    return settings.value("language", "en")
