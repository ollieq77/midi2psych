#define MyAppName "Midi2Psych"
#define MyAppVersion "2.1"
#define MyAppExeName "midi2psych.exe"

[Setup]
AppId=Midi2Psych_MBOllz
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=Output
OutputBaseFilename=Midi2Psych-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=classic
LicenseFile=DOCUMENTS\LICENSE.rtf

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Main executable and DLLs
Source: "C:\Midi2Psych\midi2psych.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\Midi2Psych\libgcc_s_seh-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\Midi2Psych\libstdc++-6.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\Midi2Psych\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion

; Documentation folder
Source: "C:\Midi2Psych\DOCUMENTS\*"; DestDir: "{app}\DOCUMENTS"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\README"; Filename: "{app}\DOCUMENTS\README.html"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\DOCUMENTS\README.html"; Description: "View README"; Flags: postinstall shellexec skipifsilent unchecked