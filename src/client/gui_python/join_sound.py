import os
import sys
import time

_last_play_time = 0.0
_DEBOUNCE_SEC = 0.30
_players = []


def _get_bgm_dir():
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, 'bgm')
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'bgm'))


def _should_play():
    global _last_play_time
    now = time.time()
    if now - _last_play_time < _DEBOUNCE_SEC:
        return False
    _last_play_time = now
    return True


def _cleanup_players():
    global _players
    _players = [p for p in _players if p.state() != 0]


def _play_mp3(filepath):
    from PyQt5.QtMultimedia import QMediaPlayer, QMediaContent
    from PyQt5.QtCore import QUrl

    _cleanup_players()

    if not os.path.exists(filepath):
        return

    url = QUrl.fromLocalFile(os.path.abspath(filepath))
    player = QMediaPlayer()
    player.setMedia(QMediaContent(url))
    player.setVolume(60)
    player.play()

    _players.append(player)


def play_join_sound():
    if not _should_play():
        return
    filepath = os.path.join(_get_bgm_dir(), 'connect_v2.mp3')
    _play_mp3(filepath)


def play_disconnect_sound():
    if not _should_play():
        return
    filepath = os.path.join(_get_bgm_dir(), 'disconnect_v2.mp3')
    _play_mp3(filepath)