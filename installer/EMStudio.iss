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
ChangesEnvironment=yes

[Files]
; Main application + Qt runtime
Source: "..\build\dist\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

; Application icon
Source: "{#AppIco}"; DestDir: "{app}"; Flags: ignoreversion

; KLayout integration script
Source: "..\scripts\klEmsDriver.py"; DestDir: "{app}\scripts"; Flags: ignoreversion

; KLayout launcher (bat)
Source: "..\scripts\KLayout.bat"; DestDir: "{app}\scripts"; Flags: ignoreversion

; KLayout icon (as PNG) - you keep it next to EMStudio.iss
Source: "KLayout.png"; DestDir: "{app}"; Flags: ignoreversion

; Logo used by KLayout action
Source: "..\icons\logo.png"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\EMStudio"; \
    Filename: "{app}\{#AppExe}"; \
    IconFilename: "{app}\{#AppIco}"

Name: "{commondesktop}\EMStudio"; \
    Filename: "{app}\{#AppExe}"; \
    IconFilename: "{app}\{#AppIco}"; \
    Tasks: desktopicon

; KLayout integration shortcut (Start Menu)
Name: "{group}\EMStudio in KLayout"; \
    Filename: "{app}\scripts\KLayout.bat"; \
    WorkingDir: "{app}\scripts"; \
    IconFilename: "{app}\KLayout.png"

; KLayout integration shortcut (Desktop)
Name: "{commondesktop}\EMStudio in KLayout"; \
    Filename: "{app}\scripts\KLayout.bat"; \
    WorkingDir: "{app}\scripts"; \
    IconFilename: "{app}\KLayout.png"; \
    Tasks: desktopicon_klayout

Name: "{group}\Uninstall EMStudio"; \
    Filename: "{uninstallexe}"; \
    IconFilename: "{app}\{#AppIco}"

[Tasks]
Name: "desktopicon"; \
    Description: "Create a desktop icon"; \
    GroupDescription: "Additional icons:"

Name: "desktopicon_klayout"; \
    Description: "Create a desktop icon (EMStudio in KLayout)"; \
    GroupDescription: "Additional icons:"; \
    Flags: unchecked

Name: "addtopath"; \
    Description: "Add EMStudio to PATH (recommended)"; \
    GroupDescription: "System integration:"; \
    Flags: checkedonce

[Registry]
; Add {app} to PATH (system-wide)
Root: HKLM; \
Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
ValueType: expandsz; \
ValueName: "Path"; \
ValueData: "{olddata};{app}"; \
Tasks: addtopath; \
Check: NeedsAddPath

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function NeedsAddPath(): Boolean;
var
  Path: string;
begin
  if not RegQueryStringValue(
    HKLM,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path',
    Path
  ) then
  begin
    Result := True;
    exit;
  end;

  Result := Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Path)) = 0;
end;
