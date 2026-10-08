; BadBT Keystroke Injection Shield — Inno Setup Script
; Production Windows Installer Configuration

#define MyAppName "BadBT Keystroke Injection Shield"
#define MyAppVersion "1.0.1"
#define MyAppPublisher "BadBT Security"
#define MyAppExeName "badbt-tray.exe"
#define MyAppCliName "badbt-detector.exe"

[Setup]
; Unique AppId generated specifically for BadBT
AppId={{C78D9C2E-2A4F-4BE5-A48F-E96BA7DE6C20}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\BadBT
DefaultGroupName=BadBT
AllowNoIcons=yes
OutputDir=..\dist
OutputBaseFilename=BadBT-{#MyAppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "Start BadBT Shield automatically on Windows login"; GroupDescription: "Startup:"; Flags: checkedonce

[Files]
; Main Tray Application Executable
Source: "..\build\tray\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Standalone CLI Detector Tool
Source: "..\build\detector\{#MyAppCliName}"; DestDir: "{app}"; Flags: ignoreversion

; ONNX Runtime Engine DLLs
Source: "..\build\tray\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\tray\onnxruntime_providers_shared.dll"; DestDir: "{app}"; Flags: ignoreversion

; Trained Canonical Random Forest ONNX Model (W=25, 12 features)
Source: "..\build\tray\model\rf_model.onnx"; DestDir: "{app}\model"; Flags: ignoreversion

[Icons]
; Start Menu shortcuts
Name: "{autoprograms}\BadBT\BadBT Shield"; Filename: "{app}\{#MyAppExeName}"
Name: "{autoprograms}\BadBT\BadBT CLI Diagnostic Tool"; Filename: "{app}\{#MyAppCliName}"
Name: "{autoprograms}\BadBT\{cm:UninstallProgram,BadBT}"; Filename: "{uninstallexe}"

; Optional Desktop shortcut
Name: "{autodesktop}\BadBT Shield"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

; Windows Startup entry (autostartup maps to common or user startup based on install mode)
Name: "{autostartup}\BadBT Shield"; Filename: "{app}\{#MyAppExeName}"; Tasks: startup

[Run]
; Option to launch BadBT Shield immediately after installation completes
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,BadBT Shield}"; Flags: nowait postinstall skipifsilent
