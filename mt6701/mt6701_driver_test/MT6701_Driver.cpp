/*
 * MT6701_Driver implementation
 */

#include "MT6701_Driver.h"

// CRC6 lookup table for MT6701 SSI protocol
// Polynomial: x^6 + x + 1 (0x43), init: 0x00
// Calculated over 18 bits (14-bit angle + 4-bit status)
const uint8_t MT6701_Driver::crc6_table_[64] = {
    0x00, 0x03, 0x06, 0x05, 0x0C, 0x0F, 0x0A, 0x09,
    0x18, 0x1B, 0x1E, 0x1D, 0x14, 0x17, 0x12, 0x11,
    0x30, 0x33, 0x36, 0x35, 0x3C, 0x3F, 0x3A, 0x39,
    0x28, 0x2B, 0x2E, 0x2D, 0x24, 0x27, 0x22, 0x21,
    0x23, 0x20, 0x25, 0x26, 0x2F, 0x2C, 0x29, 0x2A,
    0x3B, 0x38, 0x3D, 0x3E, 0x37, 0x34, 0x31, 0x32,
    0x13, 0x10, 0x15, 0x16, 0x1F, 0x1C, 0x19, 0x1A,
    0x0B, 0x08, 0x0D, 0x0E, 0x07, 0x04, 0x01, 0x02
};

MT6701_Driver::MT6701_Driver() : initialized_(false) {
    memset(encoders_, 0, sizeof(encoders_));
    memset(&config_, 0, sizeof(config_));
}

bool MT6701_Driver::begin(const Config& config) {
    if (config.num_encoders == 0 || config.num_encoders > MAX_ENCODERS) {
        return false;
    }

    config_ = config;

    // Set defaults if not specified
    if (config_.spi_clock_hz == 0) {
        config_.spi_clock_hz = 4000000;
    }
    if (config_.filter_alpha <= 0.0f || config_.filter_alpha > 1.0f) {
        config_.filter_alpha = 0.4f;
    }
    if (config_.crc_fail_threshold == 0) {
        config_.crc_fail_threshold = 10;
    }

    // Initialize SPI1
    SPI1.begin();

    // Initialize CS pins
    for (uint8_t i = 0; i < config_.num_encoders; i++) {
        pinMode(config_.cs_pins[i], OUTPUT);
        digitalWrite(config_.cs_pins[i], HIGH);
    }

    // Initialize encoder states
    for (uint8_t i = 0; i < config_.num_encoders; i++) {
        encoders_[i].angle = 0.0f;
        encoders_[i].prev_angle = 0.0f;
        encoders_[i].velocity = 0.0f;
        encoders_[i].last_read_us = micros();
        encoders_[i].health = Health::OK;
        encoders_[i].field_status = FieldStatus::NORMAL;
        memset(&encoders_[i].diag, 0, sizeof(DiagnosticCounters));
    }

    // Perform initial reads to prime the filter
    for (uint8_t i = 0; i < config_.num_encoders; i++) {
        read(i);
        encoders_[i].prev_angle = encoders_[i].angle;
    }

    initialized_ = true;
    return true;
}

uint8_t MT6701_Driver::calculateCRC6(uint32_t data) const {
    // CRC6 over 18 bits (bits 23-6 of frame)
    uint8_t crc = 0;

    // Process 18 bits, 6 bits at a time
    crc = crc6_table_[crc ^ ((data >> 12) & 0x3F)];
    crc = crc6_table_[crc ^ ((data >> 6) & 0x3F)];
    crc = crc6_table_[crc ^ (data & 0x3F)];

    return crc;
}

uint32_t MT6701_Driver::readRawFrame(uint8_t encoder_id) {
    uint32_t frame = 0;
    uint8_t data[3];

    SPI1.beginTransaction(SPISettings(config_.spi_clock_hz, MSBFIRST, SPI_MODE1));
    digitalWrite(config_.cs_pins[encoder_id], LOW);
    delayMicroseconds(1);

    data[0] = SPI1.transfer(0xFF);
    data[1] = SPI1.transfer(0xFF);
    data[2] = SPI1.transfer(0xFF);

    digitalWrite(config_.cs_pins[encoder_id], HIGH);
    SPI1.endTransaction();

    frame = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    return frame;
}

bool MT6701_Driver::processFrame(uint32_t frame, uint8_t encoder_id) {
    EncoderState& enc = encoders_[encoder_id];
    enc.diag.total_reads++;

    // Extract fields from 24-bit frame
    // Bits 23-10: 14-bit angle
    // Bits 9-8: field status (Mg[1:0])
    // Bit 7: push button (Mg[2])
    // Bit 6: track loss (Mg[3])
    // Bits 5-0: CRC6

    uint16_t raw_angle = (frame >> 10) & 0x3FFF;
    uint8_t field_bits = (frame >> 8) & 0x03;
    bool track_loss = (frame >> 6) & 0x01;
    uint8_t crc_received = frame & 0x3F;

    // Calculate CRC over bits 23-6 (18 bits)
    uint8_t crc_calculated = calculateCRC6(frame >> 6);

    // Update field status
    enc.field_status = static_cast<FieldStatus>(field_bits);

    // Check for track loss
    if (track_loss) {
        enc.diag.track_loss_count++;
        enc.health = Health::FAULTED;
        return false;
    }

    // Validate CRC
    if (crc_received != crc_calculated) {
        enc.diag.crc_fail_total++;
        enc.diag.crc_fail_consecutive++;

        if (enc.diag.crc_fail_consecutive >= config_.crc_fail_threshold) {
            enc.health = Health::FAULTED;
        }
        return false;
    }

    // CRC passed - reset consecutive counter
    enc.diag.crc_fail_consecutive = 0;
    enc.diag.last_good_us = micros();

    // Track field warnings
    if (enc.field_status == FieldStatus::WEAK) {
        enc.diag.field_weak_count++;
    } else if (enc.field_status == FieldStatus::STRONG) {
        enc.diag.field_strong_count++;
    }

    // Convert raw angle to radians (0 to 2*PI)
    float new_angle = raw_angle * (TWO_PI_F / 16384.0f);

    // Apply EMA filter
    enc.angle = config_.filter_alpha * new_angle +
                (1.0f - config_.filter_alpha) * enc.angle;

    // Calculate velocity (rad/sec) from filtered position
    uint32_t now_us = micros();
    uint32_t dt_us = now_us - enc.last_read_us;
    if (dt_us > 0 && dt_us < 1000000) {  // Sanity check: < 1 second
        float dt_sec = dt_us * 1e-6f;

        // Handle angle wrap-around for velocity calculation
        float delta = enc.angle - enc.prev_angle;
        if (delta > 3.14159265f) {
            delta -= TWO_PI_F;
        } else if (delta < -3.14159265f) {
            delta += TWO_PI_F;
        }

        enc.velocity = delta / dt_sec;
    }

    enc.prev_angle = enc.angle;
    enc.last_read_us = now_us;

    // Update health
    updateHealth(encoder_id);

    return true;
}

void MT6701_Driver::updateHealth(uint8_t encoder_id) {
    EncoderState& enc = encoders_[encoder_id];

    // Already faulted - stay faulted until reset
    if (enc.health == Health::FAULTED) {
        return;
    }

    // Check for degraded conditions
    bool degraded = false;

    // Field not normal
    if (enc.field_status != FieldStatus::NORMAL) {
        degraded = true;
    }

    // CRC error rate > 5% (check over last ~100 reads minimum)
    if (enc.diag.total_reads >= 100) {
        float error_rate = (float)enc.diag.crc_fail_total / enc.diag.total_reads;
        if (error_rate > 0.05f) {
            degraded = true;
        }
    }

    enc.health = degraded ? Health::DEGRADED : Health::OK;
}

MT6701_Driver::Health MT6701_Driver::read(uint8_t encoder_id) {
    if (encoder_id >= config_.num_encoders) {
        return Health::FAULTED;
    }

    uint32_t frame = readRawFrame(encoder_id);
    processFrame(frame, encoder_id);

    return encoders_[encoder_id].health;
}

void MT6701_Driver::readAll() {
    for (uint8_t i = 0; i < config_.num_encoders; i++) {
        read(i);
    }
}

float MT6701_Driver::getAngle(uint8_t encoder_id) const {
    if (encoder_id >= config_.num_encoders) {
        return 0.0f;
    }
    return encoders_[encoder_id].angle;
}

float MT6701_Driver::getVelocity(uint8_t encoder_id) const {
    if (encoder_id >= config_.num_encoders) {
        return 0.0f;
    }
    return encoders_[encoder_id].velocity;
}

MT6701_Driver::Health MT6701_Driver::getHealth(uint8_t encoder_id) const {
    if (encoder_id >= config_.num_encoders) {
        return Health::FAULTED;
    }
    return encoders_[encoder_id].health;
}

bool MT6701_Driver::isFaulted(uint8_t encoder_id) const {
    return getHealth(encoder_id) == Health::FAULTED;
}

bool MT6701_Driver::anyFaulted() const {
    for (uint8_t i = 0; i < config_.num_encoders; i++) {
        if (encoders_[i].health == Health::FAULTED) {
            return true;
        }
    }
    return false;
}

void MT6701_Driver::reset(uint8_t encoder_id) {
    if (encoder_id >= config_.num_encoders) {
        return;
    }

    EncoderState& enc = encoders_[encoder_id];
    enc.health = Health::OK;
    memset(&enc.diag, 0, sizeof(DiagnosticCounters));
    enc.diag.last_good_us = micros();
}

void MT6701_Driver::printDiagnostics(Stream& out, bool verbose) const {
    out.println("=== MT6701 Diagnostics ===");

    for (uint8_t i = 0; i < config_.num_encoders; i++) {
        const EncoderState& enc = encoders_[i];

        out.print("Enc ");
        out.print(i);
        out.print(": ");

        // Health status
        switch (enc.health) {
            case Health::OK:       out.print("OK      "); break;
            case Health::DEGRADED: out.print("DEGRADED"); break;
            case Health::FAULTED:  out.print("FAULTED "); break;
        }

        // Angle and velocity
        out.print(" | ang=");
        out.print(enc.angle * DEG_PER_RAD, 1);
        out.print("deg | vel=");
        out.print(enc.velocity * DEG_PER_RAD, 1);
        out.print("deg/s");

        // Field status
        out.print(" | field=");
        switch (enc.field_status) {
            case FieldStatus::NORMAL: out.print("NORM"); break;
            case FieldStatus::STRONG: out.print("STRG"); break;
            case FieldStatus::WEAK:   out.print("WEAK"); break;
            case FieldStatus::ERROR:  out.print("ERR "); break;
        }

        out.println();

        if (verbose) {
            out.print("         CRC fails: ");
            out.print(enc.diag.crc_fail_total);
            out.print(" total, ");
            out.print(enc.diag.crc_fail_consecutive);
            out.println(" consecutive");

            out.print("         Field warnings: weak=");
            out.print(enc.diag.field_weak_count);
            out.print(", strong=");
            out.print(enc.diag.field_strong_count);
            out.print(", track_loss=");
            out.println(enc.diag.track_loss_count);

            out.print("         Total reads: ");
            out.print(enc.diag.total_reads);
            if (enc.diag.total_reads > 0) {
                float error_rate = (float)enc.diag.crc_fail_total / enc.diag.total_reads * 100.0f;
                out.print(" (CRC error rate: ");
                out.print(error_rate, 2);
                out.print("%)");
            }
            out.println();

            uint32_t age_ms = (micros() - enc.diag.last_good_us) / 1000;
            out.print("         Last good read: ");
            out.print(age_ms);
            out.println("ms ago");
            out.println();
        }
    }
}
