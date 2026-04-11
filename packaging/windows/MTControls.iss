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
#ifndef InstallerIconFile
  #define InstallerIconFile ""
#endif

[Setup]
AppId={{B96A1AA6-ADF8-45BB-90D5-9F7CC1D309B9}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#AppExeName}
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

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall add rule name=""{#AppName} (Inbound)"" dir=in action=allow program=""{app}\{#AppExeName}"" enable=yes profile=any"; Flags: runhidden; Tasks: firewallexception
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall add rule name=""{#AppName} (Outbound)"" dir=out action=allow program=""{app}\{#AppExeName}"" enable=yes profile=any"; Flags: runhidden; Tasks: firewallexception
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall delete rule name=""{#AppName} (Inbound)"" program=""{app}\{#AppExeName}"""; Flags: runhidden
Filename: "{cmd}"; Parameters: "/C netsh advfirewall firewall delete rule name=""{#AppName} (Outbound)"" program=""{app}\{#AppExeName}"""; Flags: runhidden
