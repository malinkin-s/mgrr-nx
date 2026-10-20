// S0 micro-benchmark harness (Linux and Switch).
//
// Guest memory: only the regions the benchmark touches are backed --
//   [0x00000000, 0x04000000)  low 64 MB: scratch data at 0x01000000 (4 MB), stack below 0x03F00000
//   [0x82000000, 0x83A40000)  the game image (mgr_image.bin, read from disk; needed for constant tables)
// inside a 4 GB guest window, exactly as the recompiled code addresses it (base + guest address).
//
// Linux: s0 sweep           (counting build) run every candidate in a forked child, print "ADDR ok ICOUNT" for deterministic ones
//        s0 bench           (timing build)   time the selected candidates
// Switch: the .nro always runs "bench", probes the memory APIs and writes sdmc:/switch/s0bench/result_<cpu MHz>mhz.txt
#include "s0_common.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <sys/stat.h>
#include <cstdio>
#include <string>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#include <malloc.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef S0_COUNT
uint64_t s0_icount;
#endif

static const uint64_t LOW_SIZE = 0x04000000, IMG_BASE = 0x82000000, IMG_SIZE = 0x1A40000;
static const uint32_t SCRATCH = 0x01000000, SCRATCH_SIZE = 0x400000, STACK_TOP = 0x03F00000, STACK_SIZE = 0x40000;
static uint8_t* g_base;
static std::vector<uint8_t> g_scratch_tpl;
static std::string g_log;
static FILE* g_progress;      // Switch: written and flushed line by line, so a crash still leaves a trace
static unsigned g_cpu_mhz;

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    fputs(buf, stdout); g_log += buf;
    if (g_progress) { fputs(buf, g_progress); fflush(g_progress); }
#ifdef __SWITCH__
    consoleUpdate(NULL);
#endif
}

static void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void make_template() {
    g_scratch_tpl.resize(SCRATCH_SIZE);
    for (uint32_t i = 0; i < SCRATCH_SIZE / 4; i++) {
        uint32_t v;
        switch (i & 3) {
        case 0: v = SCRATCH + ((i * 0x40u) % (SCRATCH_SIZE - 0x10000)); break;      // pointers into the scratch area
        case 1: { float f = 1.0f + (i % 97) * 0.01f; memcpy(&v, &f, 4); } break;    // ordinary floats
        case 2: v = i % 16; break;                                                   // small counts / indices
        default: v = 0x3F000000; break;                                              // 0.5f
        }
        be32(&g_scratch_tpl[i * 4], v);
    }
}

static void reset(PPCContext& ctx) {
    memcpy(g_base + SCRATCH, g_scratch_tpl.data(), SCRATCH_SIZE);
    memset(g_base + STACK_TOP - STACK_SIZE, 0, STACK_SIZE + 0x1000);
    memset(&ctx, 0, sizeof ctx);
    ctx.r1.u64 = STACK_TOP - 0x100;
    ctx.r13.u64 = 0x03000000;
    PPCRegister* args[] = {&ctx.r3, &ctx.r4, &ctx.r5, &ctx.r8, &ctx.r9, &ctx.r10};
    for (int k = 0; k < 6; k++) args[k]->u64 = SCRATCH + 0x2000u * (k + 1);
    ctx.r6.u64 = 8; ctx.r7.u64 = 4;                                                   // a few plausible small sizes
    ctx.f1.f64 = 1.0; ctx.f2.f64 = 0.5; ctx.f3.f64 = 2.0;
    ctx.fpscr.loadFromHost();
}

static bool load_image(const char* path) {
    memset(g_base, 0, LOW_SIZE);                 // the Switch heap is not zeroed; the PC sweep saw zeroed memory here
    FILE* f = fopen(path, "rb");
    if (!f) { say("cannot open %s\n", path); return false; }
    size_t n = fread(g_base + IMG_BASE, 1, IMG_SIZE, f); fclose(f);
    if (n != IMG_SIZE) { say("short image read: %zu of %llu\n", n, (unsigned long long)IMG_SIZE); return false; }
    return true;
}

// ---------------------------------------------------------------- guest memory
#ifdef __SWITCH__
extern "C" {
extern char* fake_heap_start;
extern char* fake_heap_end;
// Same as libnx's default (heap from the loader's override, or all free memory), but remembers the numbers for the probe.
static u64 g_heap_base, g_heap_total, g_window;
// The guest window lives inside the heap: guest [0, 64 MB) and the image are guest memory, the gap between them
// ([64 MB, 0x82000000), ~1.9 GB) is this program's own malloc arena. No special SVCs needed (method F).
void __libnx_initheap(void) {
    void* addr = nullptr; u64 size = 0;
    if (envHasHeapOverride()) { addr = envGetHeapOverrideAddr(); size = envGetHeapOverrideSize(); }
    else {
        u64 total = 0, used = 0;
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        size = total > used + 0x200000 ? (total - used - 0x200000) & ~0x1FFFFFull : 0x200000;
        if (R_FAILED(svcSetHeapSize(&addr, size))) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
    }
    svcGetInfo(&g_heap_base, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    g_heap_total = (u64)addr + size - g_heap_base;
    u64 win = ((u64)addr + 0xFFFF) & ~0xFFFFull;
    if (win + IMG_BASE + IMG_SIZE <= (u64)addr + size) {
        g_window = win;
        fake_heap_start = (char*)(win + LOW_SIZE); fake_heap_end = (char*)(win + IMG_BASE);
    } else {
        fake_heap_start = (char*)addr; fake_heap_end = (char*)addr + size;      // too little memory (applet mode)
    }
}
}

static void info(const char* name, u32 id) {
    u64 v = 0; Result rc = svcGetInfo(&v, id, CUR_PROCESS_HANDLE, 0);
    say("  %-26s %#14llx  (rc %#x)\n", name, (unsigned long long)v, rc);
}

static bool region_free(u64 a, u64 size) {
    for (u64 p = a; p < a + size;) {
        MemoryInfo mi; u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, p)) || mi.type != MemType_Unmapped) return false;
        p = mi.addr + mi.size;
    }
    return true;
}

// first 4 GB-aligned fully unmapped 4 GB window inside [start, start+size); with need < 4 GB only [win, win+need) has to fit
static u64 find_window(u64 start, u64 size, u64 need) {
    for (u64 a = (start + 0xFFFFFFFFull) & ~0xFFFFFFFFull; a + need <= start + size; a += 0x100000000ull)
        if (region_free(a, need)) return a;
    return 0;
}

// give memory back to the system: shrink the process heap to (current break + keep)
static bool try_physical(u64 win, const char* tag) {
    Result r1 = svcMapPhysicalMemory((void*)win, LOW_SIZE);
    Result r2 = R_SUCCEEDED(r1) ? svcMapPhysicalMemory((void*)(win + IMG_BASE), IMG_SIZE) : r1;
    say("  [%s] MapPhysicalMemory @%#llx: low rc=%#x image rc=%#x\n", tag, (unsigned long long)win, r1, r2);
    if (R_SUCCEEDED(r1) && R_SUCCEEDED(r2)) return true;
    if (R_SUCCEEDED(r1)) svcUnmapPhysicalMemory((void*)win, LOW_SIZE);
    return false;
}

// Method G (UnleashedRecomp-NX, rexglue-sdk-nx): heap pages -> svcMapProcessCodeMemory into the code region ->
// RW -> svcMapProcessMemory into a 4 GB window from the ASLR region. Needs the process-memory syscalls hinted by hbl.
static bool g_map(uint8_t* win, u64 off, u64 size) {
    Handle h = envGetOwnProcessHandle();
    void* backing = memalign(0x1000, size);
    if (!backing) { say("  [G] memalign %#llx failed\n", (unsigned long long)size); return false; }
    memset(backing, 0, size);
    virtmemLock();
    void* shadow = virtmemFindCodeMemory(size, 0x1000);
    Result r1 = shadow ? svcMapProcessCodeMemory(h, (u64)shadow, (u64)backing, size) : (Result)-1;
    virtmemUnlock();
    Result r2 = R_SUCCEEDED(r1) ? svcSetProcessMemoryPermission(h, (u64)shadow, size, Perm_Rw) : r1;
    Result r3 = R_SUCCEEDED(r2) ? svcMapProcessMemory(win + off, h, (u64)shadow, size) : r2;
    say("  [G] guest %#llx+%#llx: MapProcessCodeMemory rc=%#x, SetProcessMemoryPermission rc=%#x, MapProcessMemory rc=%#x\n",
        (unsigned long long)off, (unsigned long long)size, r1, r2, r3);
    return R_SUCCEEDED(r3);
}

static bool try_g() {
    bool hinted = envIsSyscallHinted(0x74) && envIsSyscallHinted(0x75) && envIsSyscallHinted(0x77) && envIsSyscallHinted(0x78);
    say("  [G] process-memory syscalls hinted: %d\n", (int)hinted);
    if (!hinted) return false;
    virtmemLock();
    uint8_t* win = (uint8_t*)virtmemFindAslr(0x100000000ull, 0x1000);
    if (win) virtmemAddReservation(win, 0x100000000ull);
    virtmemUnlock();
    if (!win) { say("  [G] no 4 GB ASLR window\n"); return false; }
    if (!g_map(win, 0, LOW_SIZE) || !g_map(win, IMG_BASE, IMG_SIZE)) return false;
    // the rest of the window stays unmapped: a stray guest access faults instead of corrupting host memory
    g_base = win;
    say("  guest memory: G (sparse 4 GB window @%p, process memory mapping)\n", (void*)win);
    return true;
}

static bool guest_alloc() {
    say("memory probe:\n");
    info("AliasRegionAddress", InfoType_AliasRegionAddress); info("AliasRegionSize", InfoType_AliasRegionSize);
    info("HeapRegionAddress", InfoType_HeapRegionAddress); info("HeapRegionSize", InfoType_HeapRegionSize);
    info("StackRegionAddress", InfoType_StackRegionAddress); info("StackRegionSize", InfoType_StackRegionSize);
    info("TotalMemorySize", InfoType_TotalMemorySize); info("UsedMemorySize", InfoType_UsedMemorySize);
    info("SystemResourceSizeTotal", InfoType_SystemResourceSizeTotal);
    say("  applet type %d, heap override %d, heap total %#llx\n", (int)appletGetAppletType(), (int)envHasHeapOverride(), (unsigned long long)g_heap_total);
    u64 alias = 0, alias_size = 0;
    svcGetInfo(&alias, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&alias_size, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
    virtmemLock();
    u64 win = find_window(alias, alias_size, 0x100000000ull);
    if (win) {                                    // informational: sparse methods (expected to fail under hbl)
        if (try_physical(win, "A")) { svcUnmapPhysicalMemory((void*)win, LOW_SIZE); svcUnmapPhysicalMemory((void*)(win + IMG_BASE), IMG_SIZE); }
        void* lo = aligned_alloc(0x1000, 0x10000);
        if (lo) { Result r = svcMapMemory((void*)win, lo, 0x10000); say("  [B] MapMemory into alias: rc=%#x\n", r); if (R_SUCCEEDED(r)) svcUnmapMemory((void*)win, lo, 0x10000); free(lo); }
    }
    virtmemUnlock();
    if (try_g()) return true;
    if (!g_window) { say("  [F] heap too small for the guest window (%#llx needed) -- start in title takeover mode (hold R)\n", (unsigned long long)(IMG_BASE + IMG_SIZE)); return false; }
    g_base = (uint8_t*)g_window;
    say("  [F] guest window inside the heap @%#llx, host malloc arena = guest [%#llx, %#llx)\n", (unsigned long long)g_window,
        (unsigned long long)LOW_SIZE, (unsigned long long)IMG_BASE);
    say("  guest memory: F (heap window)\n");
    return true;
}

// Effective CPU frequency from a chain of dependent ADDs (1 cycle latency each on Cortex-A57), independent of what clkrst reports.
static double measured_mhz() {
    const uint64_t iters = 2000000;                 // x 64 adds
    double best = 1e30;
    for (int k = 0; k < 5; k++) {
        uint64_t x = 0, n = iters;
        uint64_t t0 = armGetSystemTick();
        __asm__ volatile(
            "1:\n"
            ".rept 64\n add %0, %0, #1\n .endr\n"
            "subs %1, %1, #1\n b.ne 1b\n" : "+r"(x), "+r"(n) :: "cc");
        double ns = armTicksToNs(armGetSystemTick() - t0);
        if (ns < best) best = ns;
    }
    return iters * 64.0 / best * 1000.0;
}

static void clocks() {
    say("measured CPU frequency: %.0f MHz\n", measured_mhz());
    if (R_FAILED(clkrstInitialize())) { say("clkrst unavailable\n"); return; }
    ClkrstSession s; u32 hz = 0;
    if (R_SUCCEEDED(clkrstOpenSession(&s, PcvModuleId_CpuBus, 3))) { clkrstGetClockRate(&s, &hz); clkrstCloseSession(&s); }
    say("CPU clock (clkrst): %u MHz\n", hz / 1000000);
    if (R_SUCCEEDED(clkrstOpenSession(&s, PcvModuleId_EMC, 3))) { clkrstGetClockRate(&s, &hz); clkrstCloseSession(&s); say("EMC clock: %u MHz\n", hz / 1000000); }
    clkrstExit();
}
#else
static bool guest_alloc() {
    void* p = mmap(nullptr, 0x100000000ull, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return false;
    g_base = (uint8_t*)p;
    return mprotect(g_base, LOW_SIZE, PROT_READ | PROT_WRITE) == 0 && mprotect(g_base + IMG_BASE, IMG_SIZE, PROT_READ | PROT_WRITE) == 0;
}
static void clocks() {}
#endif

// ---------------------------------------------------------------- modes
static double now_ns() { return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::vector<const S0Table*>& s0_registry() { static std::vector<const S0Table*> r; return r; }

// variants in a fixed order (registration order depends on the link order)
static std::vector<const S0Table*> tables() {
    static const char* order[] = {"base", "opt", "optnv", "g2", "g3", "g2nf", "c2", "c3"};
    std::vector<const S0Table*> t = s0_registry();
    auto rank = [](const S0Table* x) { for (int i = 0; i < 8; i++) if (!strcmp(order[i], x->tag)) return i; return 99; };
    std::sort(t.begin(), t.end(), [&](const S0Table* a, const S0Table* b) { return rank(a) < rank(b); });
    return t;
}

#if defined(S0_COUNT) && !defined(__SWITCH__)
static int sweep(const S0Table& T) {
    for (int i = 0; i < T.n; i++) {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            alarm(2);
            uint64_t c[3]; PPCContext ctx;
            for (int r = 0; r < 3; r++) { reset(ctx); s0_icount = 0; T.cands[i].fn(ctx, g_base); c[r] = s0_icount; }
            printf("%08X %s %llu %d\n", T.cands[i].addr, (c[0] == c[1] && c[1] == c[2]) ? "ok" : "nondet", (unsigned long long)c[0], T.cands[i].vmx);
            fflush(stdout); _exit(0);
        }
        int st = 0; waitpid(pid, &st, 0);
        if (!WIFEXITED(st)) printf("%08X crash %d 0\n", T.cands[i].addr, WIFSIGNALED(st) ? WTERMSIG(st) : -1);
    }
    return 0;
}

// same order and state carry-over as bench(), but prints the instruction count of every run (to compare builds/architectures)
static int seq(const S0Table& T) {
    PPCContext ctx;
    for (int i = 0; i < T.n; i++)
        for (int r = 0; r < 3; r++) {
            reset(ctx); s0_icount = 0; T.cands[i].fn(ctx, g_base);
            printf("%s %08X %d %llu r3=%08X\n", T.tag, T.cands[i].addr, r, (unsigned long long)s0_icount, ctx.r3.u32); fflush(stdout);
        }
    return 0;
}
#endif

static int bench(const S0Table& T) {
    double tot_ns = 0, tot_ins = 0, logsum = 0, logsum_v = 0, tot_ns_v = 0, tot_ins_v = 0; int n = 0, nv = 0, nfold = 0;
    PPCContext ctx; std::vector<double> all;
    say("\n=== variant %s\n%-10s %12s %12s %10s %s\n", T.tag, "func", "guest ins", "ns/call", "MIPS", "vmx");
    for (int i = 0; i < T.n; i++) {
        const S0Cand& c = T.cands[i];
#ifdef __SWITCH__
        if (g_progress) { fprintf(g_progress, "run %s %08X\n", T.tag, c.addr); fflush(g_progress); }
#endif
        reset(ctx); c.fn(ctx, g_base);                                   // warm-up
        double t = 0; int reps = 0;
        while ((t < 20e6 && reps < 5000) || reps < 3) {
            reset(ctx);
            double t0 = now_ns(); c.fn(ctx, g_base); t += now_ns() - t0; reps++;
        }
        double ns = t / reps, mips = c.icount / ns * 1000.0;
        // more than 50 guest instructions per host ns: the compiler folded the loop away -- not a measurement of anything
        bool folded = mips > 50000.0;
        say("%08X %12llu %12.0f %10.1f %d%s\n", c.addr, (unsigned long long)c.icount, ns, mips, c.vmx, folded ? " folded" : "");
        if (folded) { nfold++; continue; }
        all.push_back(mips); tot_ns += ns; tot_ins += c.icount; logsum += std::log(mips); n++;
        if (c.vmx) { tot_ns_v += ns; tot_ins_v += c.icount; logsum_v += std::log(mips); nv++; }
    }
    std::sort(all.begin(), all.end());
    say("\n[%s] functions %d (vector %d), excluded as folded %d\n", T.tag, n, nv, nfold);
    say("[%s] median guest MIPS: %.1f\n", T.tag, all[all.size() / 2]);
    say("[%s] aggregate guest MIPS: %.1f   geomean: %.1f\n", T.tag, tot_ins / tot_ns * 1000.0, std::exp(logsum / n));
    if (nv) say("[%s] vector-only aggregate: %.1f   geomean: %.1f\n", T.tag, tot_ins_v / tot_ns_v * 1000.0, std::exp(logsum_v / nv));
    return 0;
}

int main(int argc, char** argv) {
#ifdef __SWITCH__
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    fsdevMountSdmc();
    mkdir("sdmc:/switch/s0bench", 0777);
    g_progress = fopen("sdmc:/switch/s0bench/progress.txt", "w");
    say("MGR:R S0 micro-benchmark\n");
    clocks();
    const char* image = "sdmc:/switch/s0bench/mgr_image.bin";
#else
    const char* image = argc > 2 ? argv[2] : "x360_work/mgrr_image.bin";
#endif
    say("step: template\n");
    make_template();
    say("step: guest memory\n");
    int rc = 1;
    if (!guest_alloc()) say("guest memory allocation failed\n");
    else {
        say("step: run\n");
        std::string mode = argc > 1 ? argv[1] : "bench";
        rc = 0;
        // every variant starts from the same guest state: zeroed low memory and a freshly loaded image
        for (const S0Table* T : tables()) {
            if (!load_image(image)) { rc = 1; break; }
#if defined(S0_COUNT) && !defined(__SWITCH__)
            if (mode == "sweep") { sweep(*T); break; }
            if (mode == "seq") seq(*T);
#else
            bench(*T);
#endif
        }
    }
#ifdef __SWITCH__
    { double m = measured_mhz(); say("measured CPU frequency at the end: %.0f MHz\n", m); g_cpu_mhz = (unsigned)(m / 10 + 0.5) * 10; }
    char path[96]; snprintf(path, sizeof path, "sdmc:/switch/s0bench/result_%umhz.txt", g_cpu_mhz);
    if (FILE* f = fopen(path, "w")) { fputs(g_log.c_str(), f); fclose(f); say("\nresult: %s\n", path); }
    else say("\ncannot write %s\n", path);
    say("press + to exit\n");
    while (appletMainLoop()) { padUpdate(&pad); if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break; consoleUpdate(NULL); }
    consoleExit(NULL);
#endif
    return rc;
}
