!include "MUI2.nsh"

Name "VDO Cable"
!ifndef VERSION
!define VERSION "0.1.0"
!endif
!ifndef PRODUCT_VERSION_NUMERIC
!define PRODUCT_VERSION_NUMERIC "${VERSION}.0"
!endif
!ifndef BUILD_BIN_DIR
!define BUILD_BIN_DIR "build\bin"
!endif
!ifndef OUTFILE
!define OUTFILE "dist\vdocable-setup.exe"
!endif
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\VDO Cable"
InstallDirRegKey HKLM "Software\VDOCable" "InstallDir"
RequestExecutionLevel admin

VIProductVersion "${PRODUCT_VERSION_NUMERIC}"
VIAddVersionKey "ProductName" "VDO Cable"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "VDO Cable - Windows Audio Router for VDO.Ninja"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026"

!define MUI_ABORTWARNING
!define MUI_ICON "resources\vdocable.ico"
!define MUI_UNICON "resources\vdocable.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function EnsureVDOCableClosed
    nsExec::ExecToLog 'taskkill /F /T /IM vdocable.exe'
FunctionEnd

Function un.EnsureVDOCableClosed
    nsExec::ExecToLog 'taskkill /F /T /IM vdocable.exe'
FunctionEnd

Function RemoveExistingInstall
    IfFileExists "$INSTDIR\uninstall.exe" 0 done
    DetailPrint "Existing install detected; running previous uninstaller..."
    ExecWait '"$INSTDIR\uninstall.exe" /S _?=$INSTDIR'
done:
FunctionEnd

Section "Install"
    Call EnsureVDOCableClosed
    Call RemoveExistingInstall
    Call EnsureVDOCableClosed
    Sleep 600

    SetOutPath "$INSTDIR"

    Delete /REBOOTOK "$INSTDIR\vdocable.exe"

    ClearErrors
    File "${BUILD_BIN_DIR}\vdocable.exe"
    IfErrors 0 +3
    MessageBox MB_ICONSTOP|MB_OK "VDO Cable is still running or files are locked. Close the app and retry setup."
    Abort
    File /nonfatal "${BUILD_BIN_DIR}\*.dll"
    File /nonfatal "${BUILD_BIN_DIR}\vdocable.ico"
    File /nonfatal "${BUILD_BIN_DIR}\RELEASE-NOTES.txt"

    SetOutPath "$INSTDIR\platforms"
    File "${BUILD_BIN_DIR}\platforms\qwindows.dll"

    SetOutPath "$INSTDIR\styles"
    File /nonfatal "${BUILD_BIN_DIR}\styles\qmodernwindowsstyle.dll"

    SetOutPath "$INSTDIR"
    CreateDirectory "$SMPROGRAMS\VDO Cable"
    CreateShortcut "$SMPROGRAMS\VDO Cable\VDO Cable.lnk" "$INSTDIR\vdocable.exe" "" "$INSTDIR\vdocable.exe"
    CreateShortcut "$SMPROGRAMS\VDO Cable\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    CreateShortcut "$DESKTOP\VDO Cable.lnk" "$INSTDIR\vdocable.exe" "" "$INSTDIR\vdocable.exe"

    WriteRegStr HKLM "Software\VDOCable" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable" "DisplayName" "VDO Cable"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable" "DisplayIcon" "$INSTDIR\vdocable.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable" "Publisher" "VDO.Ninja"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable" "NoRepair" 1

    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    Call un.EnsureVDOCableClosed

    Delete "$INSTDIR\*.*"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    Delete "$INSTDIR\uninstall.exe"

    RMDir "$INSTDIR"

    Delete "$SMPROGRAMS\VDO Cable\VDO Cable.lnk"
    Delete "$SMPROGRAMS\VDO Cable\Uninstall.lnk"
    Delete "$DESKTOP\VDO Cable.lnk"
    RMDir "$SMPROGRAMS\VDO Cable"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VDOCable"
    DeleteRegKey HKLM "Software\VDOCable"
SectionEnd
