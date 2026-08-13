# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['gateway.py'],
    pathex=['../src/client/gui_python'],
    binaries=[],
    datas=[
        ('css', 'css'),
        ('js', 'js'),
        ('sounds', 'sounds'),
        ('index.html', '.'),
    ],
    hiddenimports=[
        'nevo_client', 'nevo_wire',
        'nacl', 'nacl.public', 'nacl.bindings',
        # nacl.public 经由 cffi 调用 libsodium，冻结时必须包含 C 后端
        'cffi', '_cffi_backend',
        'voice_crypto',
        'proto', 'proto.voice_pb2', 'proto.video_pb2',
        'proto.control_pb2', 'proto.common_pb2',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='nevo_gateway',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='nevo_gateway',
)
