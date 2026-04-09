# Debug Fixes Log

## 2026-04-09: MT6701 SPI Read Failures on seed_node

**Symptom:** Node 3 showing `SENS STALE CRC` errors on master dashboard. UART communication working (node responds), but sensor data invalid.

**Test:** `mt6701_spi_read.ino` test sketch worked perfectly on same hardware.

### Fix: SPI Mode Hardcoded (Ignored Config)

**File:** `seed_node/mt6701.cpp`

**Problem:** SPI transaction hardcoded MODE0, ignoring `SPI_MODE` from config.h.
```cpp
// WRONG - hardcoded MODE0
SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

// CORRECT - use config values
SPI.beginTransaction(SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE));
```

**Root cause:** MT6701 requires CPHA=1 (data valid on falling edge). MODE0 uses CPHA=0, causing bit-shifted reads and CRC failures.

| Mode | CPOL | CPHA | MT6701 Compatible |
|------|------|------|-------------------|
| MODE0 | 0 | 0 | No |
| MODE1 | 0 | 1 | Yes |
| MODE2 | 1 | 0 | No |
| MODE3 | 1 | 1 | Yes |

**Why test sketch worked:** It tried multiple SPI modes and displayed results for each. MODE1 readings were correct.

---

## Lessons Learned

1. Use config constants in implementation, don't hardcode values
2. When test code works but production code fails, diff the SPI/I2C settings
