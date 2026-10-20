; Installateur Windows de rhythm-ps5 (NSIS) : installation par utilisateur, sans
; droits administrateur, comme le client osu!. Raccourcis bureau et menu Démarrer,
; désinstalleur dans « Applications installées ».
; makensis -DVERSION=1.1.0 -DSRC=<dossier du jeu> -DOUT=<setup.exe> installer.nsi
Unicode true
Target amd64-unicode  ; installateur 64 bits, comme le jeu
!include "MUI2.nsh"

Name "rhythm-ps5"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\rhythm-ps5"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!define MUI_ICON "rhythm.ico"
!define MUI_UNICON "rhythm.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\rhythm-ps5.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\rhythm-ps5"

!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"

Section "Install"
  ; Le jeu peut être ouvert pendant une mise à jour : on attend qu'il se ferme.
  nsExec::Exec 'taskkill /IM rhythm-ps5.exe'
  Sleep 500
  SetOutPath "$INSTDIR"
  RMDir /r "$INSTDIR\assets"
  File /r "${SRC}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\rhythm-ps5.lnk" "$INSTDIR\rhythm-ps5.exe"
  CreateDirectory "$SMPROGRAMS\rhythm-ps5"
  CreateShortCut "$SMPROGRAMS\rhythm-ps5\rhythm-ps5.lnk" "$INSTDIR\rhythm-ps5.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "rhythm-ps5"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "rhythm-ps5"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\rhythm-ps5.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ; Mise à jour silencieuse (/S) lancée par le jeu : on le relance à la fin.
  IfSilent 0 +2
    Exec '"$INSTDIR\rhythm-ps5.exe"'
SectionEnd

Section "Uninstall"
  ; Les maps, skins et réglages (dossier utilisateur %APPDATA%) sont conservés.
  RMDir /r "$INSTDIR"
  Delete "$DESKTOP\rhythm-ps5.lnk"
  RMDir /r "$SMPROGRAMS\rhythm-ps5"
  DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd
