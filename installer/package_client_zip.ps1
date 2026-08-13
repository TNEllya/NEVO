# NEVO 客户端 ZIP 打包脚本
# 将 PyInstaller 生成的 NEVO.exe 打包为绿色便携版 ZIP 安装包
#
# 用法:
#   .\installer\package_client_zip.ps1
#   .\installer\package_client_zip.ps1 -Version "0.2.0"
#   .\installer\package_client_zip.ps1 -ExePath "C:\custom\path\NEVO.exe"
#
# 输出:
#   C:\Users\yzd20\Desktop\Project\NEVO\out\NEVO-Portable-<version>-win64.zip

param(
    [string]$Version = "0.1.0",
    [string]$ExePath = "C:\Users\yzd20\Desktop\Project\NEVO\src\client\gui_python\dist\NEVO.exe",
    [string]$OutDir = "C:\Users\yzd20\Desktop\Project\NEVO\out",
    [string]$AssetsDir = "C:\Users\yzd20\Desktop\Project\NEVO\installer\zip_assets"
)

$ErrorActionPreference = "Stop"

$AppName = "NEVO"
$PackageName = "$AppName-Portable-$Version-win64"
$TempDir = Join-Path $env:TEMP $PackageName
$OutFile = Join-Path $OutDir "$PackageName.zip"

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
}

function Write-ErrorAndExit {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
    exit 1
}

Write-Info "开始打包 NEVO 客户端 ZIP 安装包..."
Write-Info "版本: $Version"
Write-Info "源文件: $ExePath"
Write-Info "输出目录: $OutDir"

# 检查源文件
if (-not (Test-Path $ExePath)) {
    Write-ErrorAndExit "找不到客户端可执行文件: $ExePath`n请先运行 PyInstaller 构建客户端。"
}

# 检查资源文件
if (-not (Test-Path $AssetsDir)) {
    Write-ErrorAndExit "找不到资源目录: $AssetsDir"
}

# 创建输出目录
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

# 清理临时目录
if (Test-Path $TempDir) {
    Remove-Item -Path $TempDir -Recurse -Force
}
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

# 复制主程序
Write-Info "复制 NEVO.exe 到临时目录..."
Copy-Item -Path $ExePath -Destination (Join-Path $TempDir "NEVO.exe") -Force

# 复制资源文件
Write-Info "复制启动脚本和说明文件..."
Get-ChildItem -Path $AssetsDir -File | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $TempDir -Force
}

# 生成版本信息文件
$VersionInfo = @"
NEVO Portable $Version
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Platform: Windows x64
"@
$VersionInfo | Out-File -FilePath (Join-Path $TempDir "version.txt") -Encoding UTF8 -Force

# 创建 ZIP 压缩包
Write-Info "创建 ZIP 压缩包: $OutFile"
if (Test-Path $OutFile) {
    Remove-Item -Path $OutFile -Force
}

Compress-Archive -Path "$TempDir\*" -DestinationPath $OutFile -Force

# 清理临时目录
Remove-Item -Path $TempDir -Recurse -Force

# 验证输出
if (Test-Path $OutFile) {
    $fileSize = (Get-Item $OutFile).Length
    $sizeMB = [math]::Round($fileSize / 1MB, 2)
    Write-Info "打包完成!"
    Write-Info "输出文件: $OutFile"
    Write-Info "文件大小: $sizeMB MB"
} else {
    Write-ErrorAndExit "ZIP 文件创建失败"
}