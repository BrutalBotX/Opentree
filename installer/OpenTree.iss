; OpenTree Windows installer (Inno Setup 6)
;
; Build with:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\OpenTree.iss
; or pass a version explicitly (used by CI on tags):
;   ISCC.exe /DMyAppVersion=0.13.0 installer\OpenTree.iss

#ifndef MyAppVersion
  #define MyAppVersion "0.13.3"
#endif

#define MyAppName "OpenTree"
#define MyAppPublisher "BrutalBot"
#define MyAppURL "https://github.com/BrutalBotX/Opentree"
#define MyAppExeName "OpenTree.exe"
#define MyBuildDir "stage"
#define MyIconFile "..\assets\opentree.ico"

[Setup]
AppId={{6B4FEC08-8A44-4B93-9A7F-20F6D8D6B44A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=OpenTree-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#MyIconFile}
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=no
DisableDirPage=no
DisableReadyMemo=no
LicenseFile=..\LICENSE
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} installer
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
WizardImageStretch=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Source is the staged runtime folder (see stage_runtime.ps1): OpenTree.exe, the Qt runtime,
; the Everything SDK and the WebEngine runtime, without any build intermediates.
Source: "{#MyBuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; QtWebEngine writes its cache next to the executable.
Type: filesandordirs; Name: "{app}\QtWebEngine"
