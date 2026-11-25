# Combat Assistant - Auto Block and Perfect Dodge

A standalone trainer for Phantom Blade Zero that blocks, parries and dodges automatically. It is a single .exe: it finds the running game, connects to it and lets you toggle three options.

## What it does

- **Auto Block**: keeps the game's perfect parry window open, so every deflect is a perfect parry.
- **Auto Perfect Dodge**: makes the game's own automatic dodge assist always trigger for attacks that must be dodged.
- **Auto Dodge**: also dodges ordinary attacks. It has no effect while Auto Block is on.

Closing the program restores everything it changed.

## How it works

The program has two parts.

- `panel/` is a Go program with a native Win32 window. It finds the real `PBZ.exe` process (the one with the largest main module), writes the embedded `payload.dll` to the temp folder and loads it into the game with `CreateRemoteThread` and `LoadLibraryW`. It talks to the DLL through a named shared memory block (`Local\CombatAssistantShared`) that carries the toggle bitmask, a heartbeat and a status stage.
- `payload/` is a freestanding C++ DLL (no C runtime). Inside the game it:
  1. locates `FName::ToString`, `FName::FName` and `GUObjectArray` by byte signatures in the game image, and `ProcessEvent` through the UObject vtable;
  2. finds the player and its deflect and dodge passives, plus the three Blueprint functions that implement the game's built-in assist (`Try Auto Dodge`, `Can Activate Ability On Frame`, `Try Activate Stance Ability On Frame`);
  3. hooks the game engine tick (a vtable slot) so every change is made on the game thread;
  4. patches a few bytes of those Blueprint scripts (a tag check and a random-chance constant) and sets the passive's perfect and normal window values, and restores them when a toggle is switched off or the panel closes.

The program never touches the network and never writes to save files. The only file it creates is the extracted DLL in the temp folder, plus a log at `%TEMP%\CombatAssistant.log`.

## Building

Requirements (Windows 10/11, 64-bit):

- Go 1.24 or newer: https://go.dev/dl/
- LLVM 18 or newer: https://github.com/llvm/llvm-project/releases (the Windows installer, with "Add LLVM to the system PATH" ticked). The build needs `clang`, `lld-link`, `llvm-dlltool`, `llvm-rc` and `llvm-cvtres`.

Open PowerShell in the repository folder and run:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

The result is `CombatAssistant.exe` in the repository folder. On Linux or macOS, `./build.sh` does the same.

The build has four steps: import library for `kernel32`, freestanding DLL (`payload.dll`), icon resource (`app.syso`), and the Go program, which embeds the DLL with `go:embed`.

## Usage

1. Start Phantom Blade Zero and load into the game.
2. Run `CombatAssistant.exe` (as administrator if the status line asks for it).
3. When the status line says "assist active", the options are working.

Tested only against the current Steam build of the game. A game update can move the code the DLL looks for, in which case the status line shows "Game data not recognised" and nothing is changed.

## License

MIT. See `LICENSE`.
