/*
 * Copyright (c) 2013, 2015 ARM Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Giacomo Gabrielli
 *          Andreas Sandberg
 */

#include "dev/arm/generic_timer.hh"

#include "arch/arm/system.hh"
#include "debug/Timer.hh"
#include "dev/arm/base_gic.hh"
#include "mem/packet_access.hh"
#include "params/GenericTimer.hh"
#include "params/GenericTimerMem.hh"

SystemCounter::SystemCounter()
    : _freq(0), _period(0), _resetTick(0), _regCntkctl(0) {
  setFreq(0x01800000);
}

void SystemCounter::setFreq(uint32_t freq) {
  if (_freq != 0) {
    // Altering the frequency after boot shouldn't be done in practice.
    warn_once("The frequency of the system counter has already been set");
  }
  _freq = freq;
  _period = (1.0 / freq) * SimClock::Frequency;
  _resetTick = curTick();
}

void SystemCounter::serialize(std::ostream &os) const {
  SERIALIZE_SCALAR(_regCntkctl);
  SERIALIZE_SCALAR(_freq);
  SERIALIZE_SCALAR(_period);
  SERIALIZE_SCALAR(_resetTick);
}

void SystemCounter::unserialize(Checkpoint *cp, const std::string &section) {
  // We didn't handle CNTKCTL in this class before, assume it's zero
  // if it isn't present.
  if (!UNSERIALIZE_OPT_SCALAR(_regCntkctl))
    _regCntkctl = 0;
  UNSERIALIZE_SCALAR(_freq);
  UNSERIALIZE_SCALAR(_period);
  UNSERIALIZE_SCALAR(_resetTick);
}

ArchTimer::ArchTimer(const std::string &name, SimObject &parent,
                     SystemCounter &sysctr, const Interrupt &interrupt)
    : _name(name), _parent(parent), _systemCounter(sysctr),
      _interrupt(interrupt), _control(0), _counterLimit(0), _offset(0),
      _counterLimitReachedEvent(this) {}

void ArchTimer::counterLimitReached() {
  _control.istatus = 1;

  if (!_control.enable)
    return;

  DPRINTF(Timer, "Counter limit reached\n");
  if (!_control.imask) {
    DPRINTF(Timer, "Causing interrupt\n");
    _interrupt.send();
  }
}

void ArchTimer::updateCounter() {
  if (_counterLimitReachedEvent.scheduled())
    _parent.deschedule(_counterLimitReachedEvent);
  if (value() >= _counterLimit) {
    counterLimitReached();
  } else {
    const auto period(_systemCounter.period());
    _control.istatus = 0;
    _parent.schedule(_counterLimitReachedEvent,
                     curTick() + (_counterLimit - value()) * period);
  }
}

void ArchTimer::setCompareValue(uint64_t val) {
  _counterLimit = val;
  updateCounter();
}

void ArchTimer::setTimerValue(uint32_t val) {
  setCompareValue(value() + sext<32>(val));
}

void ArchTimer::setControl(uint32_t val) {
  ArchTimerCtrl new_ctl = val;
  if ((new_ctl.enable && !new_ctl.imask) &&
      !(_control.enable && !_control.imask)) {
    // Re-evalute the timer condition
    if (_counterLimit >= value()) {
      _control.istatus = 1;

      DPRINTF(Timer, "Causing interrupt in control\n");
      //_interrupt.send();
    }
  }
  _control.enable = new_ctl.enable;
  _control.imask = new_ctl.imask;
}

void ArchTimer::setOffset(uint64_t val) {
  _offset = val;
  updateCounter();
}

uint64_t ArchTimer::value() const { return _systemCounter.value() - _offset; }

void ArchTimer::serialize(std::ostream &os) const {
  paramOut(os, "control_serial", _control);
  SERIALIZE_SCALAR(_counterLimit);
  SERIALIZE_SCALAR(_offset);

  const bool event_scheduled(_counterLimitReachedEvent.scheduled());
  SERIALIZE_SCALAR(event_scheduled);
  if (event_scheduled) {
    const Tick event_time(_counterLimitReachedEvent.when());
    SERIALIZE_SCALAR(event_time);
  }
}

void ArchTimer::unserialize(Checkpoint *cp, const std::string &section) {
  paramIn(cp, section, "control_serial", _control);
  // We didn't serialize an offset before we added support for the
  // virtual timer. Consider it optional to maintain backwards
  // compatibility.
  if (!UNSERIALIZE_OPT_SCALAR(_offset))
    _offset = 0;
  bool event_scheduled;
  UNSERIALIZE_SCALAR(event_scheduled);
  if (event_scheduled) {
    Tick event_time;
    UNSERIALIZE_SCALAR(event_time);
    _parent.schedule(_counterLimitReachedEvent, event_time);
  }
}

void ArchTimer::Interrupt::send() {
  if (_ppi) {
    _gic.sendPPInt(_irq, _cpu);
  } else {
    _gic.sendInt(_irq);
  }
}

void ArchTimer::Interrupt::clear() {
  if (_ppi) {
    _gic.clearPPInt(_irq, _cpu);
  } else {
    _gic.clearInt(_irq);
  }
}

GenericTimer::GenericTimer(GenericTimerParams *p)
    : SimObject(p), gic(p->gic), irqPhys(p->int_phys), irqVirt(p->int_virt) {
  dynamic_cast<ArmSystem &>(*p->system).setGenericTimer(this);
}

void GenericTimer::serialize(std::ostream &os) {
  paramOut(os, "cpu_count", timers.size());

  nameOut(os, csprintf("%s.sys_counter", name()));
  systemCounter.serialize(os);

  for (int i = 0; i < timers.size(); ++i) {
    CoreTimers &core(getTimers(i));

    nameOut(os, core.phys.name());
    core.phys.serialize(os);

    nameOut(os, core.virt.name());
    core.virt.serialize(os);
  }
}

void GenericTimer::unserialize(Checkpoint *cp, const std::string &section) {
  systemCounter.unserialize(cp, csprintf("%s.sys_counter", section));

  // Try to unserialize the CPU count. Old versions of the timer
  // model assumed a 8 CPUs, so we fall back to that if the field
  // isn't present.
  static const unsigned OLD_CPU_MAX = 8;
  unsigned cpu_count;
  if (!UNSERIALIZE_OPT_SCALAR(cpu_count)) {
    warn("Checkpoint does not contain CPU count, assuming %i CPUs\n",
         OLD_CPU_MAX);
    cpu_count = OLD_CPU_MAX;
  }

  for (int i = 0; i < cpu_count; ++i) {
    CoreTimers &core(getTimers(i));
    // This should really be phys_timerN, but we are stuck with
    // arch_timer for backwards compatibility.
    core.phys.unserialize(cp, csprintf("%s.arch_timer%d", section, i));
    core.virt.unserialize(cp, csprintf("%s.virt_timer%d", section, i));
  }
}

GenericTimer::CoreTimers &GenericTimer::getTimers(int cpu_id) {
  if (cpu_id >= timers.size())
    createTimers(cpu_id + 1);

  return *timers[cpu_id];
}

void GenericTimer::createTimers(unsigned cpus) {
  assert(timers.size() < cpus);

  const unsigned old_cpu_count(timers.size());
  timers.resize(cpus);
  for (unsigned i = old_cpu_count; i < cpus; ++i) {
    timers[i].reset(new CoreTimers(*this, i, irqPhys, irqVirt));
  }
}

void GenericTimer::setMiscReg(int reg, unsigned cpu, MiscReg val) {
  CoreTimers &core(getTimers(cpu));

  switch (reg) {
  case MISCREG_CNTFRQ:
  case MISCREG_CNTFRQ_EL0:
    systemCounter.setFreq(val);
    return;

  case MISCREG_CNTKCTL:
  case MISCREG_CNTKCTL_EL1:
    systemCounter.setKernelControl(val);
    return;

  // Physical timer
  case MISCREG_CNTP_CVAL:
  case MISCREG_CNTP_CVAL_NS:
  case MISCREG_CNTP_CVAL_EL0:
    core.phys.setCompareValue(val);
    return;

  case MISCREG_CNTP_TVAL:
  case MISCREG_CNTP_TVAL_NS:
  case MISCREG_CNTP_TVAL_EL0:
    core.phys.setTimerValue(val);
    return;

  case MISCREG_CNTP_CTL:
  case MISCREG_CNTP_CTL_NS:
  case MISCREG_CNTP_CTL_EL0:
    core.phys.setControl(val);
    return;

  // Count registers
  case MISCREG_CNTPCT:
  case MISCREG_CNTPCT_EL0:
  case MISCREG_CNTVCT:
  case MISCREG_CNTVCT_EL0:
    warn("Ignoring write to read only count register: %s\n", miscRegName[reg]);
    return;

  // Virtual timer
  case MISCREG_CNTVOFF:
  case MISCREG_CNTVOFF_EL2:
    core.virt.setOffset(val);
    return;

  case MISCREG_CNTV_CVAL:
  case MISCREG_CNTV_CVAL_EL0:
    core.virt.setCompareValue(val);
    return;

  case MISCREG_CNTV_TVAL:
  case MISCREG_CNTV_TVAL_EL0:
    core.virt.setTimerValue(val);
    return;

  case MISCREG_CNTV_CTL:
  case MISCREG_CNTV_CTL_EL0:
    core.virt.setControl(val);
    return;

  // PL1 phys. timer, secure
  case MISCREG_CNTP_CTL_S:
  case MISCREG_CNTPS_CVAL_EL1:
  case MISCREG_CNTPS_TVAL_EL1:
  case MISCREG_CNTPS_CTL_EL1:
  /* FALLTHROUGH */

  // PL2 phys. timer, non-secure
  case MISCREG_CNTHCTL:
  case MISCREG_CNTHCTL_EL2:
  case MISCREG_CNTHP_CVAL:
  case MISCREG_CNTHP_CVAL_EL2:
  case MISCREG_CNTHP_TVAL:
  case MISCREG_CNTHP_TVAL_EL2:
  case MISCREG_CNTHP_CTL:
  case MISCREG_CNTHP_CTL_EL2:
    warn("Writing to unimplemented register: %s\n", miscRegName[reg]);
    return;

  default:
    warn("Writing to unknown register: %s\n", miscRegName[reg]);
    return;
  }
}

MiscReg GenericTimer::readMiscReg(int reg, unsigned cpu) {
  CoreTimers &core(getTimers(cpu));

  switch (reg) {
  case MISCREG_CNTFRQ:
  case MISCREG_CNTFRQ_EL0:
    return systemCounter.freq();

  case MISCREG_CNTKCTL:
  case MISCREG_CNTKCTL_EL1:
    return systemCounter.getKernelControl();

  // Physical timer
  case MISCREG_CNTP_CVAL:
  case MISCREG_CNTP_CVAL_EL0:
    return core.phys.compareValue();

  case MISCREG_CNTP_TVAL:
  case MISCREG_CNTP_TVAL_EL0:
    return core.phys.timerValue();

  case MISCREG_CNTP_CTL:
  case MISCREG_CNTP_CTL_EL0:
  case MISCREG_CNTP_CTL_NS:
    return core.phys.control();

  case MISCREG_CNTPCT:
  case MISCREG_CNTPCT_EL0:
    return core.phys.value();

  // Virtual timer
  case MISCREG_CNTVCT:
  case MISCREG_CNTVCT_EL0:
    return core.virt.value();

  case MISCREG_CNTVOFF:
  case MISCREG_CNTVOFF_EL2:
    return core.virt.offset();

  case MISCREG_CNTV_CVAL:
  case MISCREG_CNTV_CVAL_EL0:
    return core.virt.compareValue();

  case MISCREG_CNTV_TVAL:
  case MISCREG_CNTV_TVAL_EL0:
    return core.virt.timerValue();

  case MISCREG_CNTV_CTL:
  case MISCREG_CNTV_CTL_EL0:
    return core.virt.control();

  // PL1 phys. timer, secure
  case MISCREG_CNTP_CTL_S:
  case MISCREG_CNTPS_CVAL_EL1:
  case MISCREG_CNTPS_TVAL_EL1:
  case MISCREG_CNTPS_CTL_EL1:
  /* FALLTHROUGH */

  // PL2 phys. timer, non-secure
  case MISCREG_CNTHCTL:
  case MISCREG_CNTHCTL_EL2:
  case MISCREG_CNTHP_CVAL:
  case MISCREG_CNTHP_CVAL_EL2:
  case MISCREG_CNTHP_TVAL:
  case MISCREG_CNTHP_TVAL_EL2:
  case MISCREG_CNTHP_CTL:
  case MISCREG_CNTHP_CTL_EL2:
    warn("Reading from unimplemented register: %s\n", miscRegName[reg]);
    return 0;

  default:
    warn("Reading from unknown register: %s\n", miscRegName[reg]);
    return 0;
  }
}

GenericTimerMem::GenericTimerMem(GenericTimerMemParams *p)
    : PioDevice(p), ctrlRange(RangeSize(p->base, TheISA::PageBytes)),
      timerRange(RangeSize(p->base + TheISA::PageBytes, TheISA::PageBytes)),
      addrRanges{ ctrlRange, timerRange }, systemCounter(),
      physTimer(csprintf("%s.phys_timer0", name()), *this, systemCounter,
                ArchTimer::Interrupt(*p->gic, p->int_phys)),
      virtTimer(csprintf("%s.virt_timer0", name()), *this, systemCounter,
                ArchTimer::Interrupt(*p->gic, p->int_virt)) {}

void GenericTimerMem::serialize(std::ostream &os) {
  paramOut(os, "timer_count", 1);

  nameOut(os, csprintf("%s.sys_counter", name()));
  systemCounter.serialize(os);

  nameOut(os, physTimer.name());
  physTimer.serialize(os);

  nameOut(os, virtTimer.name());
  virtTimer.serialize(os);
}

void GenericTimerMem::unserialize(Checkpoint *cp, const std::string &section) {
  systemCounter.unserialize(cp, csprintf("%s.sys_counter", section));

  unsigned timer_count;
  UNSERIALIZE_SCALAR(timer_count);
  // The timer count variable is just here for future versions where
  // we support more than one set of timers.
  if (timer_count != 1)
    panic("Incompatible checkpoint: Only one set of timers supported");

  physTimer.unserialize(cp, csprintf("%s.phys_timer0", section));
  virtTimer.unserialize(cp, csprintf("%s.virt_timer0", section));
}

Tick GenericTimerMem::read(PacketPtr pkt) {
  const unsigned size(pkt->getSize());
  const Addr addr(pkt->getAddr());
  uint64_t value;

  pkt->makeResponse();
  if (ctrlRange.contains(addr)) {
    value = ctrlRead(addr - ctrlRange.start(), size);
  } else if (timerRange.contains(addr)) {
    value = timerRead(addr - timerRange.start(), size);
  } else {
    panic("Invalid address: 0x%x\n", addr);
  }

  DPRINTF(Timer, "Read 0x%x <- 0x%x(%i)\n", value, addr, size);

  if (size == 8) {
    pkt->set<uint64_t>(value);
  } else if (size == 4) {
    pkt->set<uint32_t>(value);
  } else {
    panic("Unexpected access size: %i\n", size);
  }

  return 0;
}

Tick GenericTimerMem::write(PacketPtr pkt) {
  const unsigned size(pkt->getSize());
  if (size != 8 && size != 4)
    panic("Unexpected access size\n");

  const Addr addr(pkt->getAddr());
  const uint64_t value(size == 8 ? pkt->get<uint64_t>() : pkt->get<uint32_t>());

  DPRINTF(Timer, "Write 0x%x -> 0x%x(%i)\n", value, addr, size);
  if (ctrlRange.contains(addr)) {
    ctrlWrite(addr - ctrlRange.start(), size, value);
  } else if (timerRange.contains(addr)) {
    timerWrite(addr - timerRange.start(), size, value);
  } else {
    panic("Invalid address: 0x%x\n", addr);
  }

  pkt->makeResponse();
  return 0;
}

uint64_t GenericTimerMem::ctrlRead(Addr addr, size_t size) const {
  if (size == 4) {
    switch (addr) {
    case CTRL_CNTFRQ:
      return systemCounter.freq();

    case CTRL_CNTTIDR:
      return 0x3; // Frame 0 implemented with virtual timers

    case CTRL_CNTNSAR:
    case CTRL_CNTACR_BASE:
      warn("Reading from unimplemented control register (0x%x)\n", addr);
      return 0;

    case CTRL_CNTVOFF_LO_BASE:
      return virtTimer.offset();

    case CTRL_CNTVOFF_HI_BASE:
      return virtTimer.offset() >> 32;

    default:
      warn("Unexpected address (0x%x:%i), assuming RAZ\n", addr, size);
      return 0;
    }
  } else if (size == 8) {
    switch (addr) {
    case CTRL_CNTVOFF_LO_BASE:
      return virtTimer.offset();

    default:
      warn("Unexpected address (0x%x:%i), assuming RAZ\n", addr, size);
      return 0;
    }
  } else {
    panic("Invalid access size: %i\n", size);
  }
}

void GenericTimerMem::ctrlWrite(Addr addr, size_t size, uint64_t value) {
  if (size == 4) {
    switch (addr) {
    case CTRL_CNTFRQ:
    case CTRL_CNTNSAR:
    case CTRL_CNTTIDR:
    case CTRL_CNTACR_BASE:
      warn("Write to unimplemented control register (0x%x)\n", addr);
      return;

    case CTRL_CNTVOFF_LO_BASE:
      virtTimer.setOffset(insertBits(virtTimer.offset(), 31, 0, value));
      return;

    case CTRL_CNTVOFF_HI_BASE:
      virtTimer.setOffset(insertBits(virtTimer.offset(), 63, 32, value));
      return;

    default:
      warn("Ignoring write to unexpected address (0x%x:%i)\n", addr, size);
      return;
    }
  } else if (size == 8) {
    switch (addr) {
    case CTRL_CNTVOFF_LO_BASE:
      virtTimer.setOffset(value);
      return;

    default:
      warn("Ignoring write to unexpected address (0x%x:%i)\n", addr, size);
      return;
    }
  } else {
    panic("Invalid access size: %i\n", size);
  }
}

uint64_t GenericTimerMem::timerRead(Addr addr, size_t size) const {
  if (size == 4) {
    switch (addr) {
    case TIMER_CNTPCT_LO:
      return physTimer.value();

    case TIMER_CNTPCT_HI:
      return physTimer.value() >> 32;

    case TIMER_CNTVCT_LO:
      return virtTimer.value();

    case TIMER_CNTVCT_HI:
      return virtTimer.value() >> 32;

    case TIMER_CNTFRQ:
      return systemCounter.freq();

    case TIMER_CNTEL0ACR:
      warn("Read from unimplemented timer register (0x%x)\n", addr);
      return 0;

    case CTRL_CNTVOFF_LO_BASE:
      return virtTimer.offset();

    case CTRL_CNTVOFF_HI_BASE:
      return virtTimer.offset() >> 32;

    case TIMER_CNTP_CVAL_LO:
      return physTimer.compareValue();

    case TIMER_CNTP_CVAL_HI:
      return physTimer.compareValue() >> 32;

    case TIMER_CNTP_TVAL:
      return physTimer.timerValue();

    case TIMER_CNTP_CTL:
      return physTimer.control();

    case TIMER_CNTV_CVAL_LO:
      return virtTimer.compareValue();

    case TIMER_CNTV_CVAL_HI:
      return virtTimer.compareValue() >> 32;

    case TIMER_CNTV_TVAL:
      return virtTimer.timerValue();

    case TIMER_CNTV_CTL:
      return virtTimer.control();

    default:
      warn("Unexpected address (0x%x:%i), assuming RAZ\n", addr, size);
      return 0;
    }
  } else if (size == 8) {
    switch (addr) {
    case TIMER_CNTPCT_LO:
      return physTimer.value();

    case TIMER_CNTVCT_LO:
      return virtTimer.value();

    case CTRL_CNTVOFF_LO_BASE:
      return virtTimer.offset();

    case TIMER_CNTP_CVAL_LO:
      return physTimer.compareValue();

    case TIMER_CNTV_CVAL_LO:
      return virtTimer.compareValue();

    default:
      warn("Unexpected address (0x%x:%i), assuming RAZ\n", addr, size);
      return 0;
    }
  } else {
    panic("Invalid access size: %i\n", size);
  }
}

void GenericTimerMem::timerWrite(Addr addr, size_t size, uint64_t value) {
  if (size == 4) {
    switch (addr) {
    case TIMER_CNTEL0ACR:
      warn("Unimplemented timer register (0x%x)\n", addr);
      return;

    case TIMER_CNTP_CVAL_LO:
      physTimer.setCompareValue(
          insertBits(physTimer.compareValue(), 31, 0, value));
      return;

    case TIMER_CNTP_CVAL_HI:
      physTimer.setCompareValue(
          insertBits(physTimer.compareValue(), 63, 32, value));
      return;

    case TIMER_CNTP_TVAL:
      physTimer.setTimerValue(value);
      return;

    case TIMER_CNTP_CTL:
      physTimer.setControl(value);
      return;

    case TIMER_CNTV_CVAL_LO:
      virtTimer.setCompareValue(
          insertBits(virtTimer.compareValue(), 31, 0, value));
      return;

    case TIMER_CNTV_CVAL_HI:
      virtTimer.setCompareValue(
          insertBits(virtTimer.compareValue(), 63, 32, value));
      return;

    case TIMER_CNTV_TVAL:
      virtTimer.setTimerValue(value);
      return;

    case TIMER_CNTV_CTL:
      virtTimer.setControl(value);
      return;

    default:
      warn("Unexpected address (0x%x:%i), ignoring write\n", addr, size);
      return;
    }
  } else if (size == 8) {
    switch (addr) {
    case TIMER_CNTP_CVAL_LO:
      return physTimer.setCompareValue(value);

    case TIMER_CNTV_CVAL_LO:
      return virtTimer.setCompareValue(value);

    default:
      warn("Unexpected address (0x%x:%i), ignoring write\n", addr, size);
      return;
    }
  } else {
    panic("Invalid access size: %i\n", size);
  }
}

GenericTimer *GenericTimerParams::create() { return new GenericTimer(this); }

GenericTimerMem *GenericTimerMemParams::create() {
  return new GenericTimerMem(this);
}
