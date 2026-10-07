; Inno Setup script for EQ Master Pro (VST3, Windows x64)
; Build:  ISCC.exe /DBuildDir=<cmake build dir> /DMyAppVersion=1.0.0 installer\EQMasterPro.iss
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#define MyAppName "EQ Master Pro"

[Setup]
AppId={{677B4624-C020-4C79-AB43-0C9C5B10DEBA}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=EQ Master
DefaultDirName={commonpf64}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\DIST
OutputBaseFilename=EQMasterPro-Setup-x64
SetupIconFile=..\assets\EQMasterPro.ico
UninstallDisplayIcon={app}\EQMasterPro.ico
UninstallDisplayName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Files]
Source: "{#BuildDir}\EQMasterPro_artefacts\Release\VST3\EQMasterPro.vst3\*"; DestDir: "{commoncf64}\VST3\EQMasterPro.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\USER_GUIDE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\EQMasterPro.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Guia do usuario (USER_GUIDE)"; Filename: "{app}\USER_GUIDE.md"
Name: "{group}\Desinstalar {#MyAppName}"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=EQ Master Pro {#MyAppVersion} foi instalado em C:\Program Files\Common Files\VST3. Abra sua DAW e rescaneie os plugins VST3.
