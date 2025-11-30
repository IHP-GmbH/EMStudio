#define AppVersion "1.0.0"

[Setup]
AppName=EMStudio
AppVersion={#AppVersion}
DefaultDirName={pf}\EMStudio
DefaultGroupName=EMStudio
OutputBaseFilename=EMStudio-Setup
Compression=lzma
SolidCompression=yes

[Files]
Source: "..\build\dist\*"; DestDir: "{app}"; Flags: recursesubdirs

[Icons]
Name: "{group}\EMStudio"; Filename: "{app}\EMStudio.exe"
Name: "{commondesktop}\EMStudio"; Filename: "{app}\EMStudio.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"


