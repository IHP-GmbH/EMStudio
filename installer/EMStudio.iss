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

Name: "{group}\Uninstall EMStudio"; \
    Filename: "{uninstallexe}"; \
    IconFilename: "{app}\{#AppIco}"

[Tasks]
Name: "desktopicon"; \
    Description: "Create a desktop icon"; \
    GroupDescription: "Additional icons:"

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
