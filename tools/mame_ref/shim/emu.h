#pragma once
// Minimal stand-in for MAME's emu.h, just enough for the vendored es5506.cpp/.h and
// es5510.cpp/.h (tools/mame_ref/vendor/) to compile unmodified. Not part of MAME.
//
// Only the behaviour the chip code depends on is reproduced, from MAME at the commit in
// PINNED_COMMIT.txt:
//   - sound_stream::put/put_int/put_int_clamp/get_output with sample_t = float (emu/disound.h).
//   - util::sext (lib/util/coretmpl.h), mul_32x32 (osd/eminline.h), ACCESSING_BITS_* (emu/emumem.h),
//     BIT.
//   - memory_access<>::cache::read_word: the word at that word address of the ROM installed
//     with install_rom, 0 when unmapped (MAME's default unmap value).
// Everything else (save states, state registration, logging, callbacks, clocks, scheduling)
// is a no-op. The harness drives the devices explicitly: start()/reset(), one-sample stream
// updates and direct host-handler calls (see tools/mame_ref/main.cpp and docs/mame-ref.md).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using offs_t = u32;

#define ATTR_COLD

// ---- emumem.h / eminline.h / coretmpl.h equivalents -------------------------------------------------------

template <typename T, typename U>
constexpr T BIT(T x, U n) noexcept {
    return (x >> n) & T(1);
}

constexpr s64 mul_32x32(s32 a, s32 b) { return s64(a) * s64(b); }

#define ACCESSING_BITS_0_7 ((mem_mask & 0x000000ffU) != 0)
#define ACCESSING_BITS_8_15 ((mem_mask & 0x0000ff00U) != 0)

enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };
enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };
enum { STATE_GENPC = -1, STATE_GENPCBASE = -2 };

namespace util {

template <typename T, typename U>
constexpr std::make_signed_t<T> sext(T value, U width) noexcept {
    return std::make_signed_t<T>(value << (8 * sizeof(value) - width)) >> (8 * sizeof(value) - width);
}

// Disassembler base (only so ES5510's create_disassembler() compiles; never called).
class disasm_interface {
public:
    class data_buffer {};
    virtual ~disasm_interface() = default;
    virtual u32 opcode_alignment() const = 0;
    virtual offs_t disassemble(std::ostream& stream, offs_t pc, const data_buffer& opcodes,
                               const data_buffer& params) = 0;
};

}  // namespace util

// MAME's string_format is a type-safe printf. Only ES5510::list_program (a debug listing the
// harness never calls) uses it, so the shim returns the format string unformatted.
template <typename... Args>
std::string string_format(const char* fmt, Args&&...) {
    return fmt;
}

// ---- device types, machine, device_t ----------------------------------------------------------

using device_type = const char*;
#define DECLARE_DEVICE_TYPE(Type, Class) extern device_type const Type;
#define DEFINE_DEVICE_TYPE(Type, Class, ShortName, FullName) device_type const Type = ShortName;

#define NAME(x) x, #x
#define STRUCT_MEMBER(s, m) s, #s "." #m

struct machine_config {};

class running_machine {
public:
    std::string describe_context() const { return "mame_ref"; }
    bool side_effects_disabled() const { return false; }
};

struct finder_base {
    static constexpr const char* DUMMY_TAG = "finder_dummy_tag";
};

class device_t {
public:
    device_t(const machine_config&, device_type, const char*, device_t*, u32 clock) : clock_(clock) {}
    virtual ~device_t() = default;
    device_t(const device_t&) = delete;
    device_t& operator=(const device_t&) = delete;

    // Harness entry points (MAME's scheduler calls these hooks during machine start/reset).
    void start() { device_start(); }
    void reset() { device_reset(); }

    u32 clock() const { return clock_; }
    running_machine& machine() { return machine_; }

    template <typename... Args>
    void logerror(Args&&...) const {}
    template <typename... Args>
    void save_item(Args&&...) {}
    template <typename... Args>
    void save_pointer(Args&&...) {}

protected:
    virtual void device_start() {}
    virtual void device_reset() {}
    virtual void device_stop() {}
    virtual void device_clock_changed() {}
    void notify_clock_changed() { device_clock_changed(); }

private:
    u32 clock_;
    running_machine machine_;
};

// ---- device callbacks (devcb): unbound, so writes do nothing and reads return the default ----

class devcb_write_line {
public:
    explicit devcb_write_line(device_t&) {}
    devcb_write_line& bind() { return *this; }
    void operator()(int) {}
};

class devcb_write32 {
public:
    explicit devcb_write32(device_t&) {}
    devcb_write32& bind() { return *this; }
    void operator()(u32) {}
};

class devcb_read16 {
public:
    devcb_read16(device_t&, u16 dflt) : dflt_(dflt) {}
    devcb_read16& bind() { return *this; }
    bool isunset() const { return true; }
    u16 operator()(offs_t) { return dflt_; }

private:
    u16 dflt_;
};

// ---- memory: regions, address spaces, caches -------------------------------------------------

// A ROM region. MAME keeps 16-bit regions in host order, so `base()` is the words as stored.
class memory_region {
public:
    explicit memory_region(std::vector<u16> words) : words_(std::move(words)) {}
    u8* base() { return reinterpret_cast<u8*>(words_.data()); }
    u32 bytes() const { return static_cast<u32>(words_.size() * 2); }

private:
    std::vector<u16> words_;
};

class optional_memory_region {
public:
    optional_memory_region(device_t&, const char*) {}
    void set_tag(memory_region* r) { region_ = r; }
    explicit operator bool() const { return region_ != nullptr; }
    memory_region* operator->() const { return region_; }

private:
    memory_region* region_ = nullptr;
};

class address_space_config {
public:
    // The geometry is fixed by memory_access<> in the device header; nothing to store here.
    address_space_config(const char*, endianness_t, u8, u8, s8) {}
};

// One 16-bit word-addressed space: a ROM installed at [0, end], unmapped elsewhere (reads 0).
class address_space {
public:
    void install_rom(offs_t start, offs_t end, void* base) {
        start_ = start;
        end_ = end;
        rom_ = static_cast<const u16*>(base);
    }
    template <typename Cache>
    void cache(Cache& c) {
        c.space_ = this;
    }
    u16 read_word(offs_t a) const { return (rom_ && a >= start_ && a <= end_) ? rom_[a - start_] : 0; }

private:
    const u16* rom_ = nullptr;
    offs_t start_ = 0, end_ = 0;
};

template <int AddrWidth, int DataWidthLevel, int AddrShift, endianness_t Endian>
struct memory_access {
    static_assert(DataWidthLevel == 1 && AddrShift == -1, "shim supports 16-bit word spaces only");
    class cache {
    public:
        u16 read_word(offs_t a) const {
            return space_ ? space_->read_word(a & ((offs_t(1) << AddrWidth) - 1)) : 0;
        }

    private:
        friend class address_space;
        const address_space* space_ = nullptr;
    };
};

class device_memory_interface {
public:
    using space_config_vector = std::vector<std::pair<int, const address_space_config*>>;
    device_memory_interface(const machine_config&, device_t&) {}
    virtual ~device_memory_interface() = default;
    address_space& space(int n) { return spaces_[n & 3]; }
    bool has_configured_map(int) const { return false; }

protected:
    virtual space_config_vector memory_space_config() const = 0;

private:
    address_space spaces_[4];
};

// ---- sound -----------------------------------------------------------------------------------

class sound_stream {
public:
    using sample_t = float;

    explicit sound_stream(u32 outputs) : out_(outputs, 0.0f) {}
    u32 output_count() const { return static_cast<u32>(out_.size()); }
    // The harness always updates one sample at a time.
    s32 samples() const { return 1; }
    void set_sample_rate(u32) {}
    void update() {}  // writes land between frames; the harness generates samples explicitly

    void put(s32 output, s32, sample_t sample) { out_[output] = sample; }
    // MAME: put(output, index, double(sample)/max), the double narrowed to sample_t (float).
    void put_int(s32 output, s32 index, s32 sample, s32 max) { put(output, index, sample_t(double(sample) / max)); }
    void put_int_clamp(s32 output, s32 index, s32 sample, s32 maxclamp) {
        put_int(output, index, std::clamp(sample, -maxclamp, maxclamp - 1), maxclamp);
    }
    sample_t get_output(s32 output, s32) const { return out_[output]; }

private:
    std::vector<sample_t> out_;
};

class device_sound_interface {
public:
    device_sound_interface(const machine_config&, device_t&) {}
    virtual ~device_sound_interface() = default;

    // Harness entry point: render exactly one output sample.
    sound_stream& update_one_sample() {
        sound_stream_update(*stream_);
        return *stream_;
    }

protected:
    virtual void sound_stream_update(sound_stream& stream) = 0;
    sound_stream* stream_alloc(int, int outputs, u32) {
        stream_ = std::make_unique<sound_stream>(static_cast<u32>(outputs));
        return stream_.get();
    }

private:
    std::unique_ptr<sound_stream> stream_;
};

// ---- CPU -------------------------------------------------------------------------------------

class cpu_device : public device_t, public device_memory_interface {
public:
    cpu_device(const machine_config& mconfig, device_type type, const char* tag, device_t* owner, u32 clock)
        : device_t(mconfig, type, tag, owner, clock), device_memory_interface(mconfig, *this) {}

protected:
    struct state_entry {
        state_entry& noshow() { return *this; }
    };
    void set_icountptr(int&) {}
    template <typename T>
    state_entry state_add(int, const char*, T&) {
        return state_entry();
    }

    virtual u64 execute_clocks_to_cycles(u64 clocks) const noexcept { return clocks; }
    virtual u64 execute_cycles_to_clocks(u64 cycles) const noexcept { return cycles; }
    virtual u32 execute_min_cycles() const noexcept { return 1; }
    virtual u32 execute_max_cycles() const noexcept { return 1; }
    virtual void execute_run() = 0;
    virtual void execute_set_input(int, int) {}
    virtual std::unique_ptr<util::disasm_interface> create_disassembler() = 0;
};
