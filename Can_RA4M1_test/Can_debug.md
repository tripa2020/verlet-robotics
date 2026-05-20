# XIAO RA4M1 Arduino_CAN Library Debug Analysis

## Summary

The Arduino_CAN library for XIAO RA4M1 has **two critical bugs** preventing CAN communication:

1. `enableInternalLoopback()` uses the wrong mode constant (EXTERNAL instead of INTERNAL)
2. The RX callback comment indicates receive polling may not work as expected

---

## FSP Error Code Reference

| Code    | Constant                      | Meaning                          |
|---------|-------------------------------|----------------------------------|
| -60000  | FSP_ERR_CAN_DATA_UNAVAILABLE  | No data available in mailbox     |
| -60001  | FSP_ERR_CAN_MODE_SWITCH_FAILED| Mode transition failed           |
| -60002  | FSP_ERR_CAN_INIT_FAILED       | Hardware initialization failed   |
| -60003  | FSP_ERR_CAN_TRANSMIT_NOT_READY| TX mailbox busy or not ready     |
| -60004  | FSP_ERR_CAN_RECEIVE_MAILBOX   | Wrong mailbox type for operation |
| -60005  | FSP_ERR_CAN_TRANSMIT_MAILBOX  | Wrong mailbox type for operation |
| -60006  | FSP_ERR_CAN_MESSAGE_LOST      | RX message overwritten/overrun   |
| -60007  | FSP_ERR_CAN_TRANSMIT_FIFO_FULL| TX FIFO is full                  |

Source: `~/.arduino15/packages/Seeeduino/hardware/renesas_uno/1.2.0/variants/XIAORA4M1/includes/ra/fsp/inc/fsp_common_api.h:283-290`

---

## Bug #1: Loopback Mode Uses Wrong Constant

**File:** `~/.arduino15/packages/Seeeduino/hardware/renesas_uno/1.2.0/libraries/Arduino_CAN/src/R7FA4M1_CAN.cpp:226-232`

```cpp
int R7FA4M1_CAN::enableInternalLoopback()
{
  // BUG: Uses CAN_TEST_MODE_LOOPBACK_EXTERNAL (5) instead of CAN_TEST_MODE_LOOPBACK_INTERNAL (7)
  if(fsp_err_t const rc = R_CAN_ModeTransition(&_can_ctrl, CAN_OPERATION_MODE_NORMAL, CAN_TEST_MODE_LOOPBACK_EXTERNAL); rc != FSP_SUCCESS)
    return -rc;

  return 1;
}
```

**Available Test Modes** (from `r_can_api.h:99-106`):

| Mode                          | Value | Description                              |
|-------------------------------|-------|------------------------------------------|
| CAN_TEST_MODE_DISABLED        | 0     | Normal CAN operation                     |
| CAN_TEST_MODE_LISTEN          | 3     | Listen-only, no TX or ACK                |
| CAN_TEST_MODE_LOOPBACK_EXTERNAL| 5    | TX → external pins → RX (needs wiring)   |
| CAN_TEST_MODE_LOOPBACK_INTERNAL| 7    | TX → internal RX (no wiring needed)      |

**Impact:**
- External loopback requires physical TX-to-RX jumper wire
- Without the jumper, the CAN transceiver sees no ACK and fails
- The -60003 error likely results from the CAN controller waiting for ACK that never comes

**Fix:** Change line 228 to use `CAN_TEST_MODE_LOOPBACK_INTERNAL`

---

## Bug #2: RX Callback Not Implemented — FIXED

**File:** `R7FA4M1_CAN.cpp:279`

```cpp
case CAN_EVENT_RX_COMPLETE: // Currently driver don't support this. This is unreachable code for now.
```

**Impact:**
- The callback handler has code for RX_COMPLETE but comments it as "unreachable"
- The `available()` method relies on `_can_rx_buf.available()` which gets populated by this callback
- If RX callbacks never fire, `available()` always returns 0

**Root Cause:**
The FSP r_can driver for RA4M1 never fires RX_COMPLETE callbacks. The R7FA6M5 (CANFD) variant works because it manually polls hardware via `R_CANFD_InfoGet()` + `R_CANFD_Read()` inside `available()`. The R7FA4M1 variant lacks this polling.

### Fix Applied (2026-05-01)

**Patched file:** `~/.arduino15/packages/Seeeduino/hardware/renesas_uno/1.2.0/libraries/Arduino_CAN/src/R7FA4M1_CAN.cpp`

**Location:** `available()` method (around line 264)

**Patch:** Added manual mailbox polling to check hardware RX mailboxes (indices 16-31) before returning buffer count.

```cpp
size_t R7FA4M1_CAN::available()
{
  /* PATCH: Manual polling for RX mailboxes.
   * The FSP CAN driver doesn't fire RX_COMPLETE callbacks, so we must poll.
   * RX mailboxes are indices 16-31 (groups 4-7).
   */
  can_info_t can_info;
  if (fsp_err_t const rc = R_CAN_InfoGet(&_can_ctrl, &can_info); rc == FSP_SUCCESS)
  {
    for (uint32_t mb = 16; mb < 32; mb++)
    {
      if (can_info.rx_mb_status & (1UL << mb))
      {
        can_frame_t frame;
        if (fsp_err_t const rc2 = R_CAN_Read(&_can_ctrl, mb, &frame); rc2 == FSP_SUCCESS)
        {
          uint8_t const data_len = (frame.data_length_code <= CAN_DATA_BUFFER_LENGTH)
                                     ? frame.data_length_code : CAN_DATA_BUFFER_LENGTH;
          CanMsg const msg(
            (frame.id_mode == CAN_ID_MODE_STANDARD) ? CanStandardId(frame.id) : CanExtendedId(frame.id),
            data_len, frame.data);
          _can_rx_buf.enqueue(msg);
        }
      }
    }
  }
  return _can_rx_buf.available();
}
```

**Warning:** This patch will be overwritten if the Seeeduino board package updates. Re-apply after any board package update.

---

## Pin Configuration

**Defined in:** `pins_arduino.h:169-173`

```cpp
#define CAN_HOWMANY       1
#define PIN_CAN0_TX       (10)  // D10
#define PIN_CAN0_RX       (9)   // D9
#define PIN_CAN0_STBY    (-1)   // No standby pin defined
```

**Physical Port Mapping** (from `variant.cpp` and `pinmux.inc`):

| Arduino Pin | BSP Port   | CAN Function |
|-------------|------------|--------------|
| D9          | P110       | CAN_RX       |
| D10         | P109       | CAN_TX       |

**Alternative CAN-capable pins** (from `pinmux.inc`):

| Arduino Pin | BSP Port   | CAN Function |
|-------------|------------|--------------|
| D17         | P102       | CAN_RX       |
| D18         | P103       | CAN_TX       |

**Note:** The library only supports D9/D10. Using D17/D18 would require modifying the library to pass different pin arguments to the CAN constructor.

---

## Mailbox Configuration

The library configures 32 mailboxes (from `R7FA4M1_CAN.cpp:64-107`):

| Group | Mailboxes | ID Mode  | Type     |
|-------|-----------|----------|----------|
| 0-1   | 0-7       | Extended | Transmit |
| 2-3   | 8-15      | Standard | Transmit |
| 4-5   | 16-23     | Extended | Receive  |
| 6-7   | 24-31     | Standard | Receive  |

**Write routing** (line 256-258):
- Standard ID → Mailbox 0
- Extended ID → Mailbox 16

---

## Clock Configuration

```cpp
static uint32_t const F_CAN_CLK_Hz = 24*1000*1000UL;  // 24 MHz
```

The RA4M1 CAN peripheral uses PCLKB (peripheral clock B) at 24 MHz.

**Bit timing calculation** supports:
- Time quanta: 8-25 TQ per bit
- TSEG1: 4-16 TQ
- TSEG2: 2-8 TQ

---

## Test Plan

### Test 1: External Loopback with Jumper Wire

Since the library uses `CAN_TEST_MODE_LOOPBACK_EXTERNAL`, add a physical jumper:
- Connect D9 (RX) to D10 (TX) externally
- This bypasses the transceiver

### Test 2: Patch the Library

Create a local copy of `R7FA4M1_CAN.cpp` and change line 228:
```cpp
// Change from:
CAN_TEST_MODE_LOOPBACK_EXTERNAL
// To:
CAN_TEST_MODE_LOOPBACK_INTERNAL
```

### Test 3: Skip Loopback, Test External Bus

Don't use loopback mode at all:
- Connect XIAO to CAN bus with Teensy
- Both need proper termination (120Ω)
- Both need TJA1051 transceivers

### Test 4: Debug CAN Peripheral State

Add diagnostic code to print CAN status:
```cpp
can_info_t info;
R_CAN_InfoGet(&_can_ctrl, &info);
Serial.printf("Status: 0x%08X, RXMB: 0x%08X, Err: 0x%08X\n",
              info.status, info.rx_mb_status, info.error_code);
```

---

## Hardware Checklist

- [x] CAN Pal (TJA1051) connected to D17 (RX) and D18 (TX) on XIAO
- [x] CAN_H and CAN_L wires connected between Teensy and XIAO
- [x] 120Ω termination resistors at both ends of bus
- [x] Common ground between all devices
- [x] 3.3V power to TJA1051 from XIAO 3V3 pin
- [x] Baud rate matches: 1 Mbps on both sides

---

## Physical Layer Fix (2026-05-01)

**Problem:** CAN TX from RA4M1 failed with -60003 (no ACK received). Teensy received no frames.

**Root Cause:** The CAN Pal PCB screw terminals had poor/no contact.

**Fix:** Bypassed the CAN Pal screw terminals — wired CAN_H/CAN_L directly between the two CAN Pal boards (solder or jumper wire to CAN Pal pads, not through the terminal block).

**Result:** Bidirectional CAN communication working at 100 Hz with 0 missed frames.

---

## Relevant Source Files

| File | Location | Purpose |
|------|----------|---------|
| R7FA4M1_CAN.cpp | `libraries/Arduino_CAN/src/` | CAN driver implementation |
| R7FA4M1_CAN.h | `libraries/Arduino_CAN/src/` | CAN class declaration |
| pins_arduino.h | `variants/XIAORA4M1/` | Pin definitions |
| variant.cpp | `variants/XIAORA4M1/` | Pin mux configuration |
| pinmux.inc | `variants/XIAORA4M1/` | Port function tables |
| r_can_api.h | `includes/ra/fsp/inc/api/` | FSP CAN API definitions |
| fsp_common_api.h | `includes/ra/fsp/inc/` | FSP error codes |

All paths relative to: `~/.arduino15/packages/Seeeduino/hardware/renesas_uno/1.2.0/`

---

## Recommended Next Steps

1. **Quick test:** Wire D9 to D10 jumper and re-run loopback test
2. **If #1 fails:** Patch library to use `CAN_TEST_MODE_LOOPBACK_INTERNAL`
3. **If #2 fails:** Debug FSP CAN peripheral initialization order
4. **Alternative:** Skip XIAO loopback testing, test directly on bus with Teensy