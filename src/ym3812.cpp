
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

#include "plus4emu.hpp"
#include "ym3812.hpp"

#include <vector>

namespace Plus4 {

  YM3812::YM3812Interface::YM3812Interface()
    : ymfm::ymfm_interface()
  {
    reset();
  }

  YM3812::YM3812Interface::~YM3812Interface()
  {
  }

  void YM3812::YM3812Interface::ymfm_set_timer(uint32_t tnum,
                                               int32_t duration_in_clocks)
  {
    if (tnum > 1U)
      return;
    if (duration_in_clocks < 0) {
      timerRunning[tnum] = false;
      timerCount[tnum] = 0;
    }
    else {
      timerRunning[tnum] = true;
      timerCount[tnum] = duration_in_clocks;
    }
  }

  void YM3812::YM3812Interface::advanceTimers(uint32_t clocks)
  {
    for (uint32_t tnum = 0; tnum < 2; tnum++) {
      if (!timerRunning[tnum])
        continue;
      timerCount[tnum] -= int32_t(clocks);
      // bounded loop: normally a timer period is much longer than a single
      // generate() call, so this fires at most once per call; the iteration
      // limit only guards against pathological (near-zero) timer periods
      int   iterations = 0;
      while (timerRunning[tnum] && timerCount[tnum] <= 0 && iterations < 8) {
        m_engine->engine_timer_expired(tnum);
        iterations++;
      }
    }
  }

  void YM3812::YM3812Interface::reset()
  {
    timerRunning[0] = false;
    timerRunning[1] = false;
    timerCount[0] = 0;
    timerCount[1] = 0;
  }

  void YM3812::YM3812Interface::saveState(Plus4Emu::File::Buffer& buf)
  {
    for (int i = 0; i < 2; i++) {
      buf.writeBoolean(timerRunning[i]);
      buf.writeInt32(timerCount[i]);
    }
  }

  void YM3812::YM3812Interface::loadState(Plus4Emu::File::Buffer& buf)
  {
    for (int i = 0; i < 2; i++) {
      timerRunning[i] = buf.readBoolean();
      timerCount[i] = buf.readInt32();
    }
  }

  // --------------------------------------------------------------------------

  YM3812::YM3812()
    : intf(),
      chip(intf),
      oplSampleRate(0),
      masterClocksPerSample(1),
      phase(0),
      phaseIncrement(0),
      currentOutput(0)
  {
    chip.reset();
    oplSampleRate = chip.sample_rate(inputClockFrequency);
    if (oplSampleRate < 1U)
      oplSampleRate = 1U;
    masterClocksPerSample = inputClockFrequency / oplSampleRate;
    if (masterClocksPerSample < 1U)
      masterClocksPerSample = 1U;
  }

  YM3812::~YM3812()
  {
  }

  uint8_t YM3812::read(uint16_t addr)
  {
    return chip.read(uint32_t(addr & 1U));
  }

  void YM3812::write(uint16_t addr, uint8_t value)
  {
    chip.write(uint32_t(addr & 1U), value);
  }

  void YM3812::reset()
  {
    chip.reset();
    intf.reset();
    phase = 0;
    currentOutput = 0;
  }

  void YM3812::setSoundClockFrequency(uint32_t soundClockFrequency_)
  {
    if (soundClockFrequency_ < 1U)
      return;
    phaseIncrement =
        uint32_t((uint64_t(oplSampleRate) << 16) / uint64_t(soundClockFrequency_));
  }

  int32_t YM3812::tick()
  {
    phase += phaseIncrement;
    while (phase >= 0x00010000U) {
      phase -= 0x00010000U;
      ymfm::ym3812::output_data  out;
      chip.generate(&out);
      intf.advanceTimers(masterClocksPerSample);
      int32_t   tmp = out.data[0];
      tmp = (tmp >= -32768 ? (tmp < 32767 ? tmp : 32767) : -32768);
      currentOutput = tmp;
    }
    return currentOutput;
  }

  // --------------------------------------------------------------------------

  class ChunkType_YM3812Snapshot : public Plus4Emu::File::ChunkTypeHandler {
   private:
    YM3812& ref;
   public:
    ChunkType_YM3812Snapshot(YM3812& ref_)
      : Plus4Emu::File::ChunkTypeHandler(),
        ref(ref_)
    {
    }
    virtual ~ChunkType_YM3812Snapshot()
    {
    }
    virtual Plus4Emu::File::ChunkType getChunkType() const
    {
      return Plus4Emu::File::PLUS4EMU_CHUNKTYPE_YM3812_STATE;
    }
    virtual void processChunk(Plus4Emu::File::Buffer& buf)
    {
      ref.loadState(buf);
    }
  };

  void YM3812::saveState(Plus4Emu::File::Buffer& buf)
  {
    buf.setPosition(0);
    buf.writeUInt32(0x01000000);        // version number
    std::vector<uint8_t>   stateData;
    {
      ymfm::ymfm_saved_state  state(stateData, true);
      chip.save_restore(state);
    }
    buf.writeUInt32(uint32_t(stateData.size()));
    for (size_t i = 0; i < stateData.size(); i++)
      buf.writeByte(stateData[i]);
    buf.writeUInt32(phase);
    buf.writeInt32(currentOutput);
    intf.saveState(buf);
  }

  void YM3812::saveState(Plus4Emu::File& f)
  {
    Plus4Emu::File::Buffer  buf;
    this->saveState(buf);
    f.addChunk(Plus4Emu::File::PLUS4EMU_CHUNKTYPE_YM3812_STATE, buf);
  }

  void YM3812::loadState(Plus4Emu::File::Buffer& buf)
  {
    buf.setPosition(0);
    // check version number
    unsigned int  version = buf.readUInt32();
    if (version != 0x01000000) {
      buf.setPosition(buf.getDataSize());
      throw Plus4Emu::Exception("incompatible YM3812 snapshot format");
    }
    chip.reset();
    intf.reset();
    phase = 0;
    currentOutput = 0;
    try {
      uint32_t  stateSize = buf.readUInt32();
      std::vector<uint8_t>  stateData(stateSize);
      for (uint32_t i = 0; i < stateSize; i++)
        stateData[i] = buf.readByte();
      {
        ymfm::ymfm_saved_state  state(stateData, false);
        chip.save_restore(state);
      }
      // per-operator data is cached (for performance) separately from the
      // register state restored above, and is not part of ymfm's own
      // save/restore mechanism, so it must be explicitly invalidated to
      // force it to be recomputed from the just-restored registers
      chip.invalidate_caches();
      phase = buf.readUInt32();
      currentOutput = buf.readInt32();
      intf.loadState(buf);
      if (buf.getPosition() != buf.getDataSize())
        throw Plus4Emu::Exception("trailing garbage at end of "
                                  "YM3812 snapshot data");
    }
    catch (...) {
      try {
        this->reset();
      }
      catch (...) {
      }
      throw;
    }
  }

  void YM3812::registerChunkType(Plus4Emu::File& f)
  {
    ChunkType_YM3812Snapshot  *p;
    p = new ChunkType_YM3812Snapshot(*this);
    try {
      f.registerChunkType(p);
    }
    catch (...) {
      delete p;
      throw;
    }
  }

}       // namespace Plus4

