/*
 * MT6701 CRC6 Debug Test
 *
 * Diagnose why CRC validation fails despite correct angle readings.
 * Outputs raw frame, extracted fields, and CRC comparison.
 *
 * Wiring (Teensy 4.1 SPI1):
 *   MT6701 B (CLK) -> Pin 27 (SCK1)
 *   MT6701 A (DO)  -> Pin 1  (MISO1)
 *   MT6701 Z (CSN) -> Pin 0
 */

#include <SPI.h>

constexpr uint8_t CS_PIN = 0;

// Try different SPI settings
uint32_t spi_clock = 1000000;  // Start slower: 1 MHz
uint8_t spi_mode = SPI_MODE1;  // MT6701 spec says mode 1

// CRC6 lookup table - polynomial x^6 + x + 1 (0x43)
const uint8_t crc6_table[64] = {
    0x00, 0x03, 0x06, 0x05, 0x0C, 0x0F, 0x0A, 0x09,
    0x18, 0x1B, 0x1E, 0x1D, 0x14, 0x17, 0x12, 0x11,
    0x30, 0x33, 0x36, 0x35, 0x3C, 0x3F, 0x3A, 0x39,
    0x28, 0x2B, 0x2E, 0x2D, 0x24, 0x27, 0x22, 0x21,
    0x23, 0x20, 0x25, 0x26, 0x2F, 0x2C, 0x29, 0x2A,
    0x3B, 0x38, 0x3D, 0x3E, 0x37, 0x34, 0x31, 0x32,
    0x13, 0x10, 0x15, 0x16, 0x1F, 0x1C, 0x19, 0x1A,
    0x0B, 0x08, 0x0D, 0x0E, 0x07, 0x04, 0x01, 0x02
};

// Method 1: Lookup table (current implementation)
uint8_t crc6_lookup(uint32_t data18) {
    uint8_t crc = 0;
    crc = crc6_table[crc ^ ((data18 >> 12) & 0x3F)];
    crc = crc6_table[crc ^ ((data18 >> 6) & 0x3F)];
    crc = crc6_table[crc ^ (data18 & 0x3F)];
    return crc;
}

// Method 2: Bit-by-bit (reference implementation)
uint8_t crc6_bitwise(uint32_t data18) {
    uint8_t crc = 0;
    for (int i = 17; i >= 0; i--) {
        uint8_t bit = (data18 >> i) & 1;
        uint8_t xor_bit = (crc >> 5) ^ bit;
        crc = ((crc << 1) & 0x3F);
        if (xor_bit) {
            crc ^= 0x03;  // x^6 + x + 1 -> feedback taps at positions 0 and 1
        }
    }
    return crc;
}

// Method 3: SmartKnob style (from their implementation)
uint8_t crc6_smartknob(uint32_t data18) {
    uint8_t crc = 0;
    for (int i = 17; i >= 0; i--) {
        uint8_t input_bit = (data18 >> i) & 1;
        uint8_t feedback = ((crc >> 5) & 1) ^ input_bit;
        crc = (crc << 1) & 0x3F;
        if (feedback) {
            crc ^= 0x03;
        }
    }
    return crc;
}

// Method 4: Alternative polynomial interpretation
uint8_t crc6_alt(uint32_t data18) {
    uint8_t crc = 0;
    for (int i = 17; i >= 0; i--) {
        uint8_t bit = (data18 >> i) & 1;
        if ((crc ^ (bit << 5)) & 0x20) {
            crc = ((crc << 1) ^ 0x43) & 0x3F;
        } else {
            crc = (crc << 1) & 0x3F;
        }
    }
    return crc;
}

// Method 5: Init with 0x3F instead of 0x00
uint8_t crc6_init3F(uint32_t data18) {
    uint8_t crc = 0x3F;  // Different init
    for (int i = 17; i >= 0; i--) {
        uint8_t bit = (data18 >> i) & 1;
        uint8_t feedback = ((crc >> 5) & 1) ^ bit;
        crc = (crc << 1) & 0x3F;
        if (feedback) {
            crc ^= 0x03;
        }
    }
    return crc;
}

// Method 6: Process LSB first instead of MSB first
uint8_t crc6_lsb(uint32_t data18) {
    uint8_t crc = 0;
    for (int i = 0; i < 18; i++) {  // LSB first
        uint8_t bit = (data18 >> i) & 1;
        uint8_t feedback = (crc & 1) ^ bit;
        crc = crc >> 1;
        if (feedback) {
            crc ^= 0x30;  // Reflected polynomial
        }
    }
    return crc;
}

// Method 7: Final XOR with 0x3F
uint8_t crc6_xor3F(uint32_t data18) {
    uint8_t crc = 0;
    for (int i = 17; i >= 0; i--) {
        uint8_t bit = (data18 >> i) & 1;
        uint8_t feedback = ((crc >> 5) & 1) ^ bit;
        crc = (crc << 1) & 0x3F;
        if (feedback) {
            crc ^= 0x03;
        }
    }
    return crc ^ 0x3F;  // Final XOR
}

// Method 8: Inverted CRC (some sensors invert output)
uint8_t crc6_inverted(uint32_t data18) {
    uint8_t crc = 0;
    for (int i = 17; i >= 0; i--) {
        uint8_t bit = (data18 >> i) & 1;
        uint8_t feedback = ((crc >> 5) & 1) ^ bit;
        crc = (crc << 1) & 0x3F;
        if (feedback) {
            crc ^= 0x03;
        }
    }
    return (~crc) & 0x3F;  // Bitwise invert
}

uint32_t readRawFrame() {
    uint8_t data[3];

    SPI1.beginTransaction(SPISettings(spi_clock, MSBFIRST, spi_mode));
    digitalWrite(CS_PIN, LOW);
    delayMicroseconds(1);

    data[0] = SPI1.transfer(0xFF);
    data[1] = SPI1.transfer(0xFF);
    data[2] = SPI1.transfer(0xFF);

    digitalWrite(CS_PIN, HIGH);
    SPI1.endTransaction();

    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println();
    Serial.println("=========================================");
    Serial.println("MT6701 CRC6 Debug Test");
    Serial.println("=========================================");
    Serial.println();

    SPI1.begin();
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    Serial.println("Frame format (24 bits):");
    Serial.println("  [23:10] 14-bit angle");
    Serial.println("  [9:8]   Field status (Mg[1:0])");
    Serial.println("  [7]     Push button (Mg[2])");
    Serial.println("  [6]     Track loss (Mg[3])");
    Serial.println("  [5:0]   CRC6");
    Serial.println();
    Serial.println("Press any key to capture frames...");
    Serial.println();
}

void loop() {
    static uint32_t last_print = 0;
    static uint32_t crc_match_count = 0;
    static uint32_t total_count = 0;

    // Capture at 10 Hz for readability
    if (millis() - last_print >= 100) {
        last_print = millis();

        uint32_t frame = readRawFrame();
        total_count++;

        // Extract fields per datasheet
        uint16_t raw_angle = (frame >> 10) & 0x3FFF;      // Bits 23-10
        uint8_t field_status = (frame >> 8) & 0x03;       // Bits 9-8
        uint8_t push_button = (frame >> 7) & 0x01;        // Bit 7
        uint8_t track_loss = (frame >> 6) & 0x01;         // Bit 6
        uint8_t crc_received = frame & 0x3F;              // Bits 5-0

        // Data for CRC calculation (bits 23-6 = 18 bits)
        uint32_t data18 = frame >> 6;

        // Calculate CRC with different methods
        uint8_t crc_lookup = crc6_lookup(data18);
        uint8_t crc_bitwise = crc6_bitwise(data18);
        uint8_t crc_smartknob = crc6_smartknob(data18);
        uint8_t crc_alt = crc6_alt(data18);
        uint8_t crc_init3F = crc6_init3F(data18);
        uint8_t crc_lsb = crc6_lsb(data18);
        uint8_t crc_xor3F = crc6_xor3F(data18);
        uint8_t crc_inv = crc6_inverted(data18);

        // Check if any method matches
        bool match_lookup = (crc_received == crc_lookup);
        bool match_bitwise = (crc_received == crc_bitwise);
        bool match_smartknob = (crc_received == crc_smartknob);
        bool match_alt = (crc_received == crc_alt);
        bool match_init3F = (crc_received == crc_init3F);
        bool match_lsb = (crc_received == crc_lsb);
        bool match_xor3F = (crc_received == crc_xor3F);
        bool match_inv = (crc_received == crc_inv);

        if (match_lookup || match_bitwise || match_smartknob || match_alt ||
            match_init3F || match_lsb || match_xor3F || match_inv) {
            crc_match_count++;
        }

        // Convert angle
        float angle_deg = raw_angle * (360.0f / 16384.0f);

        // Print frame info
        Serial.println("----------------------------------------");
        Serial.print("Frame: 0x");
        if (frame < 0x100000) Serial.print("0");
        if (frame < 0x10000) Serial.print("0");
        if (frame < 0x1000) Serial.print("0");
        if (frame < 0x100) Serial.print("0");
        if (frame < 0x10) Serial.print("0");
        Serial.print(frame, HEX);
        Serial.print(" (bin: ");
        for (int i = 23; i >= 0; i--) {
            Serial.print((frame >> i) & 1);
            if (i == 10 || i == 8 || i == 7 || i == 6) Serial.print(" ");
        }
        Serial.println(")");

        Serial.print("Angle raw: ");
        Serial.print(raw_angle);
        Serial.print(" (0x");
        Serial.print(raw_angle, HEX);
        Serial.print(") -> ");
        Serial.print(angle_deg, 2);
        Serial.println(" deg");

        Serial.print("Status: field=");
        Serial.print(field_status);
        Serial.print(" btn=");
        Serial.print(push_button);
        Serial.print(" track_loss=");
        Serial.println(track_loss);

        Serial.print("CRC data (18 bits): 0x");
        Serial.print(data18, HEX);
        Serial.print(" (");
        for (int i = 17; i >= 0; i--) {
            Serial.print((data18 >> i) & 1);
            if (i == 12 || i == 6) Serial.print(" ");
        }
        Serial.println(")");

        Serial.print("CRC received:  0x");
        if (crc_received < 0x10) Serial.print("0");
        Serial.print(crc_received, HEX);
        Serial.print(" (");
        for (int i = 5; i >= 0; i--) {
            Serial.print((crc_received >> i) & 1);
        }
        Serial.println(")");

        // Print all CRC comparisons compactly
        Serial.print("CRCs: lkp=");
        Serial.print(crc_lookup, HEX);
        Serial.print(" bit=");
        Serial.print(crc_bitwise, HEX);
        Serial.print(" i3F=");
        Serial.print(crc_init3F, HEX);
        Serial.print(" lsb=");
        Serial.print(crc_lsb, HEX);
        Serial.print(" x3F=");
        Serial.print(crc_xor3F, HEX);
        Serial.print(" inv=");
        Serial.print(crc_inv, HEX);

        // Show which matched
        Serial.print(" | ");
        if (match_lookup) Serial.print("LKP ");
        if (match_init3F) Serial.print("I3F ");
        if (match_lsb) Serial.print("LSB ");
        if (match_xor3F) Serial.print("X3F ");
        if (match_inv) Serial.print("INV ");
        if (!match_lookup && !match_init3F && !match_lsb && !match_xor3F && !match_inv) {
            Serial.print("NONE");
        }
        Serial.println();

        Serial.print("Match rate: ");
        Serial.print(crc_match_count);
        Serial.print("/");
        Serial.print(total_count);
        Serial.print(" (");
        Serial.print((float)crc_match_count / total_count * 100.0f, 1);
        Serial.println("%)");
    }
}