; Forza Motorsport 3 ReGPU: installer for the current build (Inno Setup 6.3 or newer). Built by
; installer\build-installer.ps1, which stages the files in out\installer\stage and passes the version. No game files
; inside: a "Game discs" page asks for the player's own Disc 1 / Disc 2 images, and after the files are copied the
; installer runs the launcher's own extraction (FM3Launcher --install-disc1 / --install-disc2, the same code as the
; Play tab's "From .iso...") with a progress bar. Empty boxes = do it later in the launcher. Silent installs:
; /DISC1="<iso>" /DISC2="<iso>".
; Per user, no admin rights. Layout: {app}\fm3.exe + DLLs, {app}\launcher\FM3Launcher.exe (self-contained .NET 9),
; {app}\game (Disc 1, made by the launcher), {app}\out\run (fm3.log). Saves and settings: Documents\fm3 (kept on
; uninstall).

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or newer is needed (ExecAndLogOutput): winget upgrade JRSoftware.InnoSetup
#endif

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
Filename: "{app}\launcher\FM3Launcher.exe"; Description: "Open the launcher"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The extracted Disc 1, the run folder (fm3.log, pipeline cache of the run) and the launcher's own files. Saves,
; settings and Disc 2 content in Documents\fm3 are kept.
Type: filesandordirs; Name: "{app}\game"
Type: filesandordirs; Name: "{app}\out"

[Code]
var
  DiscPage: TInputFilePage;
  ExtractPage: TOutputProgressWizardPage;
  ExtractTitle, LastError: String;

procedure InitializeWizard;
begin
  DiscPage := CreateInputFilePage(wpSelectDir, 'Game discs',
    'Your own Forza Motorsport 3 Ultimate Collection (USA) disc images',
    'Choose your Disc 1 image (Play Disc) to install the game files now: it is checked, then extracted into the ' +
    'install folder (about 6.8 GB, a few minutes). Disc 2 (Content Install Disc) is optional (about 3.4 GB).' + #13#10 +
    'Leave a box empty to do it later in the launcher (Play tab).');
  DiscPage.Add('Disc 1 image (Play Disc):', 'Disc images (*.iso)|*.iso|All files (*.*)|*.*', '.iso');
  DiscPage.Add('Disc 2 image (Content Install Disc, optional):', 'Disc images (*.iso)|*.iso|All files (*.*)|*.*', '.iso');
  DiscPage.Values[0] := ExpandConstant('{param:DISC1|}');
  DiscPage.Values[1] := ExpandConstant('{param:DISC2|}');
  ExtractPage := CreateOutputProgressPage('Installing the game files', 'Extracting your disc images. This takes a few minutes.');
end;

function Disc(I: Integer): String;
begin
  Result := Trim(DiscPage.Values[I]);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID <> DiscPage.ID then
    exit;
  if (Disc(0) <> '') and not FileExists(Disc(0)) then
  begin
    MsgBox('The Disc 1 image doesn''t exist:' + #13#10 + Disc(0), mbError, MB_OK);
    Result := False;
  end
  else if (Disc(1) <> '') and not FileExists(Disc(1)) then
  begin
    MsgBox('The Disc 2 image doesn''t exist:' + #13#10 + Disc(1), mbError, MB_OK);
    Result := False;
  end
  else if (Disc(0) = '') and not FileExists(AddBackslash(WizardDirValue) + 'game\default.xex') then
    Result := MsgBox('No Disc 1 image chosen: the game can''t start until its files are installed. ' +
      'You can add them later in the launcher (Play tab, Game, "From .iso...").' + #13#10#13#10 +
      'Continue without the game files?', mbConfirmation, MB_YESNO) = IDYES;
end;

// One line of the launcher's output: "N%" moves the bar, "Error: ..." is kept for the message, anything else is shown.
procedure OnExtractLine(const S: String; const Error, FirstLine: Boolean);
var
  T: String;
  P: Integer;
begin
  T := Trim(S);
  if T = '' then
    exit;
  if (Length(T) <= 4) and (T[Length(T)] = '%') then
  begin
    P := StrToIntDef(Copy(T, 1, Length(T) - 1), -1);
    if P >= 0 then
    begin
      ExtractPage.SetProgress(P, 100);
      exit;
    end;
  end;
  if Pos('Error: ', T) = 1 then
    LastError := Copy(T, 8, MaxInt)
  else
    ExtractPage.SetText(ExtractTitle, T);
end;

procedure Extract(const Args, Title: String);
var
  Code: Integer;
begin
  ExtractTitle := Title;
  LastError := '';
  ExtractPage.SetText(Title, '');
  ExtractPage.SetProgress(0, 100);
  if not ExecAndLogOutput(ExpandConstant('{app}\launcher\FM3Launcher.exe'), Args, ExpandConstant('{app}\launcher'),
    SW_HIDE, ewWaitUntilTerminated, Code, @OnExtractLine) then
    LastError := SysErrorMessage(Code)
  else if (Code <> 0) and (LastError = '') then
    LastError := Format('the launcher stopped with code %d', [Code]);
  if LastError <> '' then
  begin
    Log(Title + ' failed: ' + LastError);
    SuppressibleMsgBox(Title + ' wasn''t installed:' + #13#10 + LastError + #13#10#13#10 +
      'The PC build itself is installed. You can install the disc later in the launcher (Play tab).',
      mbError, MB_OK, IDOK);
  end
  else
    ExtractPage.SetProgress(100, 100);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep <> ssPostInstall) or ((Disc(0) = '') and (Disc(1) = '')) then
    exit;
  ExtractPage.Show;
  try
    if Disc(0) <> '' then
      Extract('--install-disc1 ' + AddQuotes(Disc(0)) + ' ' + AddQuotes(ExpandConstant('{app}\game')), 'Disc 1 (about 6.8 GB)');
    if Disc(1) <> '' then
      Extract('--install-disc2 ' + AddQuotes(Disc(1)), 'Disc 2 content (about 3.4 GB)');
  finally
    ExtractPage.Hide;
  end;
end;
