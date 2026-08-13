; 中文显示测试安装包脚本
; 用于验证 NSIS Unicode + UTF-8 BOM 编码修复效果

Unicode true

!include "MUI2.nsh"

; --- 应用程序信息 ---
!define APP_NAME "中文测试程序"
!define APP_VERSION "1.0.0"
!define APP_PUBLISHER "测试公司"
!define APP_EXE "test.exe"

Name "${APP_NAME}"
OutFile "C:\Users\yzd20\Desktop\Project\NEVO\out\ChineseTest-Setup-1.0.0.exe"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
RequestExecutionLevel admin

; --- MUI 设置 ---
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "欢迎使用中文测试程序"
!define MUI_WELCOMEPAGE_TEXT "这是一个用于验证安装包中文显示是否正常的测试程序。请点击“下一步”继续安装。"

!define MUI_DIRECTORYPAGE_TEXT_TOP "请选择安装目录。默认路径为：$PROGRAMFILES64\中文测试程序"
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "目标文件夹"

!define MUI_INSTFILESPAGE_FINISHHEADER_TITLE "安装完成"
!define MUI_INSTFILESPAGE_FINISHHEADER_TEXT "中文测试程序已成功安装到您的计算机。"

!define MUI_FINISHPAGE_TITLE "安装成功"
!define MUI_FINISHPAGE_TEXT "感谢您安装中文测试程序。您可以点击“完成”退出安装向导。"

SetCompressor lzma

; --- 页面 ---
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; --- 语言 ---
!insertmacro MUI_LANGUAGE "SimpChinese"

; --- 安装区段 ---
Section "主程序" SecMain
  SectionIn RO

  SetOutPath "$INSTDIR"

  ; 创建一个空的可执行文件用于测试
  FileOpen $0 "$INSTDIR\${APP_EXE}" w
  FileWrite $0 ""
  FileClose $0

  ; 创建开始菜单快捷方式
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\启动中文测试程序.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\卸载中文测试程序.lnk" "$INSTDIR\uninst.exe" "" "$INSTDIR\uninst.exe" 0

  ; 创建桌面快捷方式
  CreateShortCut "$DESKTOP\中文测试程序.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

  ; 注册表信息
  WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayVersion" "${APP_VERSION}"

  ; 卸载程序
  WriteUninstaller "$INSTDIR\uninst.exe"
SectionEnd

; --- 卸载区段 ---
Section "un.卸载"
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\uninst.exe"

  RMDir /r "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\启动中文测试程序.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\卸载中文测试程序.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  Delete "$DESKTOP\中文测试程序.lnk"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
  DeleteRegKey HKLM "Software\${APP_NAME}"
SectionEnd