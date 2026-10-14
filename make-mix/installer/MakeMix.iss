; MAKE MIX 的 Windows 安装程序（Inno Setup 6）。CI 里：iscc /DAppVer=1.0.0 MakeMix.iss
; 插件装到 C:\Program Files\Common Files\VST3\MAKE MIX.vst3（所有 DAW 都会扫这里），
; 使用说明和卸载程序装到 C:\Program Files\MAKE STUDIO\MAKE MIX。
#ifndef AppVer
  #define AppVer "1.0.0"
#endif

[Setup]
AppId={{A2FF3D84-8214-4A6B-864D-19DB311D480B}
AppName=MAKE MIX
AppVersion={#AppVer}
AppVerName=MAKE MIX {#AppVer}
AppPublisher=MAKE STUDIO
AppPublisherURL=https://www.makestudio.cn
AppSupportURL=https://www.makestudio.cn
DefaultDirName={commonpf64}\MAKE STUDIO\MAKE MIX
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName=MAKE STUDIO
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=MAKE-MIX-{#AppVer}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=MAKE MIX {#AppVer} (VST3)
UninstallDisplayIcon={uninstallexe}
InfoBeforeFile=README.txt
CloseApplications=yes
VersionInfoVersion={#AppVer}
VersionInfoCompany=MAKE STUDIO
VersionInfoDescription=MAKE MIX VST3 installer

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=安裝 MAKE MIX {#AppVer}
WelcomeLabel2=MAKE MIX 是 MAKE STUDIO 的一鍵混音插件（VST3）：人聲、伴奏、總線三個角色，一鍵分析、一鍵混音。%n%n安裝前請先關閉 FL Studio。
FinishedHeadingLabel=MAKE MIX 安裝好了
FinishedLabel=插件已裝到 C:\Program Files\Common Files\VST3\MAKE MIX.vst3。%n%n打開 FL Studio：Options > Manage plugins > Find more plugins（或 Start scan），在 MAKE STUDIO 下面就能找到 MAKE MIX。
ButtonNext=下一步 >
ButtonBack=< 上一步
ButtonInstall=安裝
ButtonFinish=完成
ButtonCancel=取消
InfoBeforeLabel=請先讀一下使用說明：

[Files]
Source: "..\..\dist\MAKE MIX.vst3\*"; DestDir: "{commoncf64}\VST3\MAKE MIX.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "README.txt"; DestDir: "{app}"; DestName: "使用說明.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MAKE STUDIO\MAKE MIX 使用說明"; Filename: "{app}\使用說明.txt"
Name: "{autoprograms}\MAKE STUDIO\卸載 MAKE MIX"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\MAKE MIX.vst3"

[Run]
Filename: "{app}\使用說明.txt"; Description: "打開使用說明"; Flags: shellexec postinstall skipifsilent unchecked
