#include "ks32/machine.h"

#include <algorithm>
#include <utility>

#include "ks32/wave_roms.h"

namespace ens::ks32 {

namespace {

constexpr uint32_t kMask24 = 0xFFFFFF;
constexpr uint32_t kRomBase = 0xC00000, kRomSize = 0x40000;
constexpr uint32_t kRamTop = 0xFF0000;
constexpr uint32_t kOtisBase = 0x200000, kOtisSize = 0x20;
constexpr uint32_t kEspBase = 0x260000, kEspSize = 0x200;
constexpr uint32_t kDuartBase = 0x280000, kDuartSize = 0x20;

bool inWindow(uint32_t a, uint32_t base, uint32_t size) { return a >= base && a < base + size; }
bool isRom(uint32_t a) { return a >= kRomBase && a < kRomBase + kRomSize; }
// DECISION K1: 64 KB RAM, A16/A17 not decoded: mirrored four times over 0-0x3FFFF and at 0xFF0000.
bool isRam(uint32_t a) { return a >= kRamTop || a < 0x40000; }

// DECISION K8: the mux select (DUART OP3..OP0) of each Analog, found by setting one select to a
// distinctive value and watching which Analog Test reading moves.
constexpr uint8_t kAnalogSelect[] = {0, 2, 4, 5, 3, 1, 6};  // by Analog
// DECISION K13: select 0x0F, the reference. The firmware subtracts (reference - 0x180) from the
// slider, wheel, pedal, pressure and battery readings when the reference is above 0x180, and
// nothing at or below it; 0x180 is the highest value that needs no correction.
constexpr uint16_t kReferenceRest = 0x180;

bool sizeOk(const std::vector<uint8_t>& v, size_t want, const char* name, std::string& err) {
    if (v.size() == want) return true;
    err = std::string(name) + ": expected " + std::to_string(want) + " bytes, got " +
          std::to_string(v.size());
    return false;
}

}  // namespace

Machine::Machine(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1)
    : board_(std::move(bank0), std::move(bank1)) {
    for (int pin = 2; pin <= 5; ++pin) duart_.setExternalClock(pin, kSerialClockHz);  // DECISION K3, K16
    // DECISION K10: IP1 is the card detect, low = card in; the slot is empty, so it is high. IP0
    // shows no effect and rests high. The card window (0x2E0000) is not modelled: with IP1 high the
    // firmware never touches it.
    duart_.setInputPins(0x3F);
    for (int a = 0; a < 7; ++a) mux_[kAnalogSelect[a]] = kAnalogRest[a];  // DECISION K13
    mux_[0x0F] = kReferenceRest;
}

void Machine::setAnalog(Analog a, uint16_t raw) {
    mux_[kAnalogSelect[static_cast<int>(a)]] = raw & 0x3FF;
    updateLines();
}

std::unique_ptr<Machine> Machine::create(const RomSet& roms, std::string& err) {
    if (!sizeOk(roms.osUpper, 0x20000, "OS upper", err) || !sizeOk(roms.osLower, 0x10000, "OS lower", err))
        return nullptr;
    std::vector<uint16_t> bank0, bank1;
    if (!buildWaveBanks(roms.u25, roms.u26, roms.rom2, roms.rom3, bank0, bank1, err)) return nullptr;
    std::unique_ptr<Machine> m(new Machine(std::move(bank0), std::move(bank1)));
    m->osUpper_ = roms.osUpper;
    m->osLower_ = roms.osLower;
    m->powerOn();
    return m;
}

void Machine::powerOn() {
    board_.otis().reset();
    board_.esp().reset();
    duart_.reset();
    panel_.reset();
    overlay_ = true;
    carry_ = 0;
    duartAcc_ = 0;
    cpu_.reset();  // the 40 reset cycles count in cycles() and are not slot carry
    updateLines();
}

// DECISION K11: the image is the whole 64 KB RAM, raw. Loading it is a power cycle with the
// battery holding the RAM: every device and the CPU are reset, and the analog inputs keep their
// values (they are the player's controls, not machine state).
bool Machine::loadRamImage(const std::vector<uint8_t>& img, std::string& err) {
    if (!sizeOk(img, ram_.size(), "RAM image", err)) return false;
    ram_ = img;
    powerOn();
    return true;
}

// DECISION K1: ROM 0xC00000-0xC3FFFF: upper EPROM = even bytes, lower EPROM = odd bytes mirrored
// every 0x20000. RAM 64 KB at 0-0x3FFFF (four mirrors) and 0xFF0000; 0-7 read ROM until the first
// write there. OTIS 0x200000 (words), ESP 0x260000 and DUART 0x280000 (odd bytes). A byte access to
// OTIS is traced with size 1 (read: the addressed byte of the register; write: ignored); an even-
// byte access to the ESP or DUART reads 0 and its write is ignored, traced with its even address;
// a word access to the ESP or DUART does the odd-byte access in the low byte (high byte reads 0).
uint8_t Machine::romByte(uint32_t off) const {
    return (off & 1) ? osLower_[(off >> 1) & 0xFFFF] : osUpper_[off >> 1];
}

uint8_t Machine::memByte(uint32_t a, bool& mapped) const {
    mapped = true;
    if (isRom(a)) return romByte(a - kRomBase);
    if (isRam(a)) return (overlay_ && a < 8) ? romByte(a) : ram_[a & 0xFFFF];  // DECISION K1: 0-7 overlay
    mapped = false;
    return 0;
}

uint16_t Machine::peek16(uint32_t addr) const {
    bool mapped;
    addr &= kMask24 & ~1u;
    const uint8_t hi = memByte(addr, mapped);
    const uint8_t lo = memByte(addr + 1, mapped);
    return static_cast<uint16_t>((hi << 8) | lo);
}

void Machine::trace(uint32_t addr, uint16_t value, uint8_t size, bool write, Device dev) {
    if (onBusEvent) onBusEvent({cpu_.totalCycles(), addr, value, size, write, dev});
}

// DECISION K2: the IPL is the highest level of the asserting devices.
// DECISION K15: DUART OP6 is ESPHALT, active high (the pin is high while OPR bit 6 is 0).
// DECISION K8: OP3..OP0 select the mux input on the OTIS POT pin; the conversion is at once.
void Machine::updateLines() {
    int level = 0;
    if (board_.otis().irqAsserted()) level = std::max(level, kOtisIrqLevel);
    if (duart_.irq()) level = std::max(level, kDuartIrqLevel);
    cpu_.setIpl(level);
    board_.esp().setHaltPin((duart_.outputPins() & 0x40) != 0);
    board_.otis().setPot(mux_[duart_.outputPins() & 0x0F]);
}

int Machine::interruptAcknowledge(int level) {
    const bool fromDuart = kDuartIrqLevel != 0 && level == kDuartIrqLevel && duart_.irq();
    const int vec = fromDuart ? duart_.ivr() : m68k::kAutovector;
    trace(static_cast<uint32_t>(level), static_cast<uint16_t>(fromDuart ? vec : 24 + level), 1, false,
          Device::Iack);
    return vec;
}

// DECISION K14: the RESET instruction resets the DUART only (the OS never executes RESET).
void Machine::resetPeripherals() {
    duart_.reset();
    updateLines();
}

uint8_t Machine::read8(uint32_t a) {
    a &= kMask24;
    bool mapped;
    const uint8_t m = memByte(a, mapped);
    if (mapped) return m;
    uint8_t v = 0;
    Device dev = Device::Unmapped;  // DECISION K6: unmapped reads 0, writes are ignored, every access is traced
    if (inWindow(a, kOtisBase, kOtisSize)) {
        const uint16_t w = board_.otis().read(static_cast<uint8_t>((a >> 1) & 0xF));
        v = static_cast<uint8_t>((a & 1) ? w : w >> 8);
        dev = Device::Otis;
    } else if (inWindow(a, kEspBase, kEspSize)) {
        if (a & 1) v = board_.esp().hostRead(static_cast<uint8_t>((a >> 1) & 0xFF));
        dev = Device::Esp;
    } else if (inWindow(a, kDuartBase, kDuartSize)) {
        if (a & 1) v = duart_.read(static_cast<uint8_t>((a >> 1) & 0xF));
        dev = Device::Duart;
    }
    trace(a, v, 1, false, dev);
    updateLines();
    return v;
}

uint16_t Machine::read16(uint32_t a) {
    a &= kMask24;
    bool mapped;
    const uint8_t hi = memByte(a, mapped);
    if (mapped) return static_cast<uint16_t>((hi << 8) | memByte(a + 1, mapped));
    uint16_t v = 0;
    Device dev = Device::Unmapped;
    if (inWindow(a, kOtisBase, kOtisSize)) {
        v = board_.otis().read(static_cast<uint8_t>((a >> 1) & 0xF));
        dev = Device::Otis;
    } else if (inWindow(a, kEspBase, kEspSize)) {
        v = board_.esp().hostRead(static_cast<uint8_t>((a >> 1) & 0xFF));
        dev = Device::Esp;
    } else if (inWindow(a, kDuartBase, kDuartSize)) {
        v = duart_.read(static_cast<uint8_t>((a >> 1) & 0xF));
        dev = Device::Duart;
    }
    trace(a, v, 2, false, dev);
    updateLines();
    return v;
}

void Machine::write8(uint32_t a, uint8_t v) {
    a &= kMask24;
    if (isRom(a)) return;
    if (isRam(a)) {
        if (a < 8) overlay_ = false;
        ram_[a & 0xFFFF] = v;
        return;
    }
    Device dev = Device::Unmapped;
    if (inWindow(a, kOtisBase, kOtisSize)) {
        dev = Device::Otis;  // a byte write to a 16-bit register is ignored
    } else if (inWindow(a, kEspBase, kEspSize)) {
        if (a & 1) board_.esp().hostWrite(static_cast<uint8_t>((a >> 1) & 0xFF), v);
        dev = Device::Esp;
    } else if (inWindow(a, kDuartBase, kDuartSize)) {
        if (a & 1) duart_.write(static_cast<uint8_t>((a >> 1) & 0xF), v);
        dev = Device::Duart;
    }
    trace(a, v, 1, true, dev);
    updateLines();
}

void Machine::write16(uint32_t a, uint16_t v) {
    a &= kMask24;
    if (isRom(a) || isRam(a)) {
        write8(a, static_cast<uint8_t>(v >> 8));
        write8(a + 1, static_cast<uint8_t>(v));
        return;
    }
    Device dev = Device::Unmapped;
    if (inWindow(a, kOtisBase, kOtisSize)) {
        board_.otis().write(static_cast<uint8_t>((a >> 1) & 0xF), v);
        dev = Device::Otis;
    } else if (inWindow(a, kEspBase, kEspSize)) {
        board_.esp().hostWrite(static_cast<uint8_t>((a >> 1) & 0xFF), static_cast<uint8_t>(v));
        dev = Device::Esp;
    } else if (inWindow(a, kDuartBase, kDuartSize)) {
        duart_.write(static_cast<uint8_t>((a >> 1) & 0xF), static_cast<uint8_t>(v));
        dev = Device::Duart;
    }
    trace(a, v, 2, true, dev);
    updateLines();
}

// DECISION K4: one voice slot = 16 CPU clocks, with the overshoot of the last instruction carried
// into the next slot (a carry of 16 or more skips the CPU); then the OTIS slot, the DUART by an
// exact rational X1 accumulator, the ESP and the IPL. Slot i of an n-slot frame.
void Machine::slot(int i, int n) {
    if (carry_ >= kSlotClocks) {
        carry_ -= kSlotClocks;
    } else {
        const int want = kSlotClocks - carry_;
        carry_ = cpu_.run(want) - want;
    }
    board_.otis().stepVoice();
    duartAcc_ += static_cast<uint32_t>(kSlotClocks) * kDuartHz;
    const uint32_t x1 = duartAcc_ / kCpuHz;
    duartAcc_ %= kCpuHz;
    if (x1) duart_.advance(x1);
    // DECISION K7: the panel takes the bytes that finished leaving TxDB in this slot and answers
    // with no processing delay of its own; its bytes enter RxDB at the line rate (D2).
    for (uint8_t b : duart_.takeTransmitted(kPanelChannel)) panel_.fromCpu(b);
    for (uint8_t b : panel_.takeToCpu()) duart_.receive(kPanelChannel, b);
    // DECISION K5: while running the ESP advances once at frame end (runFrame, via Board::route);
    // while halted slot i gets floor(89(i+1)/n) - floor(89 i/n) idle steps.
    Esp& esp = board_.esp();
    if (esp.halted()) {
        const int a = std::min(i, n), b = std::min(i + 1, n);
        const int idle = kEspSlotsPerFrame * b / n - kEspSlotsPerFrame * a / n;
        for (int k = 0; k < idle; ++k) esp.idleStep();
    }
    updateLines();
}

void Machine::runFrame(int16_t& mainL, int16_t& mainR) {
    Otis& otis = board_.otis();
    const int n = otis.activeVoices();  // N = ACT + 1; a CPU write to ACT mid-frame only moves the end
    int i = 0;
    do {
        slot(i++, n);
    } while (!otis.frameReady());
    board_.route(otis.takeFrame(), mainL, mainR, board_.esp().halted() ? 0 : kEspSlotsPerFrame);
}

}  // namespace ens::ks32
