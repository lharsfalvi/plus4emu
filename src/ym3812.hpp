
// plus4emu -- portable Commodore Plus/4 emulator
// Copyright (C) 2003-2017 Istvan Varga <istvanv@users.sourceforge.net>
// https://github.com/istvan-v/plus4emu/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Emulation of a YM3812 (OPL2) FM synthesizer chip, using Aaron Giles' ymfm
// library. This models the register layout of the SukkoPera "SoundX" sound
// card for the Commodore 16/116/Plus4 (FM part only; the MIDI/ACIA part of
// SoundX is out of scope), which follows the classic AdLib port layout:
// the OPL2 address/status register is exposed at one address, and the data
// register at the following address, so that existing AdLib-compatible
// software and detection routines work unmodified.

#ifndef PLUS4EMU_YM3812_HPP
#define PLUS4EMU_YM3812_HPP

#include "plus4emu.hpp"
#include "fileio.hpp"
#include "ymfm/ymfm_opl.h"

namespace Plus4 {

  class YM3812 {
   private:
    // Bridges ymfm's engine callbacks to plus4emu's synchronous, sample
    // clocked execution model. The OPL2 has no exposed busy signal (see
    // ymfm_opl.cpp's write_address()/write_data() comments), and its status
    // register does not report a busy bit, so only timer scheduling needs to
    // be implemented here; IRQ, external RAM/ADPCM, and busy tracking are not
    // used by the OPL2 and are left at their default (no-op) implementations.
    class YM3812Interface : public ymfm::ymfm_interface {
     public:
      YM3812Interface();
      virtual ~YM3812Interface();
      virtual void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks);
      // called once per generate() call with the number of master (OPL)
      // clock cycles that elapsed; fires expired timers via the inherited
      // engine callback interface
      void advanceTimers(uint32_t clocks);
      void reset();
      void saveState(Plus4Emu::File::Buffer&);
      void loadState(Plus4Emu::File::Buffer&);
     private:
      bool      timerRunning[2];
      int32_t   timerCount[2];
    };
    // ----------------
    YM3812Interface   intf;
    ymfm::ym3812      chip;
    // the chip's own native sample rate (Hz) for inputClockFrequency, as
    // computed by ymfm; fixed for the lifetime of the object
    uint32_t          oplSampleRate;
    // number of OPL master clock cycles per generate() call (i.e. per output
    // sample of the chip itself, at its own native ~49.7 kHz sample rate)
    uint32_t          masterClocksPerSample;
    // fixed point (16.16) phase accumulator and increment, used to decide
    // when to call generate() again while being clocked once per TED sound
    // tick (which runs at a higher rate than the OPL2's own sample rate)
    uint32_t          phase;
    uint32_t          phaseIncrement;
    // most recently generated output sample, held (zero order hold) until
    // the phase accumulator triggers the next generate() call, mirroring
    // the real chip's external DAC which only updates at its own rate
    int32_t           currentOutput;
   public:
    // fixed OPL clock frequency (Hz), matching the crystal oscillator used
    // on the SoundX board (also the standard AdLib/Sound Blaster clock, so
    // existing OPL2 software's pitch/timing calculations remain correct)
    static const uint32_t  inputClockFrequency = 3579545;

    YM3812();
    ~YM3812();

    // Read/write a register through the AdLib-style 2 address port layout:
    // 'addr' & 1 == 0 selects the address/status port, == 1 the data port
    // (matches ymfm::ym3812::read()/write(), and SoundX's $FDE4/$FDE5 map).
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t value);

    void reset();

    // Set the rate (in Hz) at which tick() will be called (this should be
    // Plus4VM's soundClockFrequency, i.e. TED's own sound sample tick rate);
    // recomputes the phase increment. Must be called at least once with a
    // nonzero value before the first call to tick().
    void setSoundClockFrequency(uint32_t soundClockFrequency_);

    // Advance by one TED sound tick, and return the current (held) output
    // sample, scaled to roughly the same range as a 16 bit PCM sample.
    int32_t tick();

    void saveState(Plus4Emu::File::Buffer&);
    void saveState(Plus4Emu::File&);
    void loadState(Plus4Emu::File::Buffer&);
    void registerChunkType(Plus4Emu::File&);
  };

}       // namespace Plus4

#endif  // PLUS4EMU_YM3812_HPP

