#ifndef SPEC_CAPS_H
#define SPEC_CAPS_H

#include <cstdint>

// Capability register of the firmware (SPEC_GREG | SPEC_GREG_CAPS, firmware with the fixes of the device to host DMA
// path): the upper half is a magic number, the lower half are flags. Older firmware decodes only four address bits and
// answers 0xDEADBEEF (or something else, never the magic) at this address.
constexpr uint32_t kSpecCapsMagicMask = 0xFFFF0000;
constexpr uint32_t kSpecCapsMagic = 0xC0DE0000;
/// The device to host DMA keeps all data when the PCIe side back-pressures it, for descriptors of up to 32 KB. Older
/// firmware loses data with descriptors larger than a few KB and can crash the host.
constexpr uint32_t kSpecCapLongBurst = 0x1;

struct SpecCaps {
    bool valid = false;       ///< the firmware has the capability register
    bool long_burst = false;  ///< flag kSpecCapLongBurst
};

inline SpecCaps decodeSpecCaps(uint32_t reg) {
    SpecCaps caps;
    caps.valid = (reg & kSpecCapsMagicMask) == kSpecCapsMagic;
    caps.long_burst = caps.valid && (reg & kSpecCapLongBurst) != 0;
    return caps;
}

/// Merged descriptors for device to host DMA: Auto = only if the firmware says that it can take them
enum class MergeMode { Auto, On, Off };

inline bool mergedReadsEnabled(MergeMode mode, const SpecCaps &caps) {
    return mode == MergeMode::On || (mode == MergeMode::Auto && caps.long_burst);
}

#endif
