# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for NEVO Server Manager GUI."""

import sys
from PyInstaller.utils.hooks import collect_data_files, collect_submodules

block_cipher = None

# Collect qfluentwidgets data files (QSS themes, icons, etc.)
datas = collect_data_files('qfluentwidgets')

# Include translation files
import os
# SPECPATH = .spec 所在目录（src/server/gui_python），动态获取避免硬编码
spec_dir = os.path.abspath(SPECPATH)
translations_dir = os.path.join(spec_dir, 'translations')
if os.path.isdir(translations_dir):
    datas.append((translations_dir, 'translations'))

# Collect all qfluentwidgets submodules
hiddenimports = collect_submodules('qfluentwidgets')

# Ensure PyQt5 plugins are included
hiddenimports += [
    'PyQt5.sip',
    'PyQt5.QtCore',
    'PyQt5.QtGui',
    'PyQt5.QtWidgets',
]

a = Analysis(
    ['main.py'],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'tkinter', 'matplotlib', 'numpy', 'scipy',
        'PIL', 'sqlalchemy', 'django', 'flask',
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
    [],
    exclude_binaries=True,
    name='NEVO Server Manager',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    icon=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='NEVO Server Manager',
)
