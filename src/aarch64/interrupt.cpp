/*
 * Interrupt Handling
 *
 * Copyright (C) 2019 Udo Steinberg, BedRock Systems, Inc.
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#include "assert.hpp"
#include "gicc.hpp"
#include "gicd.hpp"
#include "gicr.hpp"
#include "interrupt.hpp"
#include "sc.hpp"
#include "sm.hpp"
#include "smmu.hpp"
#include "stdio.hpp"
#include "timer.hpp"

Interrupt Interrupt::int_table[SPI_NUM];

void Interrupt::init()
{
    for (unsigned i = 0; i < SPI_NUM; i++)
        int_table[i].sm = Sm::create (0, i);
}

Event::Selector Interrupt::handle_sgi (uint32 val, bool)
{
    unsigned sgi = (val & 0x3ff) - SGI_BASE;

    assert (sgi < SGI_NUM);

    switch (sgi) {
        case Sgi::RRQ: Sc::rrq_handler(); break;
        case Sgi::RKE: break;
    }

    Gicc::eoi (val);
    Gicc::dir (val);

    return Event::Selector::NONE;
}

Event::Selector Interrupt::handle_ppi (uint32 val, bool vcpu)
{
    unsigned ppi = (val & 0x3ff) - PPI_BASE;

    assert (ppi < PPI_NUM);

    Event::Selector evt = Event::Selector::NONE;

    switch (ppi) {

        case HTIMER_PPI:
            Timer::interrupt();
            break;

        case VTIMER_PPI:
            if (vcpu)
                evt = Event::Selector::VTIMER;
            break;
    }

    Gicc::eoi (val);

    if (evt == Event::Selector::NONE)
        Gicc::dir (val);

    return evt;
}

Event::Selector Interrupt::handle_spi (uint32 val, bool)
{
    unsigned spi = (val & 0x3ff) - SPI_BASE;

    assert (spi < SPI_NUM);

    switch (spi) {

        case SMMU_SPI:
            Smmu::interrupt();
            Gicc::eoi (val);
            Gicc::dir (val);
            break;

        default:
            Gicc::eoi (val);

            if (!int_table[spi].gst)
                int_table[spi].dir = true;

            int_table[spi].sm->up();
            break;
    }

    return Event::Selector::NONE;
}

Event::Selector Interrupt::handler (bool vcpu)
{
    uint32 val = Gicc::ack(), i = val & 0x3ff;

    if (i < PPI_BASE)
        return handle_sgi (val, vcpu);

    if (i < SPI_BASE)
        return handle_ppi (val, vcpu);

    if (i < RSV_BASE)
        return handle_spi (val, vcpu);

    return Event::Selector::NONE;
}

void Interrupt::conf_sgi (unsigned sgi, bool msk)
{
    trace (TRACE_INTR, "INTR: %s: %u %c", __func__, sgi, msk ? 'M' : 'U');

    if (Gicd::arch < 3)
        Gicd::conf (sgi + SGI_BASE);
    else
        Gicr::conf (sgi + SGI_BASE);

    (Gicd::arch < 3 ? Gicd::mask : Gicr::mask) (sgi + SGI_BASE, msk);
}

void Interrupt::conf_ppi (unsigned ppi, bool msk, bool trg)
{
    trace (TRACE_INTR, "INTR: %s: %u %c%c", __func__, ppi, msk ? 'M' : 'U', trg ? 'E' : 'L');

    if (Gicd::arch < 3)
        Gicd::conf (ppi + PPI_BASE, trg);
    else
        Gicr::conf (ppi + PPI_BASE, trg);

    (Gicd::arch < 3 ? Gicd::mask : Gicr::mask) (ppi + PPI_BASE, msk);
}

void Interrupt::conf_spi (unsigned spi, unsigned cpu, bool msk, bool trg, bool gst)
{
    trace (TRACE_INTR, "INTR: %s: %u cpu=%u %c%c%c", __func__, spi, cpu, msk ? 'M' : 'U', trg ? 'E' : 'L', gst ? 'G' : 'H');

    int_table[spi].cpu  = static_cast<uint16>(cpu);
    int_table[spi].gst  = gst;

    Gicd::conf (spi + SPI_BASE, trg, cpu);
    Gicd::mask (spi + SPI_BASE, msk);
}

void Interrupt::send_sgi (Sgi sgi, unsigned cpu)
{
    (Gicd::arch < 3 ? Gicd::send_sgi : Gicc::send_sgi) (sgi, cpu);
}

void Interrupt::deactivate_spi (unsigned spi)
{
    if (int_table[spi].dir) {
        int_table[spi].dir = false;
        Gicc::dir (spi + SPI_BASE);
    }
}
