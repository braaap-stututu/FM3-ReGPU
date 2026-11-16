; Forza Motorsport 3 ReGPU: installer for the current build (Inno Setup 6). Built by installer\build-installer.ps1,
; which stages the files in out\installer\stage and passes the version. No game files inside: the launcher's Play tab
; extracts Disc 1 from the player's own disc image ("From .iso..."), and installs Disc 2's content the same way.
; Per user, no admin rights. Layout: {app}\fm3.exe + DLLs, {app}\launcher\FM3Launcher.exe (self-contained .NET 9),
; {app}\game (Disc 1, made by the launcher), {app}\out\run (fm3.log). Saves and settings: Documents\fm3 (kept on
; uninstall).

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef StageDir
  #define StageDir "..\out\installer\stage"
#endif
#ifndef OutDir
  #define OutDir "..\out\installer\dist"
#endif

[Setup]
AppId={{6F74359D-4D53-084D-F3A1-5245475055A1}
AppName=Forza Motorsport 3 ReGPU
AppVersion={#AppVersion}
AppVerName=Forza Motorsport 3 ReGPU {#AppVersion}
AppPublisher=FM3-ReGPU
DefaultDirName={localappdata}\FM3-ReGPU
DefaultGroupName=Forza Motorsport 3 ReGPU
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutDir}
OutputBaseFilename=FM3-ReGPU-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\launcher\FM3Launcher.exe
LicenseFile={#StageDir}\licenses\LICENSE
InfoBeforeFile={#StageDir}\before-install.txt

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "before-install.txt"

[Dirs]
Name: "{app}\out\run"

[Icons]
Name: "{group}\Forza Motorsport 3 ReGPU"; Filename: "{app}\launcher\FM3Launcher.exe"; WorkingDir: "{app}\launcher"
Name: "{group}\Uninstall Forza Motorsport 3 ReGPU"; Filename: "{uninstallexe}"
Name: "{userdesktop}\Forza Motorsport 3 ReGPU"; Filename: "{app}\launcher\FM3Launcher.exe"; WorkingDir: "{app}\launcher"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Run]
Filename: "{app}\launcher\FM3Launcher.exe"; Description: "Open the launcher (then: Play tab, Game, From .iso...)"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The extracted Disc 1, the run folder (fm3.log, pipeline cache of the run) and the launcher's own files. Saves,
; settings and Disc 2 content in Documents\fm3 are kept.
Type: filesandordirs; Name: "{app}\game"
Type: filesandordirs; Name: "{app}\out"
