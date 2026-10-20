// S0 micro-benchmark: shared declarations for the generated candidate file and the harness.
#pragma once
#include "ppc_config.h"
#include "ppc_context.h"

#ifdef S0_COUNT
extern uint64_t s0_icount;
#define S0_I() (++s0_icount)
#else
#define S0_I() ((void)0)
#endif

struct S0Cand {
    uint32_t addr;
    PPCFunc* fn;
    uint64_t icount;     // guest instructions per call, measured by the PC counting build (0 in the sweep build)
    int vmx;             // body uses vector (simde) operations
};
struct S0Table {
    const char* tag;     // code variant: base (as recompiled), opt (XenonRecomp *_as_local options), optnv (opt + non-volatile memory access)
    const S0Cand* cands;
    int n;
};
// every variant compiled into the binary registers its table at start-up
#include <vector>
std::vector<const S0Table*>& s0_registry();
struct S0Register { explicit S0Register(const S0Table* t) { s0_registry().push_back(t); } };
