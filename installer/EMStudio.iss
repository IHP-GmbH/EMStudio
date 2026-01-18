#define AppName "EMStudio"
#define AppVersion "1.0.0"
#define AppExe "EMStudio.exe"
#define AppIco "emstudio.ico"
#define KLayoutIco "KLayout.ico"

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

; KLayout icon (ICO) - stored next to EMStudio.exe
Source: "{#KLayoutIco}"; DestDir: "{app}"; Flags: ignoreversion

; Logo used by KLayout action (used by the Python macro)
Source: "..\icons\logo.png"; DestDir: "{app}\scripts"; Flags: ignoreversion

[Icons]
; Main EMStudio shortcut (Start Menu)
Name: "{group}\EMStudio"; \
    Filename: "{app}\{#AppExe}"; \
    IconFilename: "{app}\{#AppIco}"

; Main EMStudio shortcut (Desktop) - current user desktop
Name: "{autodesktop}\EMStudio"; \
    Filename: "{app}\{#AppExe}"; \
    IconFilename: "{app}\{#AppIco}"; \
    Tasks: desktopicon

; KLayout integration shortcut (Start Menu)
Name: "{group}\KLayout_EMStudio"; \
    Filename: "{app}\scripts\KLayout.bat"; \
    WorkingDir: "{app}\scripts"; \
    IconFilename: "{app}\{#KLayoutIco}"

; KLayout integration shortcut (Desktop) - current user desktop
Name: "{autodesktop}\KLayout_EMStudio"; \
    Filename: "{app}\scripts\KLayout.bat"; \
    WorkingDir: "{app}\scripts"; \
    IconFilename: "{app}\{#KLayoutIco}"; \
    Tasks: desktopicon

; Uninstall shortcut
Name: "{group}\Uninstall EMStudio"; \
    Filename: "{uninstallexe}"; \
    IconFilename: "{app}\{#AppIco}"

[Tasks]
Name: "desktopicon"; \
    Description: "Create desktop icons (EMStudio + KLayout_EMStudio)"; \
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

  Result := Pos(
    Lowercase(ExpandConstant('{app}')),
    Lowercase(Path)
  ) = 0;
end;
