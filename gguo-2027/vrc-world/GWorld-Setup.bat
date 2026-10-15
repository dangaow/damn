<# : batch part - cmd runs these lines, PowerShell treats them as a comment
@echo off
setlocal
set "GW_SELF=%~f0"
fltmc >nul 2>&1
if errorlevel 1 goto :elevate
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:GW_SELF, [Text.Encoding]::UTF8))"
echo.
pause
exit /b

:elevate
if "%~1"=="--elevated" goto :noadmin
echo Asking for administrator rights, please click Yes...
powershell.exe -NoProfile -Command "try { Start-Process -FilePath $env:GW_SELF -ArgumentList '--elevated' -Verb RunAs -ErrorAction Stop } catch { exit 1 }"
if errorlevel 1 goto :denied
exit /b

:denied
echo Administrator rights were not granted. Nothing was installed.
pause
exit /b 1

:noadmin
echo Could not get administrator rights. Right-click this file and choose Run as administrator.
pause
exit /b 1
: end of batch part #>

# ===================== 以下是 PowerShell =====================
# G 國 VRChat 世界：一鍵安裝開發工具
# 裝：Git、Claude 桌面版、Claude Code、Unity Hub、Unity 2022.3.22f1（含 Android/Quest 模組）、
#     VRChat Creator Companion、Blender、.NET 8 SDK、VRChat 命令列工具 vpm
# 已經裝過的會跳過；中途失敗的會列在最後，其他照裝。可以重複執行。

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'      # 不關的話 Windows PowerShell 下載大檔會慢幾十倍
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
$env:DOTNET_NOLOGO = '1'

# VRChat 指定的 Unity 版本（2026 年 10 月仍是這個；VRChat 明確說不要用 Unity 6 上傳）
$UnityVersion   = '2022.3.22f1'
$UnityChangeset = '887be4894c44'
$HubUrl = 'https://public-cdn.cloud.unity3d.com/hub/prod/3.22.3/UnityHubSetup-3.22.3-x64.exe'
$VccUrl = 'https://vrcpm.vrchat.cloud/vcc/Builds/2.4.5/VRChat_CreatorCompanion_Setup_2.4.5.exe'
$NeedGB = 30
# winget 這幾個退出碼也算成功：已經是最新 / 已經裝過 / 要重開機才完成
$WingetOkCodes = @(0, -1978335189, -1978335135, -1978334967)

$script:Results = New-Object System.Collections.ArrayList
$script:HasWinget = $false
$script:DlDir = Join-Path $env:TEMP 'gworld-setup'

function Write-Step([string]$t) { Write-Host ''; Write-Host "==== ${t} ====" -ForegroundColor Cyan }
function Write-Ok([string]$t)   { Write-Host "  [好] ${t}" -ForegroundColor Green }
function Write-Note([string]$t) { Write-Host "  [注意] ${t}" -ForegroundColor Yellow }
function Write-Bad([string]$t)  { Write-Host "  [失敗] ${t}" -ForegroundColor Red }
function Add-Result([string]$name, [string]$state, [string]$note) {
    [void]$script:Results.Add([pscustomobject]@{ Name = $name; State = $state; Note = $note })
}

function Update-PathEnv {
    $m = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $u = [Environment]::GetEnvironmentVariable('Path', 'User')
    $p = @($m, $u) | Where-Object { $_ }
    if ($p) { $env:Path = $p -join ';' }
}

function Get-UninstallEntry([string]$pattern) {
    $roots = @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
               'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
               'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')
    foreach ($r in $roots) {
        $hit = Get-ItemProperty -Path $r -ErrorAction SilentlyContinue |
               Where-Object { $_.DisplayName -like $pattern } | Select-Object -First 1
        if ($hit) { return $hit }
    }
    return $null
}

function Test-WingetInstalled([string]$id) {
    $out = & winget list --id $id -e --accept-source-agreements 2>&1 | Out-String
    $prefix = $id.Substring(0, [Math]::Min(12, $id.Length))
    return (($LASTEXITCODE -eq 0) -and ($out -match [regex]::Escape($prefix)))
}

# $already：額外的「已經裝過」判斷（例如不是用 winget 裝的同一個工具）
function Install-WingetPackage([string]$id, [string]$name, [scriptblock]$already) {
    Write-Step "安裝 ${name}"
    if ($already -and (& $already)) { Write-Ok "${name} 已經裝過，跳過"; Add-Result $name '已存在' ''; return }
    if (-not $script:HasWinget) { Write-Bad '這台電腦沒有 winget'; Add-Result $name '失敗' '沒有 winget（見最上面的提示）'; return }
    if (Test-WingetInstalled $id) { Write-Ok "${name} 已經裝過，跳過"; Add-Result $name '已存在' ''; return }
    Write-Host '  下載並安裝中，請稍候……'
    & winget install --id $id -e -s winget --accept-source-agreements --accept-package-agreements --silent 2>&1 | Out-Host
    $code = $LASTEXITCODE
    if ($WingetOkCodes -contains $code) {
        Update-PathEnv
        Write-Ok "${name} 安裝完成"; Add-Result $name '已安裝' ''
    } else {
        Write-Bad "${name} 安裝失敗（winget 錯誤碼 ${code}）"; Add-Result $name '失敗' "winget 錯誤碼 ${code}，檢查網路或代理後再執行一次"
    }
}

function Get-Download([string]$url, [string]$out) {
    for ($i = 1; $i -le 3; $i++) {
        try {
            if (Test-Path $out) { Remove-Item $out -Force -ErrorAction SilentlyContinue }
            Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing -ErrorAction Stop
            if ((Test-Path $out) -and ((Get-Item $out).Length -gt 1MB)) { return $true }
        } catch {
            Write-Note "下載失敗（第 ${i} 次）：$($_.Exception.Message)"
        }
        if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
            & curl.exe -L --fail --retry 2 --connect-timeout 30 -o $out $url 2>&1 | Out-Host
            if (($LASTEXITCODE -eq 0) -and (Test-Path $out) -and ((Get-Item $out).Length -gt 1MB)) { return $true }
        }
        Start-Sleep -Seconds 3
    }
    return $false
}

# $check：判斷有沒有裝好（安裝前用來跳過，安裝後用來確認，不只看退出碼）
function Install-FromUrl([string]$name, [string]$url, [string]$fileName, [string]$silentArgs, [scriptblock]$check) {
    Write-Step "安裝 ${name}"
    if (& $check) { Write-Ok "${name} 已經裝過，跳過"; Add-Result $name '已存在' ''; return }
    $out = Join-Path $script:DlDir $fileName
    Write-Host '  下載中，請稍候……'
    if (-not (Get-Download $url $out)) {
        Write-Bad "${name} 下載失敗"; Add-Result $name '失敗' '下載失敗，檢查網路或代理後再執行一次'; return
    }
    Write-Host '  安裝中……'
    $code = -1
    try {
        $p = Start-Process -FilePath $out -ArgumentList $silentArgs -Wait -PassThru -ErrorAction Stop
        $code = $p.ExitCode
    } catch {
        Write-Note $_.Exception.Message
    }
    # 有些安裝程式先退出、背景再寫完：最多再等 60 秒
    $ok = $false
    for ($i = 0; $i -lt 12; $i++) { if (& $check) { $ok = $true; break }; Start-Sleep -Seconds 5 }
    Remove-Item $out -Force -ErrorAction SilentlyContinue
    if ($ok) { Write-Ok "${name} 安裝完成"; Add-Result $name '已安裝' '' }
    else { Write-Bad "${name} 安裝失敗（退出碼 ${code}）"; Add-Result $name '失敗' "安裝程式退出碼 ${code}" }
}

function Get-HubExe {
    $cands = @()
    $e = Get-UninstallEntry 'Unity Hub*'
    if ($e -and $e.InstallLocation) { $cands += (Join-Path ($e.InstallLocation.Trim('"')) 'Unity Hub.exe') }
    if ($env:ProgramFiles) { $cands += (Join-Path $env:ProgramFiles 'Unity Hub\Unity Hub.exe') }
    foreach ($c in $cands) { if (Test-Path $c) { return $c } }
    return $null
}

function Stop-Hub {
    $p = Get-Process -Name 'Unity Hub' -ErrorAction SilentlyContinue
    if ($p) {
        Write-Note '先關掉正在開著的 Unity Hub（它開著的時候命令列安裝會失靈）'
        $p | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 3
    }
}

# 跑 Unity Hub 命令列。輸出一行一行顯示出來；-Capture 時改成把輸出當字串回傳
function Invoke-Hub([string]$hub, [string]$hubArgs, [switch]$Capture) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $hub
    $psi.Arguments = "-- --headless ${hubArgs}"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    try {
        $p = [System.Diagnostics.Process]::Start($psi)
        $errTask = $p.StandardError.ReadToEndAsync()
        if ($Capture) {
            # 只是查詢：最多等 2 分鐘，Unity Hub 沒反應就放棄，不要卡住整個安裝
            $outTask = $p.StandardOutput.ReadToEndAsync()
            if (-not $p.WaitForExit(120000)) { try { $p.Kill() } catch { }; return '' }
            return $outTask.Result
        }
        while ($null -ne ($line = $p.StandardOutput.ReadLine())) { Write-Host "  $line" }
        $p.WaitForExit()
        $err = $errTask.Result
        if ($err -and $err.Trim()) { Write-Host "  $($err.Trim())" -ForegroundColor DarkGray }
        return $p.ExitCode
    } catch {
        Write-Note "Unity Hub 命令列執行失敗：$($_.Exception.Message)"
        if ($Capture) { return '' }
        return -1
    }
}

function Get-UnityEditorExe {
    $cands = @()
    if ($env:ProgramFiles) { $cands += (Join-Path $env:ProgramFiles "Unity\Hub\Editor\${UnityVersion}\Editor\Unity.exe") }
    # 改過 Unity Hub 安裝位置的話，位置記在這個檔案裡（內容是一個帶引號的路徑）
    if ($env:APPDATA) {
        $sec = Join-Path $env:APPDATA 'UnityHub\secondaryInstallPath.json'
        if (Test-Path $sec) {
            $dir = (Get-Content $sec -Raw -ErrorAction SilentlyContinue)
            if ($dir) { $dir = $dir.Trim().Trim('"') }
            if ($dir) { $cands += (Join-Path $dir "${UnityVersion}\Editor\Unity.exe") }
        }
    }
    foreach ($c in $cands) { if (Test-Path $c) { return $c } }
    $hub = Get-HubExe
    if ($hub) {
        $txt = Invoke-Hub $hub 'editors -i' -Capture
        foreach ($line in ($txt -split "`r?`n")) {
            if ($line -match ([regex]::Escape($UnityVersion) + '.*?installed at\s+(.+?Unity\.exe)')) {
                $x = $Matches[1].Trim()
                if (Test-Path $x) { return $x }
            }
        }
    }
    return $null
}

function Test-AndroidModule([string]$editorExe) {
    $dir = Join-Path (Split-Path $editorExe) 'Data\PlaybackEngines\AndroidPlayer'
    return ((Test-Path (Join-Path $dir 'SDK')) -and (Test-Path (Join-Path $dir 'OpenJDK')))
}

function Install-UnityEditor {
    $name = "Unity ${UnityVersion}（含 Android / Quest 模組）"
    Write-Step "安裝 ${name}"
    $hub = Get-HubExe
    if (-not $hub) { Write-Bad '沒有 Unity Hub，跳過'; Add-Result $name '失敗' '要先裝好 Unity Hub，再執行一次這個檔案'; return }
    Stop-Hub
    $ed = Get-UnityEditorExe
    if (-not $ed) {
        Write-Host '  要下載 6 到 10 GB，網路慢的話要 30 分鐘以上。請不要關掉這個視窗……'
        $code = Invoke-Hub $hub "install --version ${UnityVersion} --changeset ${UnityChangeset} --module android --childModules"
        $ed = Get-UnityEditorExe
        if (-not $ed) {
            if ($code -ne 0) { Write-Bad "Unity ${UnityVersion} 沒裝上（Unity Hub 退出碼 ${code}）" } else { Write-Bad "Unity ${UnityVersion} 沒裝上" }
            Write-Note '已經幫你打開 Unity Hub 的安裝頁：勾上 Android Build Support，按「安裝」就行'
            try { Start-Process "unityhub://${UnityVersion}/${UnityChangeset}" } catch { }
            Add-Result $name '失敗' 'Unity Hub 的安裝頁已經打開，在那裡按「安裝」'
            return
        }
    }
    if (-not (Test-AndroidModule $ed)) {
        Write-Host '  補裝 Android 模組（Quest 版要用）……'
        [void](Invoke-Hub $hub "install-modules --version ${UnityVersion} --module android --childModules")
        if (-not (Test-AndroidModule $ed)) {
            Write-Note 'Unity 裝好了，但 Android 模組沒裝上'
            Add-Result $name '部分完成' "Quest 版要用：Unity Hub →「安裝」→ ${UnityVersion} 右邊齒輪 →「添加模組」→ 勾 Android Build Support"
            return
        }
    }
    Write-Ok "${name} 就緒：${ed}"
    Add-Result $name '已安裝' ''
}

function Install-Vpm {
    $name = 'VRChat 命令列工具 vpm'
    Write-Step "安裝 ${name}"
    $dotnet = $null
    if ($env:ProgramFiles -and (Test-Path (Join-Path $env:ProgramFiles 'dotnet\dotnet.exe'))) { $dotnet = Join-Path $env:ProgramFiles 'dotnet\dotnet.exe' }
    else { $c = Get-Command dotnet -ErrorAction SilentlyContinue; if ($c) { $dotnet = $c.Source } }
    if (-not $dotnet) { Write-Bad '沒有 .NET SDK，跳過'; Add-Result $name '失敗' '要先裝好 .NET 8 SDK'; return }
    $vpm = Join-Path $env:USERPROFILE '.dotnet\tools\vpm.exe'
    $was = Test-Path $vpm
    if (-not $was) {
        & $dotnet tool install --global vrchat.vpm.cli 2>&1 | Out-Host
        if (-not (Test-Path $vpm)) { Write-Bad 'vpm 沒裝上'; Add-Result $name '失敗' "dotnet 退出碼 ${LASTEXITCODE}"; return }
    }
    & $vpm install templates 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Note 'vpm 範本沒裝上，不影響 Creator Companion' }
    if ($was) { Write-Ok "${name} 已經裝過"; Add-Result $name '已存在' '' }
    else { Write-Ok "${name} 安裝完成"; Add-Result $name '已安裝' '' }
}

function Test-DotNet8 {
    if (-not $env:ProgramFiles) { return $false }
    return [bool](Get-ChildItem -Path (Join-Path $env:ProgramFiles 'dotnet\sdk') -Directory -Filter '8.*' -ErrorAction SilentlyContinue)
}

function Show-Summary {
    Write-Host ''
    Write-Host '==================== 結果 ====================' -ForegroundColor Cyan
    foreach ($r in $script:Results) {
        $color = 'Green'
        if ($r.State -eq '失敗') { $color = 'Red' } elseif ($r.State -eq '部分完成') { $color = 'Yellow' }
        $line = "  [$($r.State)] $($r.Name)"
        if ($r.Note) { $line += "：$($r.Note)" }
        Write-Host $line -ForegroundColor $color
    }
    $bad = @($script:Results | Where-Object { $_.State -ne '已安裝' -and $_.State -ne '已存在' }).Count
    Write-Host ''
    if ($bad -gt 0) { Write-Host "  有 ${bad} 項沒完成。處理完網路問題後再雙擊這個檔案一次，已經裝好的會自動跳過。" -ForegroundColor Yellow }
    else { Write-Host '  全部裝好了。' -ForegroundColor Green }
    Write-Host ''
    Write-Host '  接下來要你自己做（帳號只能你本人登入）：' -ForegroundColor Cyan
    Write-Host '  1. 打開 Unity Hub，登入 Unity 帳號，啟用免費的 Personal 授權'
    Write-Host '  2. 打開 VRChat Creator Companion，第一次打開時讓它更新到最新版'
    Write-Host '  3. 打開 Claude 桌面版登入；設定裡打開「電腦操作」'
    Write-Host '  4. Claude 桌面版切到 Code，選「本機」，開一個資料夾（例如 D:\GWorld），把方案連結貼給它'
    Write-Host '  VRChat 遊戲本體請從 Steam 安裝，這個檔案不裝它。'
}

function Main {
    Write-Host ''
    Write-Host '  G 國 VRChat 世界 —— 一鍵安裝開發工具' -ForegroundColor Cyan
    Write-Host '  會裝：Git、Claude 桌面版、Claude Code、Unity Hub、Unity 2022.3.22f1、'
    Write-Host '        VRChat Creator Companion、Blender、.NET 8 SDK、vpm'
    Write-Host '  已經裝過的會跳過。全部大約要下載 12 GB。國內網路請先開好代理。'

    if (-not [Environment]::Is64BitOperatingSystem) { Write-Bad '需要 64 位元的 Windows 10 或 11'; return }

    if ($env:SystemDrive) {
        $drv = Get-PSDrive -Name ($env:SystemDrive.TrimEnd(':')) -ErrorAction SilentlyContinue
        if ($drv -and $drv.Free) {
            $freeGB = [Math]::Round($drv.Free / 1GB, 1)
            if ($freeGB -lt $NeedGB) {
                Write-Note "系統碟 $($env:SystemDrive) 只剩 ${freeGB} GB，建議至少 ${NeedGB} GB（Unity 裝在系統碟）"
                $ans = Read-Host '  空間不夠可能會裝到一半失敗。還是要繼續嗎？輸入 Y 繼續，其他鍵取消'
                if ($ans -notmatch '^[Yy]') { Write-Host '  已取消，什麼都沒裝。'; return }
            }
        }
    }

    New-Item -ItemType Directory -Path $script:DlDir -Force | Out-Null

    $script:HasWinget = [bool](Get-Command winget -ErrorAction SilentlyContinue)
    if (-not $script:HasWinget) {
        Write-Note '這台電腦沒有 winget：Git、Claude、Blender、.NET 會跳過。'
        Write-Note '已經幫你打開微軟商店的「應用安裝程式」頁，裝好或更新後再執行一次這個檔案。'
        try { Start-Process 'ms-windows-store://pdp/?productid=9NBLGGH4NNS1' } catch { }
    }

    Install-WingetPackage 'Git.Git' 'Git' { [bool](Get-Command git -ErrorAction SilentlyContinue) }
    Install-WingetPackage 'Anthropic.Claude' 'Claude 桌面版' $null
    Install-WingetPackage 'Anthropic.ClaudeCode' 'Claude Code（命令列）' {
        ([bool](Get-Command claude -ErrorAction SilentlyContinue)) -or ($env:USERPROFILE -and (Test-Path (Join-Path $env:USERPROFILE '.local\bin\claude.exe')))
    }
    Install-FromUrl 'Unity Hub' $HubUrl 'UnityHubSetup.exe' '/S' { [bool](Get-HubExe) }
    Install-FromUrl 'VRChat Creator Companion' $VccUrl 'VCCSetup.exe' '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER' { [bool](Get-UninstallEntry '*Creator Companion*') }
    Install-WingetPackage 'BlenderFoundation.Blender' 'Blender' $null
    Install-WingetPackage 'Microsoft.DotNet.SDK.8' '.NET 8 SDK' { Test-DotNet8 }
    Install-Vpm
    Install-UnityEditor   # 最久的放最後

    Remove-Item $script:DlDir -Recurse -Force -ErrorAction SilentlyContinue
    Show-Summary
}

$logDir = $env:TEMP
if ($env:GW_SELF) { $logDir = Split-Path $env:GW_SELF }
$logFile = Join-Path $logDir 'GWorld-Setup-log.txt'
$logging = $false
try { Start-Transcript -Path $logFile -Append -ErrorAction Stop | Out-Null; $logging = $true } catch {
    try { $logFile = Join-Path $env:TEMP 'GWorld-Setup-log.txt'; Start-Transcript -Path $logFile -Append -ErrorAction Stop | Out-Null; $logging = $true } catch { }
}
if ($env:GW_NO_MAIN -ne '1') {
    try { Main } catch { Write-Bad "出現意外錯誤：$($_.Exception.Message)"; Write-Host "  $($_.ScriptStackTrace)" }
    if ($logging) { Write-Host "  完整紀錄：${logFile}" -ForegroundColor DarkGray }
}
if ($logging) { try { Stop-Transcript | Out-Null } catch { } }
