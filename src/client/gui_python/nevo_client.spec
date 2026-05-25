# -*- mode: python ; coding: utf-8 -*-

from PyInstaller.utils.hooks import collect_data_files, collect_submodules

block_cipher = None

datas = collect_data_files('qfluentwidgets')
datas += collect_data_files('mss')

import os
spec_dir = r'c:\Users\yzd20\Desktop\NEVO\src\client\gui_python'
translations_dir = os.path.join(spec_dir, 'translations')
if os.path.isdir(translations_dir):
    datas.append((translations_dir, 'translations'))

resources_dir = os.path.join(spec_dir, 'resources')
if os.path.isdir(resources_dir):
    datas.append((resources_dir, 'resources'))

bgm_dir = os.path.abspath(os.path.join(spec_dir, '..', '..', '..', 'bgm'))
if os.path.isdir(bgm_dir):
    datas.append((bgm_dir, 'bgm'))

hiddenimports = collect_submodules('qfluentwidgets')
hiddenimports += collect_submodules('mss')
hiddenimports += collect_submodules('av')

hiddenimports += [
    'PyQt5.sip',
    'PyQt5.QtCore',
    'PyQt5.QtGui',
    'PyQt5.QtWidgets',
    'PyQt5.QtMultimedia',
    'charset_normalizer',
    'sounddevice',
    'noisereduce',
    'numpy',
    'scipy',
    'scipy.signal',
    'scipy.fft',
    'scipy.special',
    'scipy.linalg',
    'scipy.sparse',
    'scipy.io',
    'scipy.ndimage',
    'joblib',
    'tqdm',
    'pynput',
    'pynput.keyboard',
    'pynput.keyboard._win32',
    'cryptography',
    'pynacl',
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
    'screen_share_dialog',
    'screen_audio_capture',
    'views.screen_share_view',
    'win32gui',
    'win32api',
    'win32con',
]

binaries = [
    (r'C:\vcpkg\installed\x64-windows\bin\opus.dll', '.'),
    (r'C:\Users\yzd20\Desktop\NEVO\build\bin\Release\libsodium.dll', '.'),
]

a = Analysis(
    ['main.py'],
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
        'matplotlib',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='NEVO',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    icon=None,
    version=None,
    onefile=True,
)
