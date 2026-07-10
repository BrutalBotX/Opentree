; OpenTree Windows installer scaffold for Inno Setup 6

#define MyAppName "OpenTree"
#define MyAppVersion "0.3.1"
#define MyAppPublisher "OpenTree Contributors"
#define MyAppExeName "OpenTree.exe"
#define MyBuildDir "..\..\build-msvc"
#define MyIconFile "..\assets\opentree.ico"

[Setup]
AppId={{6B4FEC08-8A44-4B93-9A7F-20F6D8D6B44A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/
AppSupportURL=https://github.com/
AppUpdatesURL=https://github.com/
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=OpenTree-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#MyIconFile}
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=lowest
ChangesAssociations=no
DisableDirPage=no
DisableReadyMemo=no
LicenseFile=..\LICENSE
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Installer
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
WizardImageStretch=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#MyBuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
