$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$build = Join-Path $root "build"
if (Test-Path $build) { Remove-Item $build -Recurse -Force }
New-Item -ItemType Directory -Path $build | Out-Null

function Run {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed" }
}

Push-Location (Join-Path $root "payload")
Run llvm-dlltool @("-m", "i386:x86-64", "-d", "kernel32.def", "-l", "$build\kernel32.lib")
Run clang @("-x", "c++", "-std=c++17", "--target=x86_64-pc-windows-msvc", "-O2", "-ffreestanding", "-fno-builtin", "-fno-exceptions", "-fno-rtti", "-fno-stack-protector", "-mno-stack-arg-probe", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-c", "payload.cpp", "-o", "$build\payload.obj")
Run lld-link @("/dll", "/nodefaultlib", "/entry:DllMain", "/subsystem:windows", "/machine:x64", "/opt:ref", "/out:$build\payload.dll", "$build\payload.obj", "$build\kernel32.lib")
Pop-Location

Push-Location (Join-Path $root "panel")
Run llvm-rc @("/FO", "$build\app.res", "app.rc")
Run llvm-cvtres @("/machine:x64", "/out:app.syso", "$build\app.res")
Copy-Item "$build\payload.dll" "payload.dll"
$env:GOOS = "windows"
$env:GOARCH = "amd64"
$env:CGO_ENABLED = "0"
Run go @("build", "-trimpath", "-ldflags", "-H=windowsgui -s -w", "-o", "$root\CombatAssistant.exe", ".")
Remove-Item "payload.dll", "app.syso" -Force
Pop-Location
