param (
    [switch]$Confirmed
)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$componentId = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"

$script:didFailToFetchVersions = $false
$script:declinedUpdates = @{}
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function get-unicodechar($codepoint) {
    return [char]::convertfromutf32($codepoint)
}

function read-yesno {
    param (
        [string]$prompt
    )

    while ($true) {
        $answer = (Read-Host "$prompt [Y/N]").Trim()
        if ($answer -match '^[Yy]$') {
            return $true
        }
        if ($answer -match '^[Nn]$') {
            return $false
        }

        write-host "Please enter Y or N." -ForegroundColor Yellow
    }
}

function get-versioninfo {
    param (
        [string]$name,
        [scriptblock]$getInstalled,
        [scriptblock]$getLatest = $null,
        [string]$minVersion = "0.0.0"
    )

    $installed = & $getInstalled
    $latest = if ($getLatest) { & $getLatest } else { $minVersion }
    $isOutdated = $true

    $latest = $latest -replace ('-', '')

    if ([version]::TryParse($installed, [ref]$null) -and [version]::TryParse($latest, [ref]$null)) {
        $isOutdated = [version]$installed -lt [version]$latest
    } elseif ($installed -match '^\d{8}$' -and $latest -match '^\d{8}$') {
        $isOutdated = [int]$installed -lt [int]$latest
    } elseif ($latest -eq "0.0.0" -or $latest -eq "required") {
        $isOutdated = $true
    } else {
        $isOutdated = $installed -ne $latest
    }

    return [pscustomobject]@{
        Name        = $name
        Installed   = $installed
        Latest      = $latest
        IsOutdated  = $isOutdated
    }
}

function confirm-dependency-change {
    param (
        [pscustomobject]$status
    )

    if (-not $status.IsOutdated) {
        return $false
    }

    if ($status.Installed -in @($null, "", "0.0.0", "missing")) {
        return $true
    }

    if (read-yesno "Update $($status.Name) from $($status.Installed) to $($status.Latest)?") {
        return $true
    }

    $script:declinedUpdates[$status.Name] = $true
    write-host "$(get-unicodechar 0x1f4a1) Skipping the $($status.Name) update; continuing setup."
    return $false
}

function refresh-versioninfo {
    return @(
        get-versioninfo "cmake" { get-cmakeversion } { get-latestcmakeversion }
        get-versioninfo "ninja" { get-ninjaversion } { get-latestninjaversion }
        get-versioninfo "git"   { get-gitversion }   { get-latestgitversion }
        get-versioninfo "msys2" { get-msys2version } { get-latestmsys2version }
        get-msvcstatus
    )
}

function add-to-path {
    param (
        [string]$path
    )

    if ((Test-Path $path) -and -not ($env:Path -split ";" | Where-Object { $_ -eq $path })) {
        $env:Path += ";$path"
        [System.Environment]::SetEnvironmentVariable("Path", $env:Path, [System.EnvironmentVariableTarget]::Machine)
        write-host "$(get-unicodechar 0x1f4a1) added $path to system PATH."
    } elseif (-not (Test-Path $path)) {
        write-host "$(get-unicodechar 0x26a0) warning: path '$path' does not exist!" -ForegroundColor Yellow
    } else {
        write-host "$(get-unicodechar 0x1f4a1) path already exists in PATH: $path"
    }
}

function test-isadmin {
    $identity = [system.security.principal.windowsidentity]::getcurrent()
    $principal = new-object system.security.principal.windowsprincipal($identity)
    return $principal.isinrole([system.security.principal.windowsbuiltinrole]::administrator)
}

function get-cmakeversion {
    try {
        $version = cmake --version 2>$null | select-string -pattern "cmake version (\d+\.\d+\.\d+)" | foreach-object { $_.matches.groups[1].value }
        if (-not [string]::isnullorempty($version)) {
            write-host "$(get-unicodechar 0x1f50d) cmake is installed: version $version"
            return $version
        }
    } catch {
        write-host "$(get-unicodechar 0x26a0) cmake is not installed or not found in path." -foregroundcolor yellow
    }
    return "0.0.0"
}

function get-latestcmakeversion {
    try {
        $response = invoke-restmethod -uri "https://api.github.com/repos/Kitware/CMake/releases/latest" -usebasicparsing
        $version = $response.tag_name -replace "^v", ""  # remove 'v' prefix if present
        write-host "$(get-unicodechar 0x1f50d) Found latest CMake version $version"
        return $version
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to fetch the latest CMake version from GitHub." -foregroundcolor yellow
        $didFailToFetchVersions = $true;
        return "0.0.0"
    }
}

function get-ninjaversion {
    try {
        $version = ninja --version 2>$null
        if (-not [string]::isnullorempty($version)) {
            write-host "$(get-unicodechar 0x1f50d) Finja is installed: version $version"
            return $version
        }
    } catch {
        write-host "$(get-unicodechar 0x26a0) Ninja is not installed or not found in path." -foregroundcolor yellow
    }
    return "0.0.0"
}

function get-latestninjaversion {
    try {
        $response = invoke-restmethod -uri "https://api.github.com/repos/ninja-build/ninja/releases/latest" -usebasicparsing
        $version = $response.tag_name -replace "^v", ""  # remove 'v' prefix if present
        write-host "$(get-unicodechar 0x1f50d) Found latest Ninja version $version"
        return $version
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to fetch the latest Ninja version from GitHub." -foregroundcolor yellow
        $didFailToFetchVersions = $true;
        return "0.0.0"
    }
}

function get-latestmsys2version {
    try {
        $headers = @{ "User-Agent" = "installer-script" }
        $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/msys2/msys2-installer/releases" -Headers $headers -UseBasicParsing

        foreach ($release in $releases) {
            if (-not $release.prerelease -and -not $release.draft -and $release.tag_name -notmatch "nightly") {
                $version = $release.tag_name
                $versionNormalized = $version -replace('-', '')
                write-host "$(get-unicodechar 0x1f50d) Found latest MSYS2 version $versionNormalized"
                return $version
            }
        }

        write-host "$(get-unicodechar 0x26a0) no stable MSYS2 releases found." -ForegroundColor Yellow
    } catch {
        write-host "$(get-unicodechar 0x26a0) failed to fetch MSYS2 releases from GitHub." -ForegroundColor Yellow
    }

    $didFailToFetchVersions = $true;
    return "0.0.0"
}

function get-gitversion {
    try {
        $version = git --version 2>$null | select-string -pattern "git version (\d+\.\d+\.\d+).*" | foreach-object { $_.matches.groups[1].value }
        if ($version) {
            write-host "$(get-unicodechar 0x1f50d) Git is installed: version $version"
            return $version
        }
    } catch {
        write-host "$(get-unicodechar 0x26a0) Git is not installed or not found in path." -foregroundcolor yellow
    }
    return "0.0.0"
}

function get-msvcstatus {
    if (-not (Test-Path $vswhere)) {
        write-host "$(get-unicodechar 0x26a0) Visual Studio Installer and vswhere were not found." -ForegroundColor Yellow
        return [pscustomobject]@{
            Name = "msvc"; Installed = "missing"; Latest = "current"
            IsOutdated = $true; ComponentInstalled = $false; InstallationPath = $null
        }
    }

    $instances = @(& $vswhere -latest -products * -format json -utf8 2>$null | ConvertFrom-Json)
    $instance = $instances | Select-Object -First 1
    if (-not $instance) {
        write-host "$(get-unicodechar 0x26a0) No Visual Studio installation was found." -ForegroundColor Yellow
        return [pscustomobject]@{
            Name = "msvc"; Installed = "missing"; Latest = "current"
            IsOutdated = $true; ComponentInstalled = $false; InstallationPath = $null
        }
    }

    $componentInstances = @(& $vswhere -products * -requires $componentId -format json -utf8 2>$null | ConvertFrom-Json)
    $componentInstalled = [bool]($componentInstances | Where-Object { $_.instanceId -eq $instance.instanceId })
    $latestVersion = $instance.installationVersion

    try {
        $channel = Invoke-RestMethod -Uri $instance.channelUri -UseBasicParsing
        $latestProduct = $channel.channelItems | Where-Object { $_.id -eq $instance.productId } | Select-Object -First 1
        if ($latestProduct.version) {
            $latestVersion = $latestProduct.version
        }
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to fetch the latest Visual Studio version." -ForegroundColor Yellow
        $script:didFailToFetchVersions = $true
    }

    $needsUpdate = $componentInstalled -and ([version]$instance.installationVersion -lt [version]$latestVersion)
    if (-not $componentInstalled) {
        write-host "$(get-unicodechar 0x26a0) The current MSVC x64 toolset is missing." -ForegroundColor Yellow
    } elseif ($needsUpdate) {
        write-host "$(get-unicodechar 0x26a0) Visual Studio $($instance.installationVersion) is installed; $latestVersion is available." -ForegroundColor Yellow
    } else {
        write-host "$(get-unicodechar 0x1f50d) MSVC is present and Visual Studio is current ($($instance.installationVersion))."
    }

    return [pscustomobject]@{
        Name               = "msvc"
        Installed          = if ($componentInstalled) { $instance.installationVersion } else { "missing" }
        Latest             = $latestVersion
        IsOutdated         = (-not $componentInstalled) -or $needsUpdate
        ComponentInstalled = $componentInstalled
        InstallationPath   = $instance.installationPath
    }
}

function get-msys2version {
    $componentsPath = "C:\msys64\components.xml"
    if (-not (Test-Path $componentsPath)) {
        write-host "$(get-unicodechar 0x26a0) MSYS2 components.xml not found. MSYS2 may not be installed." -ForegroundColor Yellow
        return "0.0.0"
    }

    try {
        [xml]$xml = Get-Content $componentsPath
        $versionNode = $xml.Packages.Package | Where-Object { $_.Name -eq "com.msys2.root" }

        if ($versionNode.Version) {
            write-host "$(get-unicodechar 0x1f50d) MSYS2 is installed: version $($versionNode.Version)"
            return $versionNode.Version
        } else {
            write-host "$(get-unicodechar 0x26a0) MSYS2 version not found in XML." -ForegroundColor Yellow
        }
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to parse MSYS2 components.xml" -ForegroundColor Yellow
    }

    return "0.0.0"
}

function install-gitbash {
    write-host "$(get-unicodechar 0x1f680) Downloading Git..."
    $gitInstallerUrl = get-latestgitinstaller
    $gitInstallerPath = "$env:temp\gitinstaller.exe"

    try {
        invoke-webrequest -uri $gitInstallerUrl -outfile $gitInstallerPath
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to download Git." -foregroundcolor red
        return
    }

    write-host "$(get-unicodechar 0x1f4e6) Running Git installer..."
    $exitCode = (Start-Process -FilePath $gitInstallerPath -ArgumentList "/VERYSILENT /NORESTART" -Wait -PassThru).ExitCode

    if ($exitCode -ne 0) {
        Write-Host "$(get-unicodechar 0x26A0) Git installer exited with code $exitCode" -ForegroundColor Yellow
    } else {
        Write-Host "$(get-unicodechar 0x2705) Git installed successfully!"
    }
}

function get-latestgitversion {
    try {
        $response = Invoke-RestMethod -Uri "https://api.github.com/repos/git-for-windows/git/releases/latest" -UseBasicParsing
        $version = $response.tag_name -replace "^v", "" -replace "\.windows\.\d+$", ""
        write-host "$(get-unicodechar 0x1f50d) Found latest Git version $version"
        return $version
    } catch {
        Write-Host "$(get-unicodechar 0x26A0) Failed to fetch latest Git version from GitHub." -ForegroundColor Yellow
        return "0.0.0"
    }
}

function get-latestgitinstaller {
    try {
        $response = Invoke-RestMethod -Uri "https://api.github.com/repos/git-for-windows/git/releases/latest" -UseBasicParsing
        foreach ($asset in $response.assets) {
            if ($asset.name -match "Git-.*-64-bit\.exe$") {
                return $asset.browser_download_url
            }
        }
        throw "Could not find matching 64-bit installer"
    } catch {
        Write-Host "$(get-unicodechar 0x26A0) Failed to fetch latest Git installer." -ForegroundColor Yellow
        return $null
    }
}

function get-latestcmakeinstaller {
    try {
        $headers = @{ "User-Agent" = "installer-script" }
        $release = Invoke-RestMethod -Uri "https://api.github.com/repos/Kitware/CMake/releases/latest" -Headers $headers -UseBasicParsing

        foreach ($asset in $release.assets) {
            if ($asset.name -like "*-windows-x86_64.msi") {
                return $asset.browser_download_url
            }
        }

        write-host "$(get-unicodechar 0x26a0) No suitable CMake MSI installer found in latest release." -ForegroundColor Yellow
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to fetch latest CMake release from GitHub." -ForegroundColor Yellow
    }

    return $null
}

function install-cmake {
    write-host "$(get-unicodechar 0x1f680) Downloading latest cmake..."

    $cmakeInstallerUrl = get-latestcmakeinstaller
    $cmakeInstallerPath = "$env:temp\cmake-installer.msi"
    
    try {
        Invoke-WebRequest -Uri $cmakeInstallerUrl -OutFile "$cmakeInstallerPath" -Headers @{ "User-Agent" = "installer-script" }
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to download CMake." -foregroundcolor red
    }

    write-host "$(get-unicodechar 0x1f4e6) Running CMake installer..."
    $exitCode = (Start-Process -FilePath "msiexec.exe" `
        -ArgumentList "/i `"$cmakeInstallerPath`" /quiet /norestart" `
        -Wait -PassThru).ExitCode

    if ($exitCode -ne 0) {
        Write-Host "$(get-unicodechar 0x26A0) CMake installer exited with code $exitCode" -ForegroundColor Yellow
    } else {
        Write-Host "$(get-unicodechar 0x2705) CMake installed successfully!"
    }
}

function install-ninja {
    write-host "$(get-unicodechar 0x1f680) Downloading latest ninja..."
    $ninjaurl = "https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip"
    try {
        invoke-webrequest -uri $ninjaurl -outfile "$env:temp\ninja.zip"
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to download Ninja." -foregroundcolor red
    }

    try {
        expand-archive -path "$env:temp\ninja.zip" -destinationpath "$env:localappdata\ninja-build" -force
        write-host "$(get-unicodechar 0x2705) Ninja installed successfully!"
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to install Ninja." -foregroundcolor red
    }
}

function install-msvc($status) {
    $vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe"

    if (-not $status.InstallationPath) {
        Write-Host "$(get-unicodechar 0x26a0) Install Visual Studio or Visual Studio Build Tools, then run this script again." -ForegroundColor Yellow
        return
    }

    if (Test-Path $vsInstaller) {
        if ($status.ComponentInstalled) {
            write-host "$(get-unicodechar 0x1f680) Updating Visual Studio and MSVC..."
            $arguments = @(
                "update",
                "--installPath", "`"$($status.InstallationPath)`"",
                "--quiet",
                "--norestart"
            )
        } else {
            write-host "$(get-unicodechar 0x1f680) Installing the current MSVC x64 toolset..."
            $arguments = @(
            "modify",
            "--installPath", "`"$($status.InstallationPath)`"",
            "--add", $componentId,
            "--quiet",
            "--norestart"
            )
        }

        $exitCode = (Start-Process -FilePath $vsInstaller -ArgumentList $arguments -Wait -PassThru).ExitCode
        if ($exitCode -eq 0) {
            write-host "$(get-unicodechar 0x2705) MSVC setup completed successfully!"
        } else {
            Write-Host "$(get-unicodechar 0x26a0) Visual Studio Installer exited with code $exitCode." -ForegroundColor Yellow
        }
    } else {
        Write-Host "$(get-unicodechar 0x26a0) Visual Studio Installer not found. Install component $componentId manually." -ForegroundColor Yellow
    }
}

function install-msys2 {
    write-host "$(get-unicodechar 0x1f680) Downloading MSYS2..."

    $latest = get-latestmsys2version
    $latestNormalized = $latest -replace ('-', '')
    $msys2Url = "https://github.com/msys2/msys2-installer/releases/download/$latest/msys2-x86_64-$latestNormalized.exe"
    $installerPath = "$env:TEMP\msys2-installer.exe"
    $msys2Root = "C:\msys64"

    try {
        Invoke-WebRequest -Uri $msys2Url -OutFile $installerPath -Headers @{ "User-Agent" = "installer-script" }
    } catch {
        write-host "$(get-unicodechar 0x26a0) Failed to download MSYS2." -foregroundcolor red
        return
    }

    write-host "$(get-unicodechar 0x1f4e6) Running MSYS2 installer..."
    $exitCode = (Start-Process -FilePath $installerPath -ArgumentList "install --root `"$msys2Root`" --confirm-command" -Wait -PassThru).ExitCode

    if ($exitCode -ne 0) {
        Write-Host "$(get-unicodechar 0x26A0) MSYS2 installer exited with code $exitCode" -ForegroundColor Yellow
    } else {
        Write-Host "$(get-unicodechar 0x2705) MSYS2 installed successfully!"
    }
}

function install-make {
    write-host "$(get-unicodechar 0x1f4e6) Installing make via pacman..."

    $msysBash = "C:\msys64\usr\bin\bash.exe"

    if (Test-Path $msysBash) {
        $exitCode = (Start-Process -NoNewWindow -FilePath $msysBash -ArgumentList "-l", "-c", "'pacman -S --needed --noconfirm make'" -Wait -PassThru).ExitCode
        write-host "$(get-unicodechar 0x2705) Installed make"
    } else {
        write-host "$(get-unicodechar 0x26a0) MSYS2 bash not found at $msysBash" -ForegroundColor Yellow
    }
}


# install git pre-commit hook
#$githooksdir = join-path $psscriptroot "../.git/hooks"
#$precommithook = join-path $psscriptroot "../.githooks/pre-commit"
#
#if (-not (test-path "$githooksdir/pre-commit")) {
#    write-host "installing pre-commit hook..."
#    copy-item -path $precommithook -destination "$githooksdir/pre-commit" -force
#    icacls "$githooksdir/pre-commit" /grant everyone:rx > $null  # ensure it's executable
#    write-host "$(get-unicodechar 0x2705) installed pre-commit hook"
#} else {
#    write-host "$(get-unicodechar 0x2705) pre-commit hook already installed. skipping..."
#}

if (-not $Confirmed) {
    write-host ""
    write-host "This script will install missing dependencies and offer to update outdated dependencies:"
    write-host "  - CMake"
    write-host "  - Ninja"
    write-host "  - Git for Windows / Git Bash"
    write-host "  - Visual Studio MSVC x64 tools"
    write-host "  - MSYS2 and Make"
    write-host ""
    write-host "It will also add these directories to the system PATH:"
    write-host "  - $env:ProgramFiles\Git\bin"
    write-host "  - $env:ProgramFiles\CMake\bin"
    write-host "  - $env:LocalAppData\ninja-build"
    write-host "  - C:\msys64\usr\bin"
    write-host ""

    if (-not (read-yesno "Continue?")) {
        write-host "Setup cancelled."
        exit 0
    }
}

$results = refresh-versioninfo

if (-not ($results | Where-Object { $_.IsOutdated })) {
    write-host "$(get-unicodechar 0x2705) CMake, Ninja, Git Bash, MSVC, and MSYS2 are already up to date!"
}

if (-not (test-isadmin)) {
    write-host "$(get-unicodechar 0x26a0) warning: admin rights required to install cmake/ninja." -foregroundcolor yellow
    write-host "🚀 restarting as administrator..."
    
    start-process powershell -argumentlist "-file `"$pscommandpath`" -Confirmed" -verb runas
    exit
}

$cmake, $ninja, $git, $msys2, $msvc = refresh-versioninfo

$outdated = $results | Where-Object { $_.IsOutdated }
if ($outdated -and $didFailToFetchVersions) {
    Write-Host "$(get-unicodechar 0x26A0) Failed to fetch latest versions. This may be due to GitHub rate limiting."
    Write-Host "$(get-unicodechar 0x26A0) All tools will now be (re)installed to ensure a clean setup."
    Write-Host "$(get-unicodechar 0x26A0) If you're being rate limited by GitHub the installers will fail (obviously)."
    if (-not (read-yesno "Do you want to continue?")) {
        Write-Host "0x1F92C Aborting setup."
        exit 1
    }
}
if (confirm-dependency-change $cmake) { install-cmake }
if (confirm-dependency-change $ninja) { install-ninja }
if (confirm-dependency-change $git)   { install-gitbash }
if (confirm-dependency-change $msvc)  { install-msvc $msvc }
if (confirm-dependency-change $msys2) { install-msys2 }
install-make

write-host "$(get-unicodechar 0x1f4e6) Updating PATH..."
add-to-path "$env:ProgramFiles\git\bin"
add-to-path "$env:ProgramFiles\CMake\bin"
add-to-path "$env:localappdata\ninja-build"
add-to-path "C:\msys64\usr\bin"

write-host "$(get-unicodechar 0x1F501) Refreshing PATH from system environment..."
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
            [System.Environment]::GetEnvironmentVariable("Path", "User")


write-host ""
$finalResults = refresh-versioninfo
foreach ($r in $finalResults) {
    if ($script:declinedUpdates.ContainsKey($r.Name)) {
        write-host "$(get-unicodechar 0x1f4a1) $($r.Name) update was skipped. Installed: $($r.Installed), Latest: $($r.Latest)"
    } elseif ($r.IsOutdated) {
        write-host "$(get-unicodechar 0x274C) $($r.Name) is still outdated. Installed: $($r.Installed), Latest: $($r.Latest)" -foregroundcolor red
    } else {
        write-host "$(get-unicodechar 0x2705) $($r.Name) is now up to date! (v$r.Installed)"
    }
}

if ($finalResults | Where-Object { $_.IsOutdated -and -not $script:declinedUpdates.ContainsKey($_.Name) }) {
    write-host ""
    write-host "$(get-unicodechar 0x274C) Some tools failed to install or update. You must install these manually. See README for help." -foregroundcolor red
    write-host ""
    write-host "$(get-unicodechar 0x274C) Some tools failed to install or update. You must install these manually. See README for help." -foregroundcolor red
    write-host ""
    write-host "$(get-unicodechar 0x274C) Some tools failed to install or update. You must install these manually. See README for help." -foregroundcolor red
    write-host ""
    write-host "$(get-unicodechar 0x274C) Some tools failed to install or update. You must install these manually. See README for help." -foregroundcolor red
    write-host ""
    write-host "$(get-unicodechar 0x274C) Some tools failed to install or update. You must install these manually. See README for help." -foregroundcolor red
    write-host ""
    write-host "$(get-unicodechar 0x274C) Some tools failed to install or update. You must install these manually. See README for help." -foregroundcolor red
    write-host ""
    write-host "$(get-unicodechar 0x274C) Some tools failed to install or update. You must install these manually. See README for help." -foregroundcolor red
    write-host ""
} elseif ($script:declinedUpdates.Count -gt 0) {
    write-host "$(get-unicodechar 0x2705) Setup complete. Declined dependency updates were left unchanged." -foregroundcolor green
} else {
    write-host "$(get-unicodechar 0x2705) All tools installed and up to date!" -foregroundcolor green
}

write-host ""
write-host "$(get-unicodechar 0x1f91d) Press any key to exit..." -foregroundcolor cyan
$null = $host.ui.rawui.readkey("noecho,includekeydown")
exit
