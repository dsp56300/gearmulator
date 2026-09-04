param (
    [switch]$Elevated,
    [string]$ConfigPath
)

$ErrorActionPreference = "Stop"

if (-not $Elevated) {
    $stateDir = $env:GEARMULATOR_INSTALL_STATE_DIR
    if (-not $stateDir) {
        Write-Error "Windows install state directory is not configured."
        exit 1
    }

    New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
    $stateName = ".install-$PID-$([System.IO.Path]::GetRandomFileName())"
    $configPath = Join-Path $stateDir "$stateName.json"
    $logPath = Join-Path $stateDir "$stateName.log"
    $config = @{
        ProductsDir = $env:GEARMULATOR_INSTALL_PRODUCTS_DIR
        Products = @($env:GEARMULATOR_INSTALL_PRODUCTS -split ' ' | Where-Object { $_ })
        Formats = @($env:GEARMULATOR_INSTALL_FORMATS -split ' ' | Where-Object { $_ })
        Destinations = @{
            VST3 = $env:GEARMULATOR_INSTALL_VST3_DIR
            VST2 = $env:GEARMULATOR_INSTALL_VST2_DIR
            CLAP = $env:GEARMULATOR_INSTALL_CLAP_DIR
            LV2 = $env:GEARMULATOR_INSTALL_LV2_DIR
            Standalone = $env:GEARMULATOR_INSTALL_APP_DIR
        }
        LogPath = $logPath
    }

    if (-not $config.ProductsDir -or $config.Products.Count -eq 0 -or $config.Formats.Count -eq 0) {
        Write-Error "Windows install configuration is incomplete."
        exit 1
    }

    $config | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $configPath -Encoding UTF8

    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-Elevated",
        "-ConfigPath", "`"$configPath`""
    )

    try {
        $process = Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -Wait -PassThru -ArgumentList $arguments
    } catch {
        Write-Error "Installation requires administrator privileges. The elevation request was declined or could not be started."
        Remove-Item -LiteralPath $configPath -Force -ErrorAction SilentlyContinue
        exit 1
    }

    if (Test-Path -LiteralPath $logPath) {
        Get-Content -LiteralPath $logPath
    }

    Remove-Item -LiteralPath $configPath, $logPath -Force -ErrorAction SilentlyContinue
    exit $process.ExitCode
}

try {
    if (-not $ConfigPath -or -not (Test-Path -LiteralPath $ConfigPath)) {
        throw "Install configuration was not provided to the elevated process."
    }

    $config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
    $productsDir = $config.ProductsDir
    $products = @($config.Products)
    $formats = @($config.Formats)

    $formatDetails = @{
        VST3 = @{ Folder = "VST3"; Extension = ".vst3"; Destination = $config.Destinations.VST3 }
        VST2 = @{ Folder = "VST"; Extension = ".dll"; Destination = $config.Destinations.VST2 }
        CLAP = @{ Folder = "CLAP"; Extension = ".clap"; Destination = $config.Destinations.CLAP }
        LV2 = @{ Folder = "LV2"; Extension = ".lv2"; Destination = $config.Destinations.LV2 }
        Standalone = @{ Folder = "Standalone"; Extension = ".exe"; Destination = $config.Destinations.Standalone }
    }

    foreach ($product in $products) {
        foreach ($format in $formats) {
            $details = $formatDetails[$format]
            if (-not $details) {
                throw "Unsupported Windows install format: $format"
            }

            $name = "$product$($details.Extension)"
            $source = Join-Path (Join-Path $productsDir $details.Folder) $name
            $destinationDir = $details.Destination
            $destination = Join-Path $destinationDir $name

            if (-not (Test-Path -LiteralPath $source)) {
                throw "Built artifact not found: $source"
            }

            New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
            "Installing $source -> $destination" | Tee-Object -FilePath $config.LogPath -Append

            if (Test-Path -LiteralPath $source -PathType Container) {
                Copy-Item -LiteralPath $source -Destination $destinationDir -Recurse -Force
            } else {
                Copy-Item -LiteralPath $source -Destination $destination -Force
            }
        }
    }
} catch {
    $message = "Installation failed: $($_.Exception.Message)"
    if ($config -and $config.LogPath) {
        $message | Out-File -FilePath $config.LogPath -Append -Encoding UTF8
    }
    Write-Error $message
    exit 1
}
