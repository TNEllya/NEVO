!define PRODUCT_NAME "NEVO Client"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "NEVO"
!define PRODUCT_EXE "nevo_client_ui.exe"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "c:\Users\yzd20\Desktop\NEVO\dist\NEVO-Client-Setup-${PRODUCT_VERSION}.exe"
InstallDir "$PROGRAMFILES\NEVO\Client"
InstallDirRegKey HKLM "Software\NEVO\Client" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"

Section "MainSection" SEC01
  SetOutPath "$INSTDIR"
  SetOverwrite on

  File /r "c:\Users\yzd20\Desktop\NEVO\dist\NEVO-Client\*.*"

  CreateShortCut "$DESKTOP\NEVO Client.lnk" "$INSTDIR\${PRODUCT_EXE}"
  CreateDirectory "$SMPROGRAMS\NEVO"
  CreateShortCut "$SMPROGRAMS\NEVO\NEVO Client.lnk" "$INSTDIR\${PRODUCT_EXE}"
  CreateShortCut "$SMPROGRAMS\NEVO\Uninstall NEVO Client.lnk" "$INSTDIR\uninst.exe"
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "Software\NEVO\Client" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NEVO-Client" \
    "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NEVO-Client" \
    "UninstallString" "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NEVO-Client" \
    "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NEVO-Client" \
    "Publisher" "${PRODUCT_PUBLISHER}"
SectionEnd

Section Uninstall
  Delete "$INSTDIR\*.*"
  RMDir /r "$INSTDIR"
  Delete "$DESKTOP\NEVO Client.lnk"
  Delete "$SMPROGRAMS\NEVO\NEVO Client.lnk"
  Delete "$SMPROGRAMS\NEVO\Uninstall NEVO Client.lnk"
  RMDir "$SMPROGRAMS\NEVO"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NEVO-Client"
  DeleteRegKey HKLM "Software\NEVO\Client"
SectionEnd
