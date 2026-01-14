#define AppName "EMStudio"
#define AppVersion "1.0.0"
#define AppExe "EMStudio.exe"
#define AppIco "emstudio.ico"

[Setup]
AppId={{A3B2E413-8A21-4C0C-92F1-0E20796C124B}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={pf}\EMStudio
DefaultGroupName=EMStudio
OutputBaseFilename=EMStudio-Setup
Compression=lzma
SolidCompression=yes
SetupIconFile={#AppIco}
UninstallDisplayIcon={app}\{#AppIco}

[Files]
Source: "..\build\dist\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
Source: "{#AppIco}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\EMStudio"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppIco}"
Name: "{commondesktop}\EMStudio"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppIco}"; Tasks: desktopicon
Name: "{group}\Uninstall EMStudio"; Filename: "{uninstallexe}"; IconFilename: "{app}\{#AppIco}"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"
