package main

import (
	"crypto/sha256"
	_ "embed"
	"encoding/hex"
	"math"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync/atomic"
	"syscall"
	"unsafe"
)

//go:embed payload.dll
var payload []byte

const (
	gameProcess = "PBZ.exe"
	windowTitle = "Combat Assistant - Auto Block and Perfect Dodge"
	className   = "CombatAssistantWindow"
	timerID     = 1
	timerMs     = 1500
	clientW     = 520
	clientH     = 448
)

const (
	wmCreate      = 0x0001
	wmDestroy     = 0x0002
	wmPaint       = 0x000F
	wmEraseBkgnd  = 0x0014
	wmTimer       = 0x0113
	wmLButtonDown = 0x0201

	csVRedraw = 0x0001
	csHRedraw = 0x0002

	wsMinimizeBox = 0x00020000
	wsSysMenu     = 0x00080000
	wsCaption     = 0x00C00000

	swShow            = 5
	srcCopy           = 0x00CC0020
	idcArrow          = 32512
	bkTransparent     = 1
	th32csSnapProcess = 2
	th32csSnapModules = 0x18
	gameAccess        = 0x1000 | 0x0002 | 0x0008 | 0x0010 | 0x0020
	stillActive       = 259
	iconResource      = 1
	imageIcon         = 1
	smCxIcon          = 11
	smCyIcon          = 12
	smCxSmIcon        = 49
	smCySmIcon        = 50
	sharedName        = "Local\\CombatAssistantShared"
	sharedMagic       = 0x4D414343
	sharedSize        = 40
	fileMapAll        = 0xF001F
	pageReadWrite     = 0x04
	memCommitReserve  = 0x3000
	memRelease        = 0x8000
	stallLimit        = 4

	dtLeft        = 0x0000
	dtCenter      = 0x0001
	dtRight       = 0x0002
	dtVCenter     = 0x0004
	dtSingleLine  = 0x0020
	dtNoPrefix    = 0x0800
	dtEndEllipsis = 0x8000
)

type rect struct{ Left, Top, Right, Bottom int32 }

type point struct{ X, Y int32 }

type msg struct {
	Hwnd    uintptr
	Message uint32
	WParam  uintptr
	LParam  uintptr
	Time    uint32
	Pt      point
}

type paintStruct struct {
	Hdc         uintptr
	Erase       int32
	Paint       rect
	Restore     int32
	IncUpdate   int32
	rgbReserved [32]byte
}

type wndClassEx struct {
	Size       uint32
	Style      uint32
	WndProc    uintptr
	ClsExtra   int32
	WndExtra   int32
	Instance   uintptr
	Icon       uintptr
	Cursor     uintptr
	Background uintptr
	MenuName   *uint16
	ClassName  *uint16
	IconSm     uintptr
}

type processEntry struct {
	Size            uint32
	Usage           uint32
	ProcessID       uint32
	DefaultHeapID   uintptr
	ModuleID        uint32
	Threads         uint32
	ParentProcessID uint32
	PriClassBase    int32
	Flags           uint32
	ExeFile         [260]uint16
}

type gameState int

const (
	gameWaiting gameState = iota
	gameDenied
	gameFailed
	gameLinked
)

type sharedBlock struct {
	Magic     uint32
	Version   uint32
	Toggles   uint32
	Stage     uint32
	Heartbeat uint32
	Error     uint32
	Active    uint32
	Reserved  uint32
	Player    uint64
}

type moduleEntry struct {
	Size         uint32
	ModuleID     uint32
	ProcessID    uint32
	GlblcntUsage uint32
	ProccntUsage uint32
	BaseAddr     uintptr
	BaseSize     uint32
	Module       uintptr
	ModuleName   [256]uint16
	ExePath      [260]uint16
}

type feature struct {
	key   string
	title string
	desc  string
	on    bool
}

var (
	user32   = syscall.NewLazyDLL("user32.dll")
	gdi32    = syscall.NewLazyDLL("gdi32.dll")
	kernel32 = syscall.NewLazyDLL("kernel32.dll")

	pSetProcessDPIAware = user32.NewProc("SetProcessDPIAware")
	pGetDpiForSystem    = user32.NewProc("GetDpiForSystem")
	pRegisterClassEx    = user32.NewProc("RegisterClassExW")
	pCreateWindowEx     = user32.NewProc("CreateWindowExW")
	pAdjustWindowRectEx = user32.NewProc("AdjustWindowRectEx")
	pGetSystemMetrics   = user32.NewProc("GetSystemMetrics")
	pShowWindow         = user32.NewProc("ShowWindow")
	pUpdateWindow       = user32.NewProc("UpdateWindow")
	pGetMessage         = user32.NewProc("GetMessageW")
	pTranslateMessage   = user32.NewProc("TranslateMessage")
	pDispatchMessage    = user32.NewProc("DispatchMessageW")
	pDefWindowProc      = user32.NewProc("DefWindowProcW")
	pPostQuitMessage    = user32.NewProc("PostQuitMessage")
	pLoadCursor         = user32.NewProc("LoadCursorW")
	pLoadImage          = user32.NewProc("LoadImageW")
	pSetTimer           = user32.NewProc("SetTimer")
	pKillTimer          = user32.NewProc("KillTimer")
	pBeginPaint         = user32.NewProc("BeginPaint")
	pEndPaint           = user32.NewProc("EndPaint")
	pGetClientRect      = user32.NewProc("GetClientRect")
	pInvalidateRect     = user32.NewProc("InvalidateRect")
	pFillRect           = user32.NewProc("FillRect")
	pDrawText           = user32.NewProc("DrawTextW")

	pCreateCompatibleDC     = gdi32.NewProc("CreateCompatibleDC")
	pCreateCompatibleBitmap = gdi32.NewProc("CreateCompatibleBitmap")
	pSelectObject           = gdi32.NewProc("SelectObject")
	pDeleteObject           = gdi32.NewProc("DeleteObject")
	pDeleteDC               = gdi32.NewProc("DeleteDC")
	pBitBlt                 = gdi32.NewProc("BitBlt")
	pCreateSolidBrush       = gdi32.NewProc("CreateSolidBrush")
	pCreatePen              = gdi32.NewProc("CreatePen")
	pRoundRect              = gdi32.NewProc("RoundRect")
	pEllipse                = gdi32.NewProc("Ellipse")
	pSetBkMode              = gdi32.NewProc("SetBkMode")
	pSetTextColor           = gdi32.NewProc("SetTextColor")
	pCreateFont             = gdi32.NewProc("CreateFontW")

	pGetModuleHandle          = kernel32.NewProc("GetModuleHandleW")
	pCreateToolhelp32Snapshot = kernel32.NewProc("CreateToolhelp32Snapshot")
	pProcess32First           = kernel32.NewProc("Process32FirstW")
	pProcess32Next            = kernel32.NewProc("Process32NextW")
	pCloseHandle              = kernel32.NewProc("CloseHandle")
	pModule32First            = kernel32.NewProc("Module32FirstW")
	pOpenProcess              = kernel32.NewProc("OpenProcess")
	pGetExitCodeProcess       = kernel32.NewProc("GetExitCodeProcess")
	pCreateFileMapping        = kernel32.NewProc("CreateFileMappingW")
	pMapViewOfFile            = kernel32.NewProc("MapViewOfFile")
	pVirtualAllocEx           = kernel32.NewProc("VirtualAllocEx")
	pVirtualFreeEx            = kernel32.NewProc("VirtualFreeEx")
	pWriteProcessMemory       = kernel32.NewProc("WriteProcessMemory")
	pCreateRemoteThread       = kernel32.NewProc("CreateRemoteThread")
	pWaitForSingleObject      = kernel32.NewProc("WaitForSingleObject")
	pGetExitCodeThread        = kernel32.NewProc("GetExitCodeThread")
	pLoadLibrary              = kernel32.NewProc("LoadLibraryW")
)

var (
	colBg        = rgb(16, 19, 26)
	colCard      = rgb(24, 28, 38)
	colBorder    = rgb(42, 48, 62)
	colText      = rgb(232, 236, 242)
	colMuted     = rgb(136, 146, 164)
	colAccent    = rgb(45, 200, 165)
	colAccentDim = rgb(30, 112, 98)
	colTrackOff  = rgb(54, 60, 76)
	colKnob      = rgb(244, 247, 250)
	colWarning   = rgb(230, 104, 92)
	colSignature = rgb(62, 70, 88)
)

var (
	features = []feature{
		{"AutoBlock", "Auto Block", "Blocks and lands perfect parries automatically", true},
		{"AutoPerfectDodge", "Auto Perfect Dodge", "Dodges at the perfect moment before a hit lands", true},
		{"AutoDodge", "Auto Dodge", "Also dodges ordinary attacks. Off lets you dodge", true},
	}
	scale      = 1.0
	gameStatus gameState
	gameHandle uintptr
	shared     *sharedBlock
	lastBeat   uint32
	stalled    int
	payloadDll string
	fontTitle  uintptr
	fontBody   uintptr
	fontSmall  uintptr
)

func init() {
	runtime.LockOSThread()
}

func rgb(r, g, b uint32) uintptr {
	return uintptr(r | g<<8 | b<<16)
}

func px(v int) int32 {
	return int32(math.Round(float64(v) * scale))
}

func mainModuleSize(pid uint32) uint32 {
	snap, _, _ := pCreateToolhelp32Snapshot.Call(th32csSnapModules, uintptr(pid))
	if snap == ^uintptr(0) {
		return 0
	}
	defer pCloseHandle.Call(snap)
	var m moduleEntry
	m.Size = uint32(unsafe.Sizeof(m))
	ok, _, _ := pModule32First.Call(snap, uintptr(unsafe.Pointer(&m)))
	if ok == 0 {
		return 0
	}
	return m.BaseSize
}

func findGameProcess() uint32 {
	snap, _, _ := pCreateToolhelp32Snapshot.Call(th32csSnapProcess, 0)
	if snap == ^uintptr(0) {
		return 0
	}
	defer pCloseHandle.Call(snap)
	var e processEntry
	e.Size = uint32(unsafe.Sizeof(e))
	var best, bestSize uint32
	ok, _, _ := pProcess32First.Call(snap, uintptr(unsafe.Pointer(&e)))
	for ok != 0 {
		if strings.EqualFold(syscall.UTF16ToString(e.ExeFile[:]), gameProcess) {
			if size := mainModuleSize(e.ProcessID); size > bestSize {
				best, bestSize = e.ProcessID, size
			}
		}
		ok, _, _ = pProcess32Next.Call(snap, uintptr(unsafe.Pointer(&e)))
	}
	return best
}

func openShared() bool {
	name, _ := syscall.UTF16PtrFromString(sharedName)
	mapping, _, _ := pCreateFileMapping.Call(^uintptr(0), 0, pageReadWrite, 0, sharedSize, uintptr(unsafe.Pointer(name)))
	if mapping == 0 {
		return false
	}
	view, _, _ := pMapViewOfFile.Call(mapping, fileMapAll, 0, 0, sharedSize)
	if view == 0 {
		return false
	}
	shared = (*sharedBlock)(unsafe.Pointer(view))
	atomic.StoreUint32(&shared.Magic, sharedMagic)
	return true
}

func togglesMask() uint32 {
	var mask uint32
	for i, f := range features {
		if f.on {
			mask |= 1 << uint(i)
		}
	}
	return mask
}

func syncToggles() {
	atomic.StoreUint32(&shared.Toggles, togglesMask())
}

func payloadPath() (string, error) {
	if payloadDll != "" {
		return payloadDll, nil
	}
	sum := sha256.Sum256(payload)
	path := filepath.Join(os.TempDir(), "CombatAssistant_"+hex.EncodeToString(sum[:4])+".dll")
	if err := os.WriteFile(path, payload, 0o644); err != nil {
		return "", err
	}
	payloadDll = path
	return path, nil
}

func inject(process uintptr) bool {
	path, err := payloadPath()
	if err != nil {
		return false
	}
	wide, err := syscall.UTF16FromString(path)
	if err != nil {
		return false
	}
	size := uintptr(len(wide) * 2)
	remote, _, _ := pVirtualAllocEx.Call(process, 0, size, memCommitReserve, pageReadWrite)
	if remote == 0 {
		return false
	}
	defer pVirtualFreeEx.Call(process, remote, 0, memRelease)
	var written uintptr
	ok, _, _ := pWriteProcessMemory.Call(process, remote, uintptr(unsafe.Pointer(&wide[0])), size, uintptr(unsafe.Pointer(&written)))
	if ok == 0 || written != size {
		return false
	}
	thread, _, _ := pCreateRemoteThread.Call(process, 0, 0, pLoadLibrary.Addr(), remote, 0, 0)
	if thread == 0 {
		return false
	}
	defer pCloseHandle.Call(thread)
	if wait, _, _ := pWaitForSingleObject.Call(thread, 5000); wait != 0 {
		return false
	}
	var module uint32
	pGetExitCodeThread.Call(thread, uintptr(unsafe.Pointer(&module)))
	return module != 0
}

func resetLink() {
	atomic.StoreUint32(&shared.Stage, 0)
	atomic.StoreUint32(&shared.Error, 0)
	atomic.StoreUint32(&shared.Heartbeat, 0)
	atomic.StoreUint64(&shared.Player, 0)
	lastBeat = 0
	stalled = 0
}

func refreshGame() gameState {
	if gameHandle != 0 {
		var code uint32
		pGetExitCodeProcess.Call(gameHandle, uintptr(unsafe.Pointer(&code)))
		if code != stillActive {
			pCloseHandle.Call(gameHandle)
			gameHandle = 0
			resetLink()
		}
	}
	if gameHandle == 0 {
		pid := findGameProcess()
		if pid == 0 {
			return gameWaiting
		}
		h, _, _ := pOpenProcess.Call(gameAccess, 0, uintptr(pid))
		if h == 0 {
			return gameDenied
		}
		resetLink()
		if !inject(h) {
			pCloseHandle.Call(h)
			return gameFailed
		}
		gameHandle = h
	}
	beat := atomic.LoadUint32(&shared.Heartbeat)
	if beat != lastBeat {
		lastBeat = beat
		stalled = 0
	} else if stalled < stallLimit {
		stalled++
	}
	return gameLinked
}

func linkText() (string, uintptr) {
	switch gameStatus {
	case gameDenied:
		return gameProcess + " found - run as administrator", colWarning
	case gameFailed:
		return "Could not attach to " + gameProcess, colWarning
	case gameLinked:
		switch {
		case stalled >= stallLimit && lastBeat == 0:
			return "Connecting to " + gameProcess, colMuted
		case stalled >= stallLimit:
			return "Connection to " + gameProcess + " lost", colWarning
		case atomic.LoadUint32(&shared.Error) != 0:
			return "Game data not recognised (code " + itoa(atomic.LoadUint32(&shared.Error)) + ")", colWarning
		}
		switch atomic.LoadUint32(&shared.Stage) {
		case 3:
			return "Connected to " + gameProcess + " - waiting for the player", colAccent
		case 4:
			return "Connected to " + gameProcess + " - preparing", colAccent
		case 5:
			if atomic.LoadUint32(&shared.Active) != 0 {
				return "Connected to " + gameProcess + " - assist active", colAccent
			}
			return "Connected to " + gameProcess + " - ready", colAccent
		}
		return "Connected to " + gameProcess + " - loading", colAccent
	}
	return "Waiting for " + gameProcess, colMuted
}

func itoa(v uint32) string {
	if v == 0 {
		return "0"
	}
	var b [10]byte
	i := len(b)
	for v > 0 {
		i--
		b[i] = byte('0' + v%10)
		v /= 10
	}
	return string(b[i:])
}

func cardRect(i int) rect {
	top := px(104 + 84*i)
	return rect{px(28), top, px(492), top + px(72)}
}

func toggleRect(card rect) rect {
	return rect{card.Right - px(72), card.Top + px(22), card.Right - px(20), card.Top + px(50)}
}

func enableRect() rect { return rect{px(28), px(364), px(168), px(400)} }

func stopRect() rect { return rect{px(180), px(364), px(320), px(400)} }

func contains(r rect, x, y int32) bool {
	return x >= r.Left && x < r.Right && y >= r.Top && y < r.Bottom
}

func makeFont(height int, weight uintptr) uintptr {
	face, _ := syscall.UTF16PtrFromString("Segoe UI")
	f, _, _ := pCreateFont.Call(
		uintptr(-px(height)), 0, 0, 0, weight, 0, 0, 0,
		1, 0, 0, 5, 0, uintptr(unsafe.Pointer(face)),
	)
	return f
}

func solid(dc uintptr, r rect, color uintptr) {
	brush, _, _ := pCreateSolidBrush.Call(color)
	pFillRect.Call(dc, uintptr(unsafe.Pointer(&r)), brush)
	pDeleteObject.Call(brush)
}

func roundRect(dc uintptr, r rect, fill, line uintptr, radius int32) {
	brush, _, _ := pCreateSolidBrush.Call(fill)
	pen, _, _ := pCreatePen.Call(0, 1, line)
	oldBrush, _, _ := pSelectObject.Call(dc, brush)
	oldPen, _, _ := pSelectObject.Call(dc, pen)
	pRoundRect.Call(dc, uintptr(r.Left), uintptr(r.Top), uintptr(r.Right), uintptr(r.Bottom), uintptr(radius), uintptr(radius))
	pSelectObject.Call(dc, oldBrush)
	pSelectObject.Call(dc, oldPen)
	pDeleteObject.Call(brush)
	pDeleteObject.Call(pen)
}

func ellipse(dc uintptr, r rect, color uintptr) {
	brush, _, _ := pCreateSolidBrush.Call(color)
	pen, _, _ := pCreatePen.Call(0, 1, color)
	oldBrush, _, _ := pSelectObject.Call(dc, brush)
	oldPen, _, _ := pSelectObject.Call(dc, pen)
	pEllipse.Call(dc, uintptr(r.Left), uintptr(r.Top), uintptr(r.Right), uintptr(r.Bottom))
	pSelectObject.Call(dc, oldBrush)
	pSelectObject.Call(dc, oldPen)
	pDeleteObject.Call(brush)
	pDeleteObject.Call(pen)
}

func text(dc uintptr, s string, r rect, font, color, flags uintptr) {
	p, _ := syscall.UTF16PtrFromString(s)
	pSelectObject.Call(dc, font)
	pSetTextColor.Call(dc, color)
	pSetBkMode.Call(dc, bkTransparent)
	pDrawText.Call(dc, uintptr(unsafe.Pointer(p)), ^uintptr(0), uintptr(unsafe.Pointer(&r)), flags|dtSingleLine|dtNoPrefix)
}

func render(dc uintptr, w, h int32) {
	solid(dc, rect{0, 0, w, h}, colBg)
	text(dc, "Combat Assistant", rect{px(28), px(22), px(492), px(58)}, fontTitle, colText, dtLeft|dtVCenter)
	text(dc, "Auto Block and Perfect Dodge for Phantom Blade Zero", rect{px(28), px(60), px(492), px(84)}, fontSmall, colMuted, dtLeft|dtVCenter)

	for i, f := range features {
		card := cardRect(i)
		border := colBorder
		if f.on {
			border = colAccentDim
		}
		roundRect(dc, card, colCard, border, px(14))

		tg := toggleRect(card)
		label := rect{tg.Left - px(48), card.Top + px(22), tg.Left - px(12), card.Top + px(50)}
		textRight := label.Left - px(8)
		text(dc, f.title, rect{card.Left + px(20), card.Top + px(12), textRight, card.Top + px(38)}, fontBody, colText, dtLeft|dtVCenter|dtEndEllipsis)
		text(dc, f.desc, rect{card.Left + px(20), card.Top + px(38), textRight, card.Top + px(60)}, fontSmall, colMuted, dtLeft|dtVCenter|dtEndEllipsis)

		track := colTrackOff
		state := "Off"
		stateColor := colMuted
		knob := rect{tg.Left + px(3), tg.Top + px(3), tg.Left + px(25), tg.Top + px(25)}
		if f.on {
			track = colAccent
			state = "On"
			stateColor = colAccent
			knob = rect{tg.Right - px(25), tg.Top + px(3), tg.Right - px(3), tg.Top + px(25)}
		}
		text(dc, state, label, fontBody, stateColor, dtRight|dtVCenter)
		roundRect(dc, tg, track, track, px(28))
		ellipse(dc, knob, colKnob)
	}

	enable := enableRect()
	roundRect(dc, enable, colCard, colBorder, px(10))
	text(dc, "Enable All", enable, fontBody, colText, dtCenter|dtVCenter)
	stop := stopRect()
	roundRect(dc, stop, colCard, colBorder, px(10))
	text(dc, "Stop All", stop, fontBody, colText, dtCenter|dtVCenter)

	solid(dc, rect{px(28), px(412), px(492), px(413)}, colBorder)
	status, dot := linkText()
	ellipse(dc, rect{px(28), px(426), px(38), px(436)}, dot)
	text(dc, status, rect{px(46), px(420), px(390), px(442)}, fontSmall, colMuted, dtLeft|dtVCenter|dtEndEllipsis)
	text(dc, "by Saigofu", rect{px(392), px(420), px(492), px(442)}, fontSmall, colSignature, dtRight|dtVCenter)
}

func paint(hwnd uintptr) {
	var ps paintStruct
	hdc, _, _ := pBeginPaint.Call(hwnd, uintptr(unsafe.Pointer(&ps)))
	var rc rect
	pGetClientRect.Call(hwnd, uintptr(unsafe.Pointer(&rc)))
	w, h := rc.Right-rc.Left, rc.Bottom-rc.Top
	mem, _, _ := pCreateCompatibleDC.Call(hdc)
	bmp, _, _ := pCreateCompatibleBitmap.Call(hdc, uintptr(w), uintptr(h))
	old, _, _ := pSelectObject.Call(mem, bmp)
	render(mem, w, h)
	pBitBlt.Call(hdc, 0, 0, uintptr(w), uintptr(h), mem, 0, 0, srcCopy)
	pSelectObject.Call(mem, old)
	pDeleteObject.Call(bmp)
	pDeleteDC.Call(mem)
	pEndPaint.Call(hwnd, uintptr(unsafe.Pointer(&ps)))
}

func invalidate(hwnd uintptr) {
	pInvalidateRect.Call(hwnd, 0, 0)
}

func setAll(hwnd uintptr, on bool) {
	for i := range features {
		features[i].on = on
	}
	syncToggles()
	invalidate(hwnd)
}

func click(hwnd uintptr, x, y int32) {
	for i := range features {
		if contains(cardRect(i), x, y) {
			features[i].on = !features[i].on
			syncToggles()
			invalidate(hwnd)
			return
		}
	}
	if contains(enableRect(), x, y) {
		setAll(hwnd, true)
	} else if contains(stopRect(), x, y) {
		setAll(hwnd, false)
	}
}

func wndProc(hwnd, message, wparam, lparam uintptr) uintptr {
	switch message {
	case wmCreate:
		fontTitle = makeFont(26, 600)
		fontBody = makeFont(15, 600)
		fontSmall = makeFont(13, 400)
		pSetTimer.Call(hwnd, timerID, timerMs, 0)
		return 0
	case wmEraseBkgnd:
		return 1
	case wmPaint:
		paint(hwnd)
		return 0
	case wmTimer:
		gameStatus = refreshGame()
		invalidate(hwnd)
		return 0
	case wmLButtonDown:
		click(hwnd, int32(int16(lparam&0xFFFF)), int32(int16((lparam>>16)&0xFFFF)))
		return 0
	case wmDestroy:
		pKillTimer.Call(hwnd, timerID)
		atomic.StoreUint32(&shared.Magic, 0)
		if gameHandle != 0 {
			pCloseHandle.Call(gameHandle)
		}
		pDeleteObject.Call(fontTitle)
		pDeleteObject.Call(fontBody)
		pDeleteObject.Call(fontSmall)
		pPostQuitMessage.Call(0)
		return 0
	}
	r, _, _ := pDefWindowProc.Call(hwnd, message, wparam, lparam)
	return r
}

func main() {
	pSetProcessDPIAware.Call()
	dpi, _, _ := pGetDpiForSystem.Call()
	if dpi == 0 {
		dpi = 96
	}
	scale = float64(dpi) / 96

	if !openShared() {
		return
	}
	syncToggles()
	gameStatus = refreshGame()

	instance, _, _ := pGetModuleHandle.Call(0)
	cursor, _, _ := pLoadCursor.Call(0, idcArrow)
	class, _ := syscall.UTF16PtrFromString(className)
	bigW, _, _ := pGetSystemMetrics.Call(smCxIcon)
	bigH, _, _ := pGetSystemMetrics.Call(smCyIcon)
	smallW, _, _ := pGetSystemMetrics.Call(smCxSmIcon)
	smallH, _, _ := pGetSystemMetrics.Call(smCySmIcon)
	iconBig, _, _ := pLoadImage.Call(instance, iconResource, imageIcon, bigW, bigH, 0)
	iconSmall, _, _ := pLoadImage.Call(instance, iconResource, imageIcon, smallW, smallH, 0)
	wc := wndClassEx{
		Style:     csHRedraw | csVRedraw,
		WndProc:   syscall.NewCallback(wndProc),
		Instance:  instance,
		Icon:      iconBig,
		IconSm:    iconSmall,
		Cursor:    cursor,
		ClassName: class,
	}
	wc.Size = uint32(unsafe.Sizeof(wc))
	pRegisterClassEx.Call(uintptr(unsafe.Pointer(&wc)))

	style := uintptr(wsCaption | wsSysMenu | wsMinimizeBox)
	frame := rect{0, 0, px(clientW), px(clientH)}
	pAdjustWindowRectEx.Call(uintptr(unsafe.Pointer(&frame)), style, 0, 0)
	width, height := frame.Right-frame.Left, frame.Bottom-frame.Top
	screenW, _, _ := pGetSystemMetrics.Call(0)
	screenH, _, _ := pGetSystemMetrics.Call(1)
	x := (int32(screenW) - width) / 2
	y := (int32(screenH) - height) / 2

	title, _ := syscall.UTF16PtrFromString(windowTitle)
	hwnd, _, _ := pCreateWindowEx.Call(
		0, uintptr(unsafe.Pointer(class)), uintptr(unsafe.Pointer(title)), style,
		uintptr(x), uintptr(y), uintptr(width), uintptr(height),
		0, 0, instance, 0,
	)
	pShowWindow.Call(hwnd, swShow)
	pUpdateWindow.Call(hwnd)

	var m msg
	for {
		r, _, _ := pGetMessage.Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if int32(r) <= 0 {
			break
		}
		pTranslateMessage.Call(uintptr(unsafe.Pointer(&m)))
		pDispatchMessage.Call(uintptr(unsafe.Pointer(&m)))
	}
}
