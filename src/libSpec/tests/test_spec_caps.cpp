// Which firmware may get merged device-to-host DMA descriptors: only the one that has the capability register
// with the long-burst flag (see SpecCaps.h). Anything else, also what firmware without the register answers and what a
// dead card answers, must not enable them automatically.
#include "SpecCaps.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
int main() {
    // new firmware
    SpecCaps n = decodeSpecCaps(0xC0DE0001);
    CHECK(n.valid && n.long_burst);
    // magic but no flag (a future firmware without the flag)
    SpecCaps m = decodeSpecCaps(0xC0DE0000);
    CHECK(m.valid && !m.long_burst);
    // flag bit set without the magic must not count
    CHECK(!decodeSpecCaps(0x00000001).valid && !decodeSpecCaps(0x00000001).long_burst);
    // what old firmware answers, and a dead card
    CHECK(!decodeSpecCaps(0xDEADBEEF).valid && !decodeSpecCaps(0xDEADBEEF).long_burst);
    CHECK(!decodeSpecCaps(0xFFFFFFFF).valid && !decodeSpecCaps(0xFFFFFFFF).long_burst);
    CHECK(!decodeSpecCaps(0x00000000).valid);
    // decision table
    for (int fw = 0; fw < 2; fw++) {
        SpecCaps c = fw ? n : decodeSpecCaps(0xDEADBEEF);
        CHECK(mergedReadsEnabled(MergeMode::Auto, c) == (fw == 1));
        CHECK(mergedReadsEnabled(MergeMode::On, c) == true);
        CHECK(mergedReadsEnabled(MergeMode::Off, c) == false);
    }
    printf("PASS SpecCaps\n");
}
