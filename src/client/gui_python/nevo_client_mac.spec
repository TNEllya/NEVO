# -*- mode: python ; coding: utf-8 -*-

from PyInstaller.utils.hooks import collect_data_files, collect_submodules

block_cipher = None

datas = collect_data_files('qfluentwidgets')
datas += collect_data_files('mss')

import os
spec_dir = os.path.dirname(os.path.abspath(SPECPATH))

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
    'pynput.keyboard._darwin',
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
    'views.update_dialog',
    'theme_manager',
    'updater',
    'Quartz',
    'CoreFoundation',
    'AppKit',
    'Foundation',
    'Cocoa',
]

binaries = []

if os.path.exists('/usr/local/lib/libopus.dylib'):
    binaries.append(('/usr/local/lib/libopus.dylib', '.'))
elif os.path.exists('/opt/homebrew/lib/libopus.dylib'):
    binaries.append(('/opt/homebrew/lib/libopus.dylib', '.'))

if os.path.exists('/usr/local/lib/libsodium.dylib'):
    binaries.append(('/usr/local/lib/libsodium.dylib', '.'))
elif os.path.exists('/opt/homebrew/lib/libsodium.dylib'):
    binaries.append(('/opt/homebrew/lib/libsodium.dylib', '.'))

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
    noarchive=False,
    cipher=block_cipher,
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
    onefile=True,
)

app = BUNDLE(
    exe,
    name='NEVO.app',
    icon=None,
    bundle_identifier='com.nevo.client',
    info_plist={
        'NSMicrophoneUsageDescription': 'NEVO requires microphone access for voice communication.',
        'NSScreenCaptureUsageDescription': 'NEVO requires screen recording access for screen sharing.',
    },
)