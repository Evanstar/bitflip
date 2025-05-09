#if TU_DORTMUND
#include "nvmain_read_pmu.hh"

#include <iostream>
#include <params/NVMainReadPMU.hh>

Nvmain_Read_PMU *Nvmain_Read_PMU::instance = 0;

Nvmain_Read_PMU::Nvmain_Read_PMU(const Params *p) : SimObject(*p) {
    instance = this;
}

void Nvmain_Read_PMU::regProbePoints() {
    /**
     * Register the memory write pmu probe
     */
    ppMemBusReads.reset(new gem5::probing::PMU(getProbeManager(), "MemBusReads"));
}

void Nvmain_Read_PMU::triggerRead() {
    ppMemBusReads->notify(1);
}

Nvmain_Read_PMU *gem5::NVMainReadPMUParams::create() const {
    return new Nvmain_Read_PMU(this);
}
#endif