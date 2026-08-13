#define AppName "PlayWorship"
#define AppPublisher "Multitracks"
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "."
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "playworship-windows-installer"
#endif
#ifndef AppExeName
  #define AppExeName "MTControls.exe"
#endif
#ifndef InstallerArch
  #define InstallerArch "x64"
#endif
#ifndef InstallerIconFile
  #define InstallerIconFile ""
#endif
#ifndef VcRedistFile
  #define VcRedistFile ""
#endif

[Setup]
AppId={{B96A1AA6-ADF8-45BB-90D5-9F7CC1D309B9}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ChangesAssociations=yes
UninstallDisplayIcon={app}\{#AppExeName}
#if InstallerArch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DefaultDirName={autopf}\{#AppName}
#else
DefaultDirName={autopf32}\{#AppName}
#endif
#if InstallerIconFile != ""
SetupIconFile={#InstallerIconFile}
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.AdditionalTasks=Additional tasks:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "firewallexception"; Description: "Agregar excepcion de firewall para {#AppName}"; GroupDescription: "{cm:AdditionalTasks}"; Flags: checkedonce

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "bootstrap-assets.ps1"; DestDir: "{app}\installer"; Flags: ignoreversion
Source: "add-defender-exclusion.ps1"; DestDir: "{app}\installer"; Flags: ignoreversion
Source: "remove-defender-exclusion.ps1"; DestDir: "{app}\installer"; Flags: ignoreversion
#if VcRedistFile != ""
Source: "{#VcRedistFile}"; DestDir: "{tmp}"; DestName: "vc_redist.exe"; Flags: deleteafterinstall ignoreversion
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.pwproj"; ValueType: string; ValueName: ""; ValueData: "PlayWorship.Project"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\PlayWorship.Project"; ValueType: string; ValueName: ""; ValueData: "PlayWorship Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\PlayWorship.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"",0"
Root: HKA; Subkey: "Software\Classes\PlayWorship.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""

[Run]
#if VcRedistFile != ""
Filename: "{cmd}"; Parameters: "/C ""{tmp}\vc_redist.exe"" /install /quiet /norestart || exit /b 0"; StatusMsg: "Instalando Microsoft Visual C++ Runtime... / Installing Microsoft Visual C++ Runtime..."; Flags: runhidden waituntilterminated
#endif
Filename: "{cmd}"; Parameters: "/C powershell -NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\bootstrap-assets.ps1"" || exit /b 0"; StatusMsg: "Descargando recursos iniciales (encoder y pads)... / Downloading initial resources (encoder and pads)..."; Flags: runhidden nowait
; P0.1 (perf): agregar exclusiones puntuales de Microsoft Defender para
; MTControls.exe y %LocalAppData%\MTControls. Corta el costo de AV
; en el primer read de cada stem (~200-500ms por archivo en maquinas
; con AV empresarial). Idempotente: si ya estan, no las duplica.
Filename: "{cmd}"; Parameters: "/C powershell -NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\add-defender-exclusion.ps1"" || exit /b 0"; Flags: runhidden waituntilterminated
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall add rule name=""{#AppName} (Inbound)"" dir=in action=allow program=""{app}\{#AppExeName}"" enable=yes profile=any"; Flags: runhidden; Tasks: firewallexception
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall add rule name=""{#AppName} (Outbound)"" dir=out action=allow program=""{app}\{#AppExeName}"" enable=yes profile=any"; Flags: runhidden; Tasks: firewallexception
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall delete rule name=""{#AppName} (Inbound)"" program=""{app}\{#AppExeName}"""; Flags: runhidden
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall delete rule name=""{#AppName} (Outbound)"" program=""{app}\{#AppExeName}"""; Flags: runhidden
; P0.1 (perf): limpiar exclusiones de Defender que agrego el instalador.
Filename: "{cmd}"; Parameters: "/C powershell -NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\remove-defender-exclusion.ps1"" || exit /b 0"; Flags: runhidden waituntilterminated
