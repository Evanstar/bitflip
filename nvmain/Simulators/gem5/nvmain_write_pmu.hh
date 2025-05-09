#if TU_DORTMUND
#ifndef NVMAIN_WRITE_PMU
#define NVMAIN_WRITE_PMU

#include <sim/sim_object.hh>
#include "sim/probe/pmu.hh"

class Nvmain_Write_PMU : public gem5::SimObject {
   protected:
    /**
     * Add a PMU Probe which notifies about memory writes
     */
    gem5::probing::PMUUPtr ppMemBusWrites;

   public:
    Nvmain_Write_PMU(const Params *p);
    void triggerWrite();
    void regProbePoints();

    static Nvmain_Write_PMU *instance;
};

#endif
#endif