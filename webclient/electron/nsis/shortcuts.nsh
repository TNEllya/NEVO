; Custom NSIS page to let user choose whether to create desktop/start menu shortcuts.
; This file is included by electron-builder via the nsis.include option.

!ifndef BUILD_UNINSTALLER

!include nsDialogs.nsh

Var desktopShortcutCheckbox
Var startMenuShortcutCheckbox
Var createDesktopShortcutChoice
Var createStartMenuShortcutChoice

; Page creation function
Function shortcutsPageCreate
  nsDialogs::Create 1018
  Pop $0
  StrCmp $0 "error" shortcutsPageError

  ${NSD_CreateCheckbox} 0 0 100% 12u "创建桌面快捷方式"
  Pop $desktopShortcutCheckbox
  ${NSD_Check} $desktopShortcutCheckbox

  ${NSD_CreateCheckbox} 0 20u 100% 12u "创建开始菜单快捷方式"
  Pop $startMenuShortcutCheckbox
  ${NSD_Check} $startMenuShortcutCheckbox

  nsDialogs::Show
  Return

shortcutsPageError:
  Abort
FunctionEnd

; Page leave function - read checkbox states
Function shortcutsPageLeave
  ${NSD_GetState} $desktopShortcutCheckbox $createDesktopShortcutChoice
  ${NSD_GetState} $startMenuShortcutCheckbox $createStartMenuShortcutChoice
FunctionEnd

; Set default values (create shortcuts unless user unchecks them)
!macro customInit
  StrCpy $createDesktopShortcutChoice 1
  StrCpy $createStartMenuShortcutChoice 1
!macroend

; Insert the custom page after the directory page
!macro customPageAfterChangeDir
  Page custom shortcutsPageCreate shortcutsPageLeave
!macroend

; After electron-builder creates shortcuts, remove the ones the user unchecked.
!macro customInstall
  IntCmp $createDesktopShortcutChoice 0 removeDesktopShortcut
  Goto checkStartMenuShortcut
removeDesktopShortcut:
  IfFileExists "$newDesktopLink" 0 checkStartMenuShortcut
  Delete "$newDesktopLink"

checkStartMenuShortcut:
  IntCmp $createStartMenuShortcutChoice 0 removeStartMenuShortcut
  Goto endShortcuts
removeStartMenuShortcut:
  IfFileExists "$newStartMenuLink" 0 endShortcuts
  Delete "$newStartMenuLink"

endShortcuts:
!macroend

!endif
