#pragma once

#include <cstddef>
#include <cstdint>

// CrossInk: "Last read 28.10. 11:10" on the stats sleep screens. The local time
// of the latest page turn is kept in APP_STATE (saved with it before sleep).
namespace LastRead {

// Records the current local time; no-op without a clock.
void markNow();
// Formats the stored time with STR_LAST_READ_FORMAT; false when none is stored.
bool format(char* buf, size_t len);

uint32_t pack(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

}  // namespace LastRead
