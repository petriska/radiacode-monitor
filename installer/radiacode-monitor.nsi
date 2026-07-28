; RadiaCode Monitor — NSIS installer (x64)
;
; Built by scripts/package-windows.ps1 which stages the app + Qt + libusb, then:
;   makensis /DPRODUCT_VERSION=x.y.z /DSTAGE_DIR=... /DOUT_DIR=... radiacode-monitor.nsi
;
; Requires NSIS 3.x with Modern UI 2.

Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

; ---------------------------------------------------------------------------
; Product metadata (overridable from the command line)
; ---------------------------------------------------------------------------
!ifndef PRODUCT_NAME
  !define PRODUCT_NAME "RadiaCode Monitor"
!endif
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.2.0"
!endif
!ifndef PUBLISHER
  !define PUBLISHER "RadiaCode Monitor contributors"
!endif
!ifndef STAGE_DIR
  !define STAGE_DIR "..\dist\stage"
!endif
!ifndef OUT_DIR
  !define OUT_DIR "..\dist"
!endif
!ifndef EXE_NAME
  !define EXE_NAME "radiacode-monitor.exe"
!endif

!define PRODUCT_REGKEY "Software\${PRODUCT_NAME}"
!define UNINSTALL_REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

Name "${PRODUCT_NAME}"
OutFile "${OUT_DIR}\RadiaCodeMonitor-${PRODUCT_VERSION}-win64.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_REGKEY}" "InstallLocation"
RequestExecutionLevel admin

VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey /LANG=0 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=0 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=0 "FileDescription" "${PRODUCT_NAME} installer"
VIAddVersionKey /LANG=0 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=0 "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=0 "LegalCopyright" "Copyright (c) ${PUBLISHER}"

; ---------------------------------------------------------------------------
; Modern UI
; ---------------------------------------------------------------------------
!define MUI_ABORTWARNING
; Same app icon as the .exe (relative to this .nsi under installer/)
!define MUI_ICON "..\icons\radiacode-monitor.ico"
!define MUI_UNICON "..\icons\radiacode-monitor.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE_NAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_NAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Slovak"
!insertmacro MUI_LANGUAGE "Czech"

; ---------------------------------------------------------------------------
; Install
; ---------------------------------------------------------------------------
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "This installer requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "Application" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Entire staged tree (exe, Qt DLLs/plugins, QtRadiacode, libusb, …)
  File /r "${STAGE_DIR}\*.*"

  ; Start Menu
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" \
    "$INSTDIR\${EXE_NAME}" "" "$INSTDIR\${EXE_NAME}" 0
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" \
    "$INSTDIR\Uninstall.exe"

  ; Optional desktop shortcut
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" \
    "$INSTDIR\${EXE_NAME}" "" "$INSTDIR\${EXE_NAME}" 0

  ; Uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Registry (Add/Remove Programs)
  WriteRegStr HKLM "${PRODUCT_REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_REGKEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${UNINSTALL_REGKEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_REGKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINSTALL_REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_REGKEY}" "DisplayIcon" "$INSTDIR\${EXE_NAME}"
  WriteRegStr HKLM "${UNINSTALL_REGKEY}" "UninstallString" \
    '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_REGKEY}" "QuietUninstallString" \
    '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_REGKEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINSTALL_REGKEY}" "EstimatedSize" "$0"
SectionEnd

; ---------------------------------------------------------------------------
; Uninstall
; ---------------------------------------------------------------------------
Section "Uninstall"
  SetRegView 64

  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

  ; Remove install tree (plugins, Qt DLLs, app)
  RMDir /r "$INSTDIR"

  DeleteRegKey HKLM "${UNINSTALL_REGKEY}"
  DeleteRegKey HKLM "${PRODUCT_REGKEY}"
SectionEnd
