param(
    [string]$SoundSourceDir = "",
    [string]$FfmpegPath = "",
    [switch]$Deploy
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDirectory = Join-Path $projectRoot "build"
$distributionDirectory = Join-Path $projectRoot "dist"
if (-not $SoundSourceDir) {
    $SoundSourceDir = Join-Path $projectRoot "assets"
}
$SoundSourceDir = [IO.Path]::GetFullPath($SoundSourceDir)

$hornFiles = @(
    "Car.wav", "Truck.wav", "Classic.wav", "Train.wav", "Warthog.wav",
    "Bicycle.wav", "Reversing.wav", "Police.wav", "Boat.wav", "Doom.wav",
    "Doorbell.wav", "Honk.wav", "Horse.wav", "Quack.wav", "Rooster.wav",
    "Starter.wav", "Teams.wav", "WindowsXP.wav", "Wololo.wav", "Augh.wav",
    "Bad to the bone.wav", "Bruh.wav", "DejaVu.wav", "Discord.wav", "DJ.wav",
    "Goofy.wav", "Lego.wav", "MGS.wav", "Social credit.wav", "Spiderman.wav",
    "Wow.wav"
)

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmakePath = $null
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
}
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installationPath = & $vswhere -latest -products "*" -property installationPath
        $candidate = Join-Path $installationPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path -LiteralPath $candidate) {
            $cmakePath = $candidate
        }
    }
}
if (-not $cmakePath) {
    throw "CMake was not found. Install CMake or the Visual Studio CMake component."
}

New-Item -ItemType Directory -Force -Path $distributionDirectory | Out-Null
$converter = Join-Path $projectRoot "tools\convert_audio.ps1"
if ($FfmpegPath) {
    & $converter -FfmpegPath $FfmpegPath -OutputDir $SoundSourceDir
}
else {
    & $converter -OutputDir $SoundSourceDir
}
Remove-Item -LiteralPath (Join-Path $SoundSourceDir "Car1.wav") `
    -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $SoundSourceDir "Truck1.wav") `
    -Force -ErrorAction SilentlyContinue

foreach ($sound in $hornFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $SoundSourceDir $sound))) {
        throw "Required horn sound not found: $(Join-Path $SoundSourceDir $sound)"
    }
}

& $cmakePath -S $projectRoot -B $buildDirectory -A x64
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

& $cmakePath --build $buildDirectory --config Release
if ($LASTEXITCODE -ne 0) {
    throw "HonkHonk compilation failed with exit code $LASTEXITCODE."
}

$audioValidator = Join-Path $buildDirectory "Release\HonkHonkAudioValidation.exe"
& $audioValidator `
    @($hornFiles | ForEach-Object {
        Join-Path $SoundSourceDir $_
    })
if ($LASTEXITCODE -ne 0) {
    throw "Horn WAV validation failed with exit code $LASTEXITCODE."
}

Copy-Item -LiteralPath (Join-Path $projectRoot "HonkHonk.ini") `
    -Destination (Join-Path $distributionDirectory "HonkHonk.ini") -Force
$soundDirectory = Join-Path $distributionDirectory "HonkHonk"
New-Item -ItemType Directory -Force -Path $soundDirectory | Out-Null
Remove-Item -LiteralPath (Join-Path $soundDirectory "Car1.wav") `
    -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $soundDirectory "Truck1.wav") `
    -Force -ErrorAction SilentlyContinue
foreach ($sound in $hornFiles) {
    Copy-Item -LiteralPath (Join-Path $SoundSourceDir $sound) `
        -Destination (Join-Path $soundDirectory $sound) -Force
}

if ($Deploy) {
    $azimuthDirectory = Split-Path -Parent $projectRoot
    $modsDirectory = Join-Path $azimuthDirectory "fh2-mods"
    if (Get-Process -Name "azimuth" -ErrorAction SilentlyContinue) {
        throw "Close Azimuth / the game before deploying the DLL."
    }
    $newIni = Join-Path $modsDirectory "HonkHonk.ini"
    $legacyIni = Join-Path $modsDirectory "HonkHorn.ini"
    if (-not (Test-Path -LiteralPath $newIni)) {
        if (Test-Path -LiteralPath $legacyIni) {
            $legacyText = [IO.File]::ReadAllText($legacyIni)
            $migratedText = $legacyText.Replace("[HonkHorn]", "[HonkHonk]")
            [IO.File]::WriteAllText(
                $newIni, $migratedText, [Text.UTF8Encoding]::new($false))
        }
        else {
            Copy-Item -LiteralPath (Join-Path $distributionDirectory "HonkHonk.ini") `
                -Destination $newIni
        }
    }
    Copy-Item -LiteralPath (Join-Path $distributionDirectory "HonkHonk.dll") `
        -Destination (Join-Path $modsDirectory "HonkHonk.dll") -Force
    $installedSoundDirectory = Join-Path $modsDirectory "HonkHonk"
    New-Item -ItemType Directory -Force -Path $installedSoundDirectory | Out-Null
    foreach ($sound in $hornFiles) {
        Copy-Item -LiteralPath (Join-Path $soundDirectory $sound) `
            -Destination (Join-Path $installedSoundDirectory $sound) -Force
    }
    Remove-Item -LiteralPath (Join-Path $installedSoundDirectory "Car1.wav") `
        -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $installedSoundDirectory "Truck1.wav") `
        -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $installedSoundDirectory "HonkHonk") `
        -Recurse -Force -ErrorAction SilentlyContinue

    Remove-Item -LiteralPath (Join-Path $modsDirectory "HonkHorn.dll") `
        -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $legacyIni -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $modsDirectory "HonkHorn") `
        -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Build complete. Distribution files are in '$distributionDirectory'."
