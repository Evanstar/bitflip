#if TU_DORTMUND
#include "nvmain_write_pmu.hh"
#include <params/NVMainWritePMU.hh>
#include <iostream>

Nvmain_Write_PMU *Nvmain_Write_PMU::instance=0;

Nvmain_Write_PMU::Nvmain_Write_PMU(const Params *p) : SimObject(*p) {
    instance = this;
}

void Nvmain_Write_PMU::regProbePoints(){
/**
     * Register the memory write pmu probe
     */
    ppMemBusWrites.reset(
        new gem5::probing::PMU(getProbeManager(), "MemBusWrites"));
}

void Nvmain_Write_PMU::triggerWrite() { 
    ppMemBusWrites->notify(1); }

Nvmain_Write_PMU *gem5::NVMainWritePMUParams::create() const {
    return new Nvmain_Write_PMU(this);
}
#endif