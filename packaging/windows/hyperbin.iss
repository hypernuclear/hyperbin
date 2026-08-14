; Inno Setup script for hyperbin — the direct-download Windows installer.
;
; Expects a staging directory that windeployqt has already populated,
; with WinSparkle.dll beside the executable. CI passes both in:
;
;   iscc /DAppVersion=0.1.0 /DStagingDir=dist\windows ^
;        /Obuild packaging\windows\hyperbin.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StagingDir
  #define StagingDir "dist\windows"
#endif

[Setup]
AppName=hyperbin
AppVersion={#AppVersion}
AppVerName=hyperbin {#AppVersion}
AppPublisher=Hypernuclear LLC
AppPublisherURL=https://hypernuclear.com
DefaultDirName={autopf}\hyperbin
DefaultGroupName=hyperbin
OutputBaseFilename=hyperbin-setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
SetupIconFile=..\..\resources\hyperbin.ico
UninstallDisplayIcon={app}\hyperbin.exe
ArchitecturesAllowed=x64compatible arm64
ArchitecturesInstallIn64BitMode=x64compatible arm64
WizardStyle=modern
; No admin prompt. This installs per-user into the profile, which is all
; a notification-area app needs, and an elevation prompt on a free
; giveaway is a reason to close the installer.
PrivilegesRequired=lowest
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Ticked by default, unlike the desktop icon: hyperbin draws on the
; Recycle Bin and has no window, so one that is not running is one the
; user will assume is broken.
Name: "startup"; Description: "Start hyperbin when I sign in"; \
  GroupDescription: "Startup"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything windeployqt staged. The MSIX-only pieces are excluded so a
; user installing the EXE does not get a manifest and a set of Store
; tiles dropped into Program Files.
Source: "{#StagingDir}\*"; DestDir: "{app}"; \
  Flags: ignoreversion recursesubdirs createallsubdirs; \
  Excludes: "AppxManifest.xml,Assets"

[Icons]
Name: "{group}\hyperbin"; Filename: "{app}\hyperbin.exe"
Name: "{autodesktop}\hyperbin"; Filename: "{app}\hyperbin.exe"; Tasks: desktopicon
Name: "{userstartup}\hyperbin"; Filename: "{app}\hyperbin.exe"; Tasks: startup

[Run]
Filename: "{app}\hyperbin.exe"; Description: "{cm:LaunchProgram,hyperbin}"; \
  Flags: nowait postinstall skipifsilent
