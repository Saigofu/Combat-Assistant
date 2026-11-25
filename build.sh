#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
build="$root/build"
rm -rf "$build"
mkdir -p "$build"
cd "$root/payload"
llvm-dlltool -m i386:x86-64 -d kernel32.def -l "$build/kernel32.lib"
clang -x c++ -std=c++17 --target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -mno-stack-arg-probe -fno-unwind-tables -fno-asynchronous-unwind-tables -c payload.cpp -o "$build/payload.obj"
lld-link /dll /nodefaultlib /entry:DllMain /subsystem:windows /machine:x64 /opt:ref /out:"$build/payload.dll" "$build/payload.obj" "$build/kernel32.lib"
cd "$root/panel"
llvm-rc /FO "$build/app.res" app.rc
llvm-cvtres /machine:x64 /out:app.syso "$build/app.res"
cp "$build/payload.dll" payload.dll
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -ldflags "-H=windowsgui -s -w" -o "$root/CombatAssistant.exe" .
rm -f payload.dll app.syso
