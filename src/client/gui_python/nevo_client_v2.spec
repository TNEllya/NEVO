# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for NEVO v2 client (Discord-style GUI)."""

from PyInstaller.utils.hooks import collect_data_files, collect_submodules
import sys
import os

block_cipher = None

datas = collect_data_files('qfluentwidgets')
datas += collect_data_files('mss')

# SPECPATH = .spec 所在目录（src/client/gui_python）
spec_dir = os.path.abspath(SPECPATH)
translations_dir = os.path.join(spec_dir, 'translations')
if os.path.isdir(translations_dir):
    datas.append((translations_dir, 'translations'))

resources_dir = os.path.join(spec_dir, 'resources')
if os.path.isdir(resources_dir):
    datas.append((resources_dir, 'resources'))

version_file = os.path.join(spec_dir, 'version.txt')
if os.path.isfile(version_file):
    datas.append((version_file, '.'))

bgm_dir = os.path.abspath(os.path.join(spec_dir, '..', '..', '..', 'bgm'))
if os.path.isdir(bgm_dir):
    datas.append((bgm_dir, 'bgm'))

hiddenimports = collect_submodules('qfluentwidgets')
hiddenimports += collect_submodules('mss')
hiddenimports += collect_submodules('av')
hiddenimports += collect_submodules('requests')

datas += collect_data_files('requests')

hiddenimports += [
    'PyQt5.sip',
    'PyQt5.QtCore',
    'PyQt5.QtGui',
    'PyQt5.QtWidgets',
    'PyQt5.QtSvg',
    'charset_normalizer',
    'sounddevice',
    'numpy',
    'pynput',
    'pynput.keyboard',
    'pynput.keyboard._win32',
    'cryptography',
    'nacl',
    'nacl.public',
    'nacl.bindings',
    'nacl.bindings.crypto_aead',
    'nacl.bindings.crypto_box',
    'nacl.bindings.crypto_secretbox',
    'opuslib',
    'google.protobuf',
    'google.protobuf.descriptor',
    'google.protobuf.descriptor_pool',
    'google.protobuf.symbol_database',
    'google.protobuf.internal',
    'google.protobuf.internal.builder',
    'screen_capture',
    'video_encoder',
    'video_engine',
    'video_call_engine',
    'camera_capture',
    'screen_share_dialog',
    'screen_audio_capture',
    'wasapi_loopback',
    'views.screen_share_view',
    'views.update_dialog',
    'views.video_call_dialog',
    'views.incoming_call_dialog',
    'theme_manager',
    'updater',
    'win32gui',
    'win32api',
    'win32con',
    'cv2',
    # ---- v2 package modules (explicit to be safe) ----
    'v2',
    'v2.theme',
    'v2.sidebar',
    'v2.chat_panel',
    'v2.voice_users_panel',
    'v2.video_call_window',
    'v2.settings_window',
    'v2.main_window',
]

binaries = []
# opus.dll: opuslib 编解码库
_opus_dll = os.path.join(os.path.dirname(sys.executable), 'opus.dll')
if not os.path.isfile(_opus_dll):
    _opus_dll = os.path.join(os.path.dirname(sys.executable), 'Scripts', 'opus.dll')
if os.path.isfile(_opus_dll):
    binaries.append((_opus_dll, '.'))
# libsodium.dll: 若存在则打包（PyNaCl 通常静态链接）
_libsod = os.path.join(os.path.dirname(sys.executable), 'libsodium.dll')
if not os.path.isfile(_libsod):
    _libsod = os.path.join(os.path.dirname(sys.executable), 'Scripts', 'libsodium.dll')
if os.path.isfile(_libsod):
    binaries.append((_libsod, '.'))

a = Analysis(
    ['main_v2.py'],
    pathex=[spec_dir],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'tkinter', 'PIL', 'sqlalchemy', 'django', 'flask',
        'grpcio', 'grpc_tools',
        'matplotlib', 'torch', 'tensorflow', 'pandas', 'datasets',
        'nltk', 'transformers', 'sklearn', 'scipy', 'noisereduce',
        'joblib', 'tqdm', 'librosa', 'sympy', 'pyarrow',
        'uvicorn', 'fastapi', 'IPython', 'jupyter', 'notebook',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

_icon_path = os.path.join(spec_dir, 'resources', 'nevo_icon.ico')
_icon_arg = _icon_path if os.path.isfile(_icon_path) else None

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='NEVO-v2',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    icon=_icon_arg,
    version=None,
    onefile=True,
)
