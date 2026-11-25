typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int i32;
typedef long long i64;
typedef wchar_t wc;

extern "C" {
__declspec(dllimport) void *GetModuleHandleW(const wc *);
__declspec(dllimport) void *GetCurrentProcess(void);
__declspec(dllimport) int ReadProcessMemory(void *, const void *, void *, u64, u64 *);
__declspec(dllimport) void *OpenFileMappingW(u32, int, const wc *);
__declspec(dllimport) void *MapViewOfFile(void *, u32, u32, u32, u64);
__declspec(dllimport) void *CreateThread(void *, u64, u32 (*)(void *), void *, u32, u32 *);
__declspec(dllimport) void Sleep(u32);
__declspec(dllimport) void *CreateFileW(const wc *, u32, u32, void *, u32, u32, void *);
__declspec(dllimport) int WriteFile(void *, const void *, u32, u32 *, void *);
__declspec(dllimport) void FreeLibraryAndExitThread(void *, u32);
__declspec(dllimport) int DisableThreadLibraryCalls(void *);
__declspec(dllimport) u32 GetTempPathW(u32, wc *);
__declspec(dllimport) int VirtualProtect(void *, u64, u32, u32 *);
__declspec(dllimport) int WriteProcessMemory(void *, void *, const void *, u64, u64 *);
__declspec(dllimport) u64 GetTickCount64(void);
}

extern "C" int _fltused = 0;

extern "C" __attribute__((optnone, noinline)) void *memset(void *d, int v, u64 n) {
    u8 *p = (u8 *)d;
    for (u64 i = 0; i < n; i++) p[i] = (u8)v;
    return d;
}

extern "C" __attribute__((optnone, noinline)) void *memcpy(void *d, const void *s, u64 n) {
    u8 *p = (u8 *)d;
    const u8 *q = (const u8 *)s;
    for (u64 i = 0; i < n; i++) p[i] = q[i];
    return d;
}

struct Shared {
    volatile u32 magic;
    volatile u32 version;
    volatile u32 toggles;
    volatile u32 stage;
    volatile u32 heartbeat;
    volatile u32 error;
    volatile u32 active;
    volatile u32 reserved;
    volatile u64 player;
};

struct FnTarget {
    const wc *name;
    const wc *owner;
    u32 nameIndex;
    u32 ownerIndex;
    volatile u64 object;
};

enum PatchKind { PatchTag = 0, PatchConst = 1 };

struct Patch {
    u32 fn;
    u32 feature;
    u32 kind;
    u32 start;
    u32 end;
    u32 next;
    u32 length;
    volatile u32 applied;
    volatile u32 dead;
    u64 script;
    u64 offset;
    u8 original[80];
};

struct Pattern {
    u8 bytes[80];
    u8 mask[80];
    u32 length;
    u64 hits[8];
    u32 count;
};

struct Target {
    const wc *name;
    u32 index;
    u64 object[64];
    u64 outer[64];
    u32 count;
};

enum Stage { StageLoaded = 1, StageSignatures = 2, StageWorld = 3, StagePlayer = 4, StageReady = 5 };
enum Error { ErrToString = 1, ErrNameCtor = 2, ErrObjectArray = 3, ErrProcessEvent = 4, ErrPatch = 5, ErrHook = 6 };
enum Feature { FeatureBlock = 1, FeatureDodge = 2, FeatureOrdinary = 4, FeatureAnyDodge = 6 };

static const u32 kItemSize = 0x18;
static const u32 kChunkItems = 0x10000;
static const u32 kScanChunk = 1 << 20;
static const u32 kTickSlot = 94;
static const u32 kProcessEventSlot = 76;

static const u32 kMagic = 0x4D414343;
static const u32 kVersion = 2;

static void *g_module;
static void *g_process;
static void *g_log;
static Shared *g_shared;
static u64 g_base;
static u64 g_size;
static u64 g_toString;
static u64 g_nameCtor;
static u64 g_objects;
static u64 g_table;
static u32 g_numChunks;
static u32 g_numElements;
static u64 g_processEvent;
static u32 g_processEventSlot;
static u64 g_lastPlayer;
static u8 g_scan[kScanChunk + 128];
static u8 g_items[kItemSize * kChunkItems];

static Pattern g_patterns[3];
static Target g_targets[5];
static FnTarget g_fns[4];
static Patch g_patches[5];
static u32 g_functionClass;
static u64 g_slot;
static u64 g_hookInstalled;
static volatile u32 g_failCode;
static u32 g_frame;
static volatile u64 g_drivePlayer;
static volatile u64 g_driveComponent;
static volatile u64 g_drivePassive;
static volatile u64 g_driveDodge;
static u8 g_parmsA[512];
static u8 g_parmsB[512];
typedef void (*TickFn)(void *, float, u8);
static TickFn g_origTick;

static bool rd(u64 address, void *out, u64 size) {
    u64 got = 0;
    return ReadProcessMemory(g_process, (const void *)address, out, size, &got) && got == size;
}

static u64 rd64(u64 address) {
    u64 v = 0;
    rd(address, &v, 8);
    return v;
}

static u32 rd32(u64 address) {
    u32 v = 0;
    rd(address, &v, 4);
    return v;
}

static void put(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    u32 written;
    WriteFile(g_log, s, n, &written, 0);
}

static void put_hex(u64 v) {
    char b[19];
    b[0] = '0';
    b[1] = 'x';
    for (int i = 0; i < 16; i++) {
        u8 d = (u8)((v >> ((15 - i) * 4)) & 15);
        b[2 + i] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    }
    b[18] = 0;
    put(b);
}

static void put_dec(u64 v) {
    char b[24];
    int i = 23;
    b[i] = 0;
    if (!v) b[--i] = '0';
    while (v) {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    put(b + i);
}

static void log_text(const char *s) {
    put(s);
    put("\r\n");
}

static void log_hex(const char *label, u64 v) {
    put(label);
    put(" = ");
    put_hex(v);
    put("\r\n");
}

static void log_dec(const char *label, u64 v) {
    put(label);
    put(" = ");
    put_dec(v);
    put("\r\n");
}

static void log_open() {
    static wc path[300];
    u32 n = GetTempPathW(260, path);
    const char *name = "CombatAssistant.log";
    for (u32 i = 0; name[i]; i++) path[n++] = (wc)name[i];
    path[n] = 0;
    g_log = CreateFileW(path, 0x40000000, 3, 0, 2, 0x80, 0);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void pattern_parse(Pattern *p, const char *s) {
    u32 n = 0;
    while (*s) {
        if (*s == ' ') {
            s++;
        } else if (*s == '?') {
            p->bytes[n] = 0;
            p->mask[n] = 0;
            n++;
            s++;
            if (*s == '?') s++;
        } else {
            p->bytes[n] = (u8)((hex_value(s[0]) << 4) | hex_value(s[1]));
            p->mask[n] = 0xFF;
            n++;
            s += 2;
        }
    }
    p->length = n;
    p->count = 0;
}

static void read_region(u64 address, u8 *out, u64 size) {
    if (rd(address, out, size)) return;
    for (u64 o = 0; o < size; o += 4096) {
        u64 m = size - o < 4096 ? size - o : 4096;
        if (!rd(address + o, out + o, m)) memset(out + o, 0, m);
    }
}

static void scan_image() {
    for (u64 off = 0; off < g_size; off += kScanChunk) {
        u64 n = g_size - off < kScanChunk + 128 ? g_size - off : kScanChunk + 128;
        read_region(g_base + off, g_scan, n);
        u64 limit = off + kScanChunk < g_size ? kScanChunk : n;
        for (u64 i = 0; i < limit; i++) {
            for (u32 k = 0; k < 3; k++) {
                Pattern *p = &g_patterns[k];
                if (g_scan[i] != p->bytes[0] || i + p->length > n || p->count >= 8) continue;
                bool match = true;
                for (u32 j = 1; j < p->length; j++) {
                    if ((g_scan[i + j] & p->mask[j]) != p->bytes[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) p->hits[p->count++] = g_base + off + i;
            }
        }
    }
}

static bool object_array_valid(u64 gu) {
    u64 table = rd64(gu + 0x18);
    u32 chunks = rd32(gu + 0x14);
    u32 elements = rd32(gu + 0x20);
    return table != 0 && chunks > 0 && chunks <= 64 && elements > 1000 && elements < 5000000;
}

static bool load_object_array() {
    g_table = rd64(g_objects + 0x18);
    g_numChunks = rd32(g_objects + 0x14);
    g_numElements = rd32(g_objects + 0x20);
    return g_table != 0 && g_numChunks > 0 && g_numChunks <= 64 && g_numElements > 1000;
}

static bool is_process_event(const u8 *c) {
    static const u8 prefix[15] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC};
    for (u32 i = 0; i < 15; i++) {
        if (c[i] != prefix[i]) return false;
    }
    return c[19] == 0x48 && c[20] == 0x8D && c[21] == 0x6C && c[22] == 0x24;
}

static u64 first_object() {
    u64 chunk = rd64(g_table);
    for (u32 j = 1; j < 2000; j++) {
        u64 obj = rd64(chunk + (u64)j * kItemSize + 8);
        if (obj && rd64(obj) >= g_base && rd64(obj) < g_base + g_size) return obj;
    }
    return 0;
}

static void resolve_process_event() {
    u64 obj = first_object();
    if (!obj) return;
    u64 vtable = rd64(obj);
    u8 code[32];
    u32 order[81];
    u32 count = 0;
    order[count++] = kProcessEventSlot;
    for (u32 s = 40; s <= 120; s++) {
        if (s != kProcessEventSlot) order[count++] = s;
    }
    for (u32 k = 0; k < count; k++) {
        u64 fn = rd64(vtable + 8ull * order[k]);
        if (fn >= g_base && fn < g_base + g_size && rd(fn, code, 32) && is_process_event(code)) {
            g_processEvent = fn;
            g_processEventSlot = order[k];
            return;
        }
    }
}

static u32 name_index(const wc *name) {
    u32 fname[2] = {0, 0};
    ((void (*)(void *, const wc *, i32))g_nameCtor)(fname, name, 0);
    return fname[0];
}

static void scan_objects() {
    u64 found[4] = {0, 0, 0, 0};
    for (u32 t = 0; t < 5; t++) g_targets[t].count = 0;
    u64 lastClass = 0;
    u32 lastIndex = 0;
    for (u32 ci = 0; ci < g_numChunks; ci++) {
        u64 chunk = rd64(g_table + 8ull * ci);
        u32 left = g_numElements - ci * kChunkItems;
        u32 n = left < kChunkItems ? left : kChunkItems;
        if (!chunk || !rd(chunk, g_items, (u64)n * kItemSize)) continue;
        for (u32 j = 0; j < n; j++) {
            u64 obj = *(u64 *)(g_items + (u64)j * kItemSize + 8);
            if (!obj) continue;
            u64 cls = rd64(obj + 0x10);
            if (!cls) continue;
            if (cls != lastClass) {
                lastClass = cls;
                lastIndex = rd32(cls + 0x18);
            }
            if (lastIndex == g_functionClass) {
                u32 nameIdx = rd32(obj + 0x18);
                for (u32 f = 0; f < 4; f++) {
                    if (g_fns[f].nameIndex == 0 || g_fns[f].nameIndex != nameIdx || found[f]) continue;
                    u64 outer = rd64(obj + 0x20);
                    if (outer && rd32(outer + 0x18) == g_fns[f].ownerIndex) found[f] = obj;
                }
            }
            for (u32 t = 0; t < 5; t++) {
                Target *tg = &g_targets[t];
                if (tg->index == 0 || tg->index != lastIndex || tg->count >= 64) continue;
                if (rd32(obj + 8) & 0x10) continue;
                tg->object[tg->count] = obj;
                tg->outer[tg->count] = rd64(obj + 0x20);
                tg->count++;
            }
        }
    }
    for (u32 f = 0; f < 4; f++) g_fns[f].object = found[f];
}

static u64 owned_by(const Target *t, u64 owner) {
    for (u32 i = 0; i < t->count; i++) {
        if (t->outer[i] == owner) return t->object[i];
    }
    return 0;
}

static bool world_cached() {
    u64 player = g_shared->player;
    if (g_shared->stage != StageReady || !player) return false;
    u64 cls = rd64(player + 0x10);
    if (!cls || rd32(cls + 0x18) != g_targets[0].index) return false;
    for (u32 t = 1; t < 4; t++) {
        u64 obj = owned_by(&g_targets[t], player);
        if (!obj || rd64(obj + 0x20) != player || rd32(rd64(obj + 0x10) + 0x18) != g_targets[t].index) return false;
    }
    for (u32 f = 0; f < 4; f++) {
        u64 fn = g_fns[f].object;
        if (!fn || rd32(fn + 0x18) != g_fns[f].nameIndex) return false;
    }
    return true;
}

static u64 g_lastFns[4];
static u32 g_lastFail;
static u32 g_lastActive = 0xFFFFFFFF;
static u64 g_scanCount;

static void log_scan(u64 ms) {
    g_scanCount++;
    if (g_scanCount > 2) return;
    log_dec("object scan ms", ms);
}

static void report_state() {
    for (u32 f = 0; f < 4; f++) {
        if (g_fns[f].object == g_lastFns[f]) continue;
        g_lastFns[f] = g_fns[f].object;
        put("function ");
        log_hex(f == 0 ? "Try Auto Dodge" : f == 1 ? "Can Activate Ability On Frame" : f == 2 ? "Try Activate Stance Ability On Frame" : "GetNearestValidReceivedFrame", g_fns[f].object);
    }
    if (g_failCode != g_lastFail) {
        g_lastFail = g_failCode;
        log_hex("patch failure (patch<<8 | reason)", g_failCode);
    }
    if (g_shared->active != g_lastActive) {
        g_lastActive = g_shared->active;
        log_dec("active feature mask", g_lastActive);
    }
}

static void report_world() {
    u64 player = g_targets[0].count ? g_targets[0].object[0] : 0;
    u64 component = player ? owned_by(&g_targets[1], player) : 0;
    u64 deflect = player ? owned_by(&g_targets[2], player) : 0;
    u64 dodge = player ? owned_by(&g_targets[3], player) : 0;
    g_shared->player = player;
    g_driveComponent = component;
    g_drivePassive = deflect;
    g_driveDodge = dodge;
    g_drivePlayer = player;
    if (!player) {
        g_shared->stage = StageWorld;
    } else if (component && deflect && dodge) {
        g_shared->stage = StageReady;
    } else {
        g_shared->stage = StagePlayer;
    }
    if (player == g_lastPlayer) return;
    g_lastPlayer = player;
    log_text("--- world state changed");
    log_hex("player", player);
    log_hex("DeflectComponent", component);
    log_hex("BP_PlayerDeflectPassive", deflect);
    log_hex("BP_PerfectDodgePassive", dodge);
    log_dec("BP_Renewal_Hun_C instances", g_targets[0].count);
    if (g_targets[4].count) {
        u64 engine = g_targets[4].object[0];
        u64 vtable = rd64(engine);
        log_hex("GameEngine", engine);
        log_hex("GameEngine vtable", vtable);
        log_hex("GameEngine tick slot target rva", rd64(vtable + 8ull * kTickSlot) - g_base);
    }
}


static bool wr(u64 address, const void *data, u64 size) {
    u64 done = 0;
    return WriteProcessMemory(g_process, (void *)address, data, size, &done) && done == size;
}

static void patch_fail(u32 index, u32 code, u64 script) {
    Patch *p = &g_patches[index];
    p->dead = 1;
    p->script = script;
    g_failCode = ((index + 1) << 8) | code;
    g_shared->error = ErrPatch;
}

static void patch_apply(u32 index, u64 fn) {
    Patch *p = &g_patches[index];
    u64 script = rd64(fn + 0xB0);
    u32 num = rd32(fn + 0xB8);
    u8 bytes[80];
    static const u8 half[8] = {0, 0, 0, 0, 0, 0, 0xE0, 0x3F};
    static const u8 two[8] = {0, 0, 0, 0, 0, 0, 0, 0x40};
    if (!script || num != p->length) return patch_fail(index, 1, script);
    u32 span = p->end - p->start;
    if (!rd(script + p->start, bytes, span)) return patch_fail(index, 2, script);
    if (p->kind == PatchTag) {
        u8 next = 0;
        if (bytes[0] != 0x14 || bytes[1] != 0x00 || !rd(script + p->end, &next, 1) || next != p->next) return patch_fail(index, 3, script);
        memcpy(p->original, bytes, span);
        p->offset = script + p->start;
        bytes[10] = 0x27;
        for (u32 i = 11; i < span; i++) bytes[i] = 0x0B;
        if (!wr(p->offset, bytes, span)) return patch_fail(index, 5, script);
    } else {
        u32 hit = 0, count = 0;
        for (u32 i = 0; i + 8 <= span; i++) {
            u32 j = 0;
            while (j < 8 && bytes[i + j] == half[j]) j++;
            if (j == 8) {
                hit = i;
                count++;
            }
        }
        if (count != 1) return patch_fail(index, 4, script);
        memcpy(p->original, half, 8);
        p->offset = script + p->start + hit;
        if (!wr(p->offset, two, 8)) return patch_fail(index, 5, script);
    }
    p->script = script;
    p->applied = 1;
    p->dead = 0;
}

static void patch_restore(u32 index) {
    Patch *p = &g_patches[index];
    u32 size = p->kind == PatchTag ? p->end - p->start : 8;
    wr(p->offset, p->original, size);
    p->applied = 0;
}

static void patch_step(u32 index, bool want) {
    Patch *p = &g_patches[index];
    u64 fn = g_fns[p->fn].object;
    u64 script = fn ? rd64(fn + 0xB0) : 0;
    if (p->applied && script != p->script) p->applied = 0;
    if (p->dead && script != p->script) p->dead = 0;
    if (!fn || p->dead || (p->applied != 0) == want) return;
    if (want) {
        patch_apply(index, fn);
    } else {
        patch_restore(index);
    }
}

static void patch_tick() {
    u32 want = g_shared->toggles;
    g_frame++;
    bool recheck = (g_frame & 31) == 0;
    u32 active = 0;
    for (u32 i = 0; i < 5; i++) {
        Patch *p = &g_patches[i];
        bool on = (want & p->feature) != 0;
        if (recheck || (p->applied != 0) != on) patch_step(i, on);
    }
    for (u32 f = 1; f <= 2; f <<= 1) {
        bool all = true;
        bool any = false;
        for (u32 i = 0; i < 5; i++) {
            if (!(g_patches[i].feature & f)) continue;
            any = true;
            if (!g_patches[i].applied) all = false;
        }
        if (any && all) active |= f;
    }
    g_shared->active = active;
}

static u64 g_windowsOwner;

static void drive_windows(bool on) {
    u64 player = g_drivePlayer, passive = g_drivePassive;
    if (!player || !passive || rd64(passive + 0x20) != player) return;
    if (rd32(rd64(passive + 0x10) + 0x18) != g_targets[2].index) return;
    static const u8 unlimited[8] = {0x00, 0x00, 0x34, 0x26, 0xF5, 0x6B, 0x0C, 0x43};
    static const u8 zero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const u8 *value = on ? unlimited : zero;
    wr(passive + 0x5F0, value, 8);
    wr(passive + 0x628, value, 8);
    g_windowsOwner = on ? passive : 0;
}

static void drive_ordinary() {
    u64 player = g_drivePlayer, component = g_driveComponent, passive = g_driveDodge;
    u64 getFn = g_fns[3].object, tryFn = g_fns[0].object;
    if (!player || !component || !passive || !getFn || !tryFn || !g_processEvent) return;
    if (rd64(component + 0x20) != player || rd64(passive + 0x20) != player) return;
    if (rd32(rd64(component + 0x10) + 0x18) != g_targets[1].index) return;
    if (rd32(rd64(passive + 0x10) + 0x18) != g_targets[3].index) return;
    if (rd32(getFn + 0x18) != g_fns[3].nameIndex || rd32(tryFn + 0x18) != g_fns[0].nameIndex) return;
    memset(g_parmsA, 0, sizeof(g_parmsA));
    *(float *)(g_parmsA + 0x00) = 360.0f;
    *(float *)(g_parmsA + 0x04) = 999.0f;
    *(i32 *)(g_parmsA + 0x0C) = -1;
    ((void (*)(u64, u64, void *))g_processEvent)(component, getFn, g_parmsA);
    i32 handle = *(i32 *)(g_parmsA + 0x60);
    if (handle < 0) return;
    memset(g_parmsB, 0, sizeof(g_parmsB));
    *(i32 *)g_parmsB = handle;
    memcpy(g_parmsB + 8, g_parmsA + 0x10, 0x50);
    ((void (*)(u64, u64, void *))g_processEvent)(passive, tryFn, g_parmsB);
}

static void hook_tick(void *self, float delta, u8 idle) {
    g_origTick(self, delta, idle);
    patch_tick();
    if (g_shared->active & FeatureBlock) {
        drive_windows(true);
    } else if (g_windowsOwner) {
        drive_windows(false);
    }
    u32 want = g_shared->toggles;
    if ((want & FeatureOrdinary) && !(want & FeatureBlock) && (g_shared->active & FeatureDodge)) drive_ordinary();
}

static void install_hook() {
    if (g_hookInstalled || !g_targets[4].count) return;
    u64 vtable = rd64(g_targets[4].object[0]);
    u64 slot = vtable + 8ull * kTickSlot;
    u64 target = rd64(slot);
    if (target < g_base || target >= g_base + g_size) {
        g_shared->error = ErrHook;
        return;
    }
    u32 old = 0;
    if (!VirtualProtect((void *)slot, 8, 4, &old)) {
        g_shared->error = ErrHook;
        return;
    }
    g_origTick = (TickFn)target;
    *(volatile u64 *)slot = (u64)&hook_tick;
    VirtualProtect((void *)slot, 8, old, &old);
    g_slot = slot;
    g_hookInstalled = 1;
    log_hex("tick hook slot", slot);
    log_hex("original tick rva", target - g_base);
}

static void remove_hook() {
    g_shared->toggles = 0;
    for (u32 i = 0; i < 40 && g_shared->active; i++) Sleep(50);
    if (g_hookInstalled) {
        u32 old = 0;
        if (VirtualProtect((void *)g_slot, 8, 4, &old)) {
            *(volatile u64 *)g_slot = (u64)g_origTick;
            VirtualProtect((void *)g_slot, 8, old, &old);
        }
        Sleep(300);
    }
}

static void init_patches() {
    g_fns[0].name = L"Try Auto Dodge";
    g_fns[0].owner = L"BP_PerfectDodgePassive_C";
    g_fns[1].name = L"Can Activate Ability On Frame";
    g_fns[1].owner = L"BP_PlayerDeflectPassive_C";
    g_fns[2].name = L"Try Activate Stance Ability On Frame";
    g_fns[2].owner = L"BP_PlayerDeflectPassive_C";
    g_fns[3].name = L"GetNearestValidReceivedFrame";
    g_fns[3].owner = L"DeflectComponent";
    g_patches[0] = {0, FeatureAnyDodge, PatchTag, 282, 356, 0x07, 631, 0, 0, 0, 0, {}};
    g_patches[1] = {0, FeatureAnyDodge, PatchConst, 466, 504, 0, 631, 0, 0, 0, 0, {}};
    g_patches[2] = {1, FeatureBlock, PatchTag, 268, 342, 0x14, 410, 0, 0, 0, 0, {}};
    g_patches[3] = {2, FeatureBlock, PatchConst, 531, 569, 0, 1126, 0, 0, 0, 0, {}};
    g_patches[4] = {2, FeatureBlock, PatchTag, 355, 421, 0x07, 1126, 0, 0, 0, 0, {}};
}

static void open_shared() {
    static const wc name[] = L"Local\\CombatAssistantShared";
    while (!g_shared) {
        void *mapping = OpenFileMappingW(0xF001F, 0, name);
        if (mapping) g_shared = (Shared *)MapViewOfFile(mapping, 0xF001F, 0, 0, sizeof(Shared));
        if (!g_shared) Sleep(250);
    }
}

static void beat() {
    if (g_shared->magic != kMagic) {
        log_text("panel closed, unloading");
        remove_hook();
        FreeLibraryAndExitThread(g_module, 0);
    }
    g_shared->heartbeat = g_shared->heartbeat + 1;
}

static void init_patterns() {
    pattern_parse(&g_patterns[0], "48 89 5C 24 10 57 48 83 EC 30 48 8B F9 48 8B DA 8B 09 83 7F 04 00 75 1E E8 ?? ?? ?? ?? 48 8B D3 48 8B C8 E8 ?? ?? ?? ?? 48 8B C3 48 8B 5C 24 48 48 83 C4 30 5F C3");
    pattern_parse(&g_patterns[1], "48 89 5C 24 08 57 48 83 EC 30 48 8B D9 48 89 54 24 20 33 C9 41 8B F8 4C 8B D2 44 8B C9 48 85 D2 74 20 0F B7 02 66 85 C0 74 18 66 0F 1F 44 00 00 48 83 C2 02 0F B7 C0 44 0B C8 0F B7 02 66 85 C0");
    pattern_parse(&g_patterns[2], "8B 3D ?? ?? ?? ?? 48 63 F1 48 8D 0D ?? ?? ?? ?? 48 8B 04 F7 48 8B 58 08");
    g_targets[0].name = L"BP_Renewal_Hun_C";
    g_targets[1].name = L"DeflectComponent";
    g_targets[2].name = L"BP_PlayerDeflectPassive_C";
    g_targets[3].name = L"BP_PerfectDodgePassive_C";
    g_targets[4].name = L"GameEngine";
}

static bool resolve_signatures() {
    scan_image();
    log_dec("hits FName::ToString", g_patterns[0].count);
    log_dec("hits FName::FName", g_patterns[1].count);
    log_dec("hits GUObjectArray reference", g_patterns[2].count);
    if (!g_patterns[0].count) {
        g_shared->error = ErrToString;
        return false;
    }
    g_toString = g_patterns[0].hits[0];
    if (!g_patterns[1].count) {
        g_shared->error = ErrNameCtor;
        return false;
    }
    g_nameCtor = g_patterns[1].hits[0];
    for (u32 i = 0; i < g_patterns[2].count; i++) {
        u64 hit = g_patterns[2].hits[i];
        u64 target = hit + 16 + (u64)(i64)(i32)rd32(hit + 12);
        if (object_array_valid(target)) {
            g_objects = target;
            break;
        }
    }
    if (!g_objects || !load_object_array()) {
        g_shared->error = ErrObjectArray;
        return false;
    }
    log_hex("FName::ToString rva", g_toString - g_base);
    log_hex("FName::FName rva", g_nameCtor - g_base);
    log_hex("GUObjectArray rva", g_objects - g_base);
    log_dec("NumElements", g_numElements);
    log_dec("NumChunks", g_numChunks);
    resolve_process_event();
    if (g_processEvent) {
        log_hex("ProcessEvent rva", g_processEvent - g_base);
        log_dec("ProcessEvent vtable slot", g_processEventSlot);
    } else {
        g_shared->error = ErrProcessEvent;
        log_text("ProcessEvent not found");
    }
    return true;
}

static u32 worker(void *) {
    log_open();
    log_text("payload loaded");
    open_shared();
    g_shared->version = kVersion;
    g_shared->stage = StageLoaded;
    init_patterns();
    init_patches();
    g_base = (u64)GetModuleHandleW(0);
    g_size = rd32(g_base + rd32(g_base + 0x3C) + 0x50);
    log_hex("module base", g_base);
    log_hex("module size", g_size);
    Sleep(1000);
    if (!resolve_signatures()) {
        log_text("signatures failed");
        for (;;) {
            beat();
            Sleep(250);
        }
    }
    g_shared->stage = StageSignatures;
    for (u32 tick = 0;; tick++) {
        beat();
        report_state();
        if (tick % 12 == 0 && load_object_array()) {
            for (u32 t = 0; t < 5; t++) g_targets[t].index = name_index(g_targets[t].name);
            g_functionClass = name_index(L"Function");
            for (u32 f = 0; f < 4; f++) {
                g_fns[f].nameIndex = name_index(g_fns[f].name);
                g_fns[f].ownerIndex = name_index(g_fns[f].owner);
            }
            if (!world_cached()) {
                u64 began = GetTickCount64();
                scan_objects();
                log_scan(GetTickCount64() - began);
                install_hook();
                report_world();
            }
        }
        Sleep(250);
    }
}

extern "C" i32 DllMain(void *module, u32 reason, void *) {
    if (reason == 1) {
        g_module = module;
        g_process = GetCurrentProcess();
        DisableThreadLibraryCalls(module);
        CreateThread(0, 0, worker, 0, 0, 0);
    }
    return 1;
}
