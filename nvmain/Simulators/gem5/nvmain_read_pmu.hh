#if TU_DORTMUND
#ifndef NVMAIN_READ_PMU
#define NVMAIN_READ_PMU

#include <sim/sim_object.hh>
#include "sim/probe/pmu.hh"

class Nvmain_Read_PMU : public gem5::SimObject {
   protected:
    /**
     * Add a PMU Probe which notifies about memory writes
     */
    gem5::probing::PMUUPtr ppMemBusReads;

   public:
    Nvmain_Read_PMU(const Params *p);
    void triggerRead();
    void regProbePoints();

    static Nvmain_Read_PMU *instance;
};

#endif
#endif