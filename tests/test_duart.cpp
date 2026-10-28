#include <cstdint>
#include <vector>

#include "check.h"
#include "duart/duart.h"

using ens::duart::Duart;

namespace {

// Register numbers (A4..A1).
constexpr uint8_t kMR = 0, kSR = 1, kCSR = 1, kCR = 2, kHR = 3, kACR = 4, kIPCR = 4, kISR = 5,
                  kIMR = 5, kCUR = 6, kCTUR = 6, kCLR = 7, kCTLR = 7, kIVR = 12, kIP = 13,
                  kOPCR = 13, kStart = 14, kSetOp = 14, kStop = 15, kResetOp = 15;

constexpr uint32_t kX1 = 3686400;
constexpr uint32_t kChar9600 = 3840;  // 10 bits x 16 ticks x 24 X1 clocks

// Channel A: 8 bits, no parity, 1 stop bit, 9600 baud both ways, TX and RX enabled.
void setupA(Duart& d) {
    d.write(kMR, 0x13);
    d.write(kMR, 0x07);
    d.write(kCSR, 0xBB);
    d.write(kCR, 0x05);
}

}  // namespace

TEST(duart_reset_state) {
    Duart d(kX1);
    CHECK_EQ(d.ivr(), 0x0F);
    CHECK_EQ(d.read(kISR), 0);
    CHECK_EQ(d.read(kSR) & 0x04, 0);  // transmitter disabled: TxRDY = 0
    CHECK_EQ(d.outputPins(), 0xFF);
    CHECK(!d.irq());
    // reset() restores the same state after use.
    d.write(kIVR, 0x40);
    d.write(kSetOp, 0x81);
    d.write(kIMR, 0xFF);
    d.reset();
    CHECK_EQ(d.ivr(), 0x0F);
    CHECK_EQ(d.outputPins(), 0xFF);
    CHECK(!d.irq());
    CHECK_EQ(d.read(kSR), 0);
}

TEST(duart_mr_pointer) {
    Duart d(kX1);
    d.write(kMR, 0x13);  // MR1A
    d.write(kMR, 0x07);  // MR2A
    d.write(kCR, 0x10);  // reset MR pointer
    CHECK_EQ(d.read(kMR), 0x13);
    CHECK_EQ(d.read(kMR), 0x07);
    CHECK_EQ(d.read(kMR), 0x07);  // stays at MR2
    // Channel B has its own pointer.
    d.write(8, 0x1B);
    d.write(8, 0x0F);
    d.write(10, 0x10);
    CHECK_EQ(d.read(8), 0x1B);
    CHECK_EQ(d.read(8), 0x0F);
}

TEST(duart_tx_character_timing) {
    Duart d(kX1);
    setupA(d);
    CHECK_EQ(d.read(kSR) & 0x0C, 0x0C);  // enabled and idle: TxRDY, TxEMT
    d.write(kHR, 0x55);
    CHECK_EQ(d.read(kSR) & 0x0C, 0x04);  // TxRDY = 1, TxEMT = 0
    d.advance(kChar9600 - 1);
    CHECK(d.takeTransmitted(0).empty());
    d.advance(1);
    const std::vector<uint8_t> out = d.takeTransmitted(0);
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0], 0x55);
    CHECK_EQ(d.read(kSR) & 0x0C, 0x0C);
    CHECK(d.takeTransmitted(0).empty());
}

TEST(duart_tx_double_buffer) {
    Duart d(kX1);
    setupA(d);
    d.write(kHR, 0x11);
    d.write(kHR, 0x22);                  // waits in the holding register
    CHECK_EQ(d.read(kSR) & 0x0C, 0x00);  // TxRDY = 0, TxEMT = 0
    d.advance(kChar9600);
    CHECK_EQ(d.read(kSR) & 0x0C, 0x04);  // 0x22 moved to the shift register: TxRDY again
    d.advance(kChar9600);
    const std::vector<uint8_t> out = d.takeTransmitted(0);
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0], 0x11);
    CHECK_EQ(out[1], 0x22);
    CHECK_EQ(d.read(kSR) & 0x0C, 0x0C);
}

TEST(duart_tx_disabled_ignores_writes) {
    Duart d(kX1);
    d.write(kMR, 0x13);
    d.write(kMR, 0x07);
    d.write(kCSR, 0xBB);
    d.write(kHR, 0x77);  // transmitter disabled
    d.write(kCR, 0x04);  // enable TX now
    d.advance(10 * kChar9600);
    CHECK(d.takeTransmitted(0).empty());
    // A disable lets the character in the shift register finish and clears TxRDY, TxEMT.
    d.write(kHR, 0x66);
    d.write(kCR, 0x08);
    CHECK_EQ(d.read(kSR) & 0x0C, 0x00);
    d.advance(kChar9600);
    CHECK_EQ(d.takeTransmitted(0).size(), 1u);
    CHECK_EQ(d.read(kSR) & 0x0C, 0x00);
}

TEST(duart_char_length_parity_and_stop_bits) {
    Duart d(kX1);
    d.write(kMR, 0x03);  // 8 bits, with parity
    d.write(kMR, 0x0F);  // 2.000 stop bits
    d.write(kCSR, 0xBB);
    d.write(kCR, 0x04);
    d.write(kHR, 0xAA);
    d.advance(12 * 16 * 24 - 1);  // start + 8 + parity + 2 stop = 12 bits
    CHECK(d.takeTransmitted(0).empty());
    d.advance(1);
    CHECK_EQ(d.takeTransmitted(0).size(), 1u);
    // 5 bits, no parity, stop code 0 = 1.063 bits: (1 + 5) * 16 + 17 = 113 ticks.
    d.write(kCR, 0x10);
    d.write(kMR, 0x10);
    d.write(kMR, 0x00);
    d.write(kHR, 0xFF);  // masked to 5 bits
    d.advance(113 * 24 - 1);
    CHECK(d.takeTransmitted(0).empty());
    d.advance(1);
    const std::vector<uint8_t> out = d.takeTransmitted(0);
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0], 0x1F);
}

TEST(duart_rx_fifo_and_overrun) {
    Duart d(kX1);
    setupA(d);
    for (uint8_t b = 1; b <= 5; ++b) d.receive(0, b);
    d.advance(5 * kChar9600);
    CHECK_EQ(d.read(kSR) & 0x13, 0x13);  // OE, FFULL, RxRDY
    CHECK_EQ(d.overruns(0), 1u);
    CHECK_EQ(d.read(kHR), 1);
    CHECK_EQ(d.read(kHR), 2);
    CHECK_EQ(d.read(kHR), 3);
    CHECK_EQ(d.read(kHR), 5);  // the character waiting in the shift register moved in; 4 was lost
    CHECK_EQ(d.read(kSR) & 0x01, 0);
    CHECK_EQ(d.read(kSR) & 0x10, 0x10);  // OE stays until reset error status
    d.write(kCR, 0x40);
    CHECK_EQ(d.read(kSR) & 0x10, 0);
    CHECK_EQ(d.overruns(0), 1u);  // the count is not cleared by the command
}

TEST(duart_rx_one_char_per_char_time) {
    Duart d(kX1);
    setupA(d);
    d.receive(0, 0x41);
    d.receive(0, 0x42);
    d.advance(kChar9600 - 1);
    CHECK_EQ(d.read(kSR) & 0x01, 0);
    d.advance(1);
    CHECK_EQ(d.read(kSR) & 0x03, 0x01);
    d.advance(kChar9600);
    CHECK_EQ(d.read(kHR), 0x41);
    CHECK_EQ(d.read(kHR), 0x42);
    CHECK_EQ(d.read(kSR) & 0x01, 0);
}

TEST(duart_rx_disabled_drops_bytes) {
    Duart d(kX1);
    d.write(kMR, 0x13);
    d.write(kMR, 0x07);
    d.write(kCSR, 0xBB);
    d.receive(0, 0x99);  // receiver disabled
    d.write(kCR, 0x01);  // enable RX
    d.advance(3 * kChar9600);
    CHECK_EQ(d.read(kSR) & 0x01, 0);
    // A disable loses the character being received.
    d.receive(0, 0x98);
    d.advance(kChar9600 / 2);
    d.write(kCR, 0x02);
    d.write(kCR, 0x01);
    d.advance(3 * kChar9600);
    CHECK_EQ(d.read(kSR) & 0x01, 0);
}

TEST(duart_rx_ready_interrupt_and_ffull_select) {
    Duart d(kX1);
    setupA(d);
    d.write(kIMR, 0x02);
    CHECK(!d.irq());
    d.receive(0, 0x31);
    d.advance(kChar9600);
    CHECK_EQ(d.read(kISR) & 0x02, 0x02);
    CHECK(d.irq());
    CHECK_EQ(d.read(kHR), 0x31);
    CHECK(!d.irq());
    // MR1A[6] = 1 selects FIFO-full for ISR[1].
    d.write(kCR, 0x10);
    d.write(kMR, 0x53);
    d.receive(0, 1);
    d.receive(0, 2);
    d.advance(2 * kChar9600);
    CHECK_EQ(d.read(kISR) & 0x02, 0);
    d.receive(0, 3);
    d.advance(kChar9600);
    CHECK_EQ(d.read(kISR) & 0x02, 0x02);
    CHECK(d.irq());
}

TEST(duart_tx_ready_interrupt) {
    Duart d(kX1);
    d.write(kIMR, 0x01);
    CHECK(!d.irq());
    d.write(kCR, 0x04);  // enabling the transmitter asserts TxRDYA
    CHECK_EQ(d.read(kISR) & 0x01, 0x01);
    CHECK(d.irq());
    d.write(kHR, 0x00);  // loaded character goes straight to the shift register: still ready
    CHECK(d.irq());
    d.write(kHR, 0x00);  // second one waits in THR
    CHECK(!d.irq());
}

TEST(duart_counter_mode_ready) {
    Duart d(kX1);
    d.write(kACR, 0x30);  // counter, X1/16
    d.write(kCTUR, 0x09);
    d.write(kCTLR, 0xC4);
    d.read(kStart);
    d.advance(2500 * 16 - 1);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
    d.read(kStop);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.write(kIMR, 0x08);
    CHECK(!d.irq());
    d.read(kStart);
    d.advance(2500 * 16);
    CHECK(d.irq());
    d.read(kStop);
    CHECK(!d.irq());
}

TEST(duart_counter_prescaler_exact_across_calls) {
    Duart d(kX1);
    d.write(kACR, 0x30);
    d.write(kCTUR, 0x00);
    d.write(kCTLR, 0x0A);
    d.read(kStart);
    for (int i = 0; i < 159; ++i) d.advance(1);  // 10 * 16 - 1 single-clock calls
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
}

TEST(duart_counter_reads_and_rollover) {
    Duart d(kX1);
    d.write(kACR, 0x30);
    d.write(kCTUR, 0x01);
    d.write(kCTLR, 0x00);  // 256
    d.read(kStart);
    d.advance(256 * 16 - 16);
    CHECK_EQ(d.read(kCUR), 0x00);
    CHECK_EQ(d.read(kCLR), 0x01);  // one count left
    d.advance(16);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
    CHECK_EQ(d.read(kCUR), 0x00);  // terminal count: 0
    CHECK_EQ(d.read(kCLR), 0x00);
    d.advance(3 * 16);  // rolls over from 0 to FFFF and keeps counting
    CHECK_EQ(d.read(kCUR), 0xFF);
    CHECK_EQ(d.read(kCLR), 0xFD);
}

TEST(duart_timer_mode_period) {
    Duart d(kX1);
    d.write(kACR, 0x60);  // timer, X1
    d.write(kCTUR, 0x00);
    d.write(kCTLR, 100);
    d.read(kStart);
    // The square wave has a period of 2 x preload clocks and ISR[3] sets once per cycle.
    d.advance(199);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
    d.read(kStop);  // clears ISR[3] but does not stop the timer
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(199);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
    // X1/16 source.
    d.write(kACR, 0x70);
    d.read(kStop);
    d.read(kStart);
    d.advance(200 * 16 - 1);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
}

TEST(duart_timer_runs_without_start_command) {
    Duart d(kX1);
    d.write(kACR, 0x60);  // timer, X1; the preload is written after ACR
    d.write(kCTUR, 0x00);
    d.write(kCTLR, 100);
    d.advance(199);  // no read of reg 14
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
    d.read(kStop);
    d.advance(199);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
}

TEST(duart_leaving_counter_mode_stops_it) {
    Duart d(kX1);
    d.write(kCTLR, 10);
    d.write(kACR, 0x30);
    d.read(kStart);
    d.write(kACR, 0x70);  // timer
    d.write(kACR, 0x30);  // back to counter: not started
    d.read(kStop);
    d.advance(10 * 16 * 5);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.read(kStart);
    d.advance(10 * 16);
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
}

TEST(duart_timer_new_preload_at_terminal_count) {
    Duart d(kX1);
    d.write(kACR, 0x60);
    d.write(kCTLR, 10);
    d.read(kStart);
    d.write(kCTLR, 50);  // not seen until the running count reaches terminal count
    d.advance(10);       // first terminal count: reloads 50
    d.advance(50 - 1);
    CHECK_EQ(d.read(kISR) & 0x08, 0);
    d.advance(1);  // second terminal count, 10 + 50 clocks after the start
    CHECK_EQ(d.read(kISR) & 0x08, 0x08);
}

TEST(duart_op_pins_inverted) {
    Duart d(kX1);
    d.write(kSetOp, 0x40);
    CHECK_EQ(d.outputPins(), 0xBF);
    d.write(kResetOp, 0x40);
    CHECK_EQ(d.outputPins(), 0xFF);
    d.write(kSetOp, 0x05);
    d.write(kSetOp, 0x80);
    CHECK_EQ(d.outputPins(), 0x7A);
    d.write(kResetOp, 0x01);
    CHECK_EQ(d.outputPins(), 0x7B);
}

TEST(duart_ip_change_interrupt) {
    Duart d(kX1);
    d.setInputPins(0x3F);
    d.read(kIPCR);
    d.write(kACR, 0x01);  // IP0 change interrupt enabled
    d.write(kIMR, 0x80);
    d.setInputPins(0x3E);
    CHECK_EQ(d.read(kISR) & 0x80, 0x80);
    CHECK(d.irq());
    CHECK_EQ(d.read(kIP), 0xC0 | 0x3E);  // bit 7 reads 1, bit 6 is IACK (negated)
    CHECK_EQ(d.read(kIPCR), 0x10 | 0x0E);
    CHECK_EQ(d.read(kISR) & 0x80, 0);
    CHECK(!d.irq());
    CHECK_EQ(d.read(kIPCR), 0x0E);  // delta bits cleared by the read
    // IP1 changes latch in IPCR but do not interrupt: ACR[1] = 0.
    d.setInputPins(0x3C);
    CHECK_EQ(d.read(kISR) & 0x80, 0);
    CHECK_EQ(d.read(kIPCR), 0x20 | 0x0C);
}

TEST(duart_rx_bit_clocks) {
    Duart d(4000000);
    d.write(kACR, 0x60);
    d.write(kCTUR, 0x00);
    d.write(kCTLR, 0x04);
    d.write(kCSR, 0xDD);  // receiver and transmitter clocked by the timer
    d.read(kStart);
    CHECK_EQ(d.rxBitClocks(0), 128u);
    CHECK_EQ(d.unsupportedWrites(), 0u);
}

TEST(duart_baud_table) {
    Duart d(kX1);
    // Set 1: 9600 baud is 24 X1 clocks per 16x tick; 50 baud is 4608.
    d.write(kCSR, 0xBB);
    CHECK_EQ(d.rxBitClocks(0), 16u * 24);
    d.write(kCSR, 0x00);
    CHECK_EQ(d.rxBitClocks(0), 16u * 4608);
    d.write(kCSR, 0x11);  // 110 baud: 2096 (table 4-6: 1.759 kHz, -0.069 %)
    CHECK_EQ(d.rxBitClocks(0), 16u * 2096);
    d.write(kCSR, 0xCC);  // 38.4k
    CHECK_EQ(d.rxBitClocks(0), 16u * 6);
    d.write(8 + kCSR, 0x77);  // channel B, 1050 baud: 220
    CHECK_EQ(d.rxBitClocks(1), 16u * 220);
    // Set 2 (ACR[7] = 1): code 0 is 75 baud, code 0xC is 19.2k, code 7 is 2000.
    d.write(kACR, 0x80);
    d.write(kCSR, 0x00);
    CHECK_EQ(d.rxBitClocks(0), 16u * 3072);
    d.write(kCSR, 0xCC);
    CHECK_EQ(d.rxBitClocks(0), 16u * 12);
    d.write(kCSR, 0x77);
    CHECK_EQ(d.rxBitClocks(0), 16u * 115);
    // The divisors are fixed, so the rates scale with X1.
    Duart e(4000000);
    e.write(kCSR, 0xBB);
    CHECK_EQ(e.rxBitClocks(0), 16u * 24);
    CHECK_EQ(d.unsupportedWrites(), 0u);
}

TEST(duart_unsupported_mode_counted) {
    Duart d(kX1);
    d.write(kMR, 0x13);
    d.write(kMR, 0x47);  // MR2A: automatic echo
    CHECK_EQ(d.unsupportedWrites(), 1u);
    d.write(kMR, 0x07);  // normal mode: not counted
    CHECK_EQ(d.unsupportedWrites(), 1u);
    d.write(kCR, 0x60);  // start break
    CHECK_EQ(d.unsupportedWrites(), 2u);
    d.write(kCSR, 0xEE);  // external IP clocks
    CHECK_EQ(d.unsupportedWrites(), 3u);
    d.write(kOPCR, 0x04);  // OP3 as counter/timer output
    CHECK_EQ(d.unsupportedWrites(), 4u);
    d.write(kOPCR, 0x00);
    CHECK_EQ(d.unsupportedWrites(), 4u);
    d.reset();
    CHECK_EQ(d.unsupportedWrites(), 0u);
}

TEST(duart_channel_b_independent) {
    Duart d(kX1);
    d.write(8, 0x13);
    d.write(8, 0x07);
    d.write(9, 0xBB);
    d.write(10, 0x05);
    d.write(11, 0xA5);
    d.receive(1, 0x5A);
    d.advance(kChar9600);
    const std::vector<uint8_t> out = d.takeTransmitted(1);
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0], 0xA5);
    CHECK(d.takeTransmitted(0).empty());
    CHECK_EQ(d.read(9) & 0x01, 0x01);
    CHECK_EQ(d.read(11), 0x5A);
    CHECK_EQ(d.read(kSR) & 0x01, 0);  // channel A untouched
    d.write(kIMR, 0x30);
    CHECK(d.irq());  // TxRDYB is set
}

TEST(duart_ivr_register) {
    Duart d(kX1);
    d.write(kIVR, 0x4C);
    CHECK_EQ(d.ivr(), 0x4C);
    CHECK_EQ(d.read(kIVR), 0x4C);
}
