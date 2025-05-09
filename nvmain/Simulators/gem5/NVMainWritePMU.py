from m5.params import *
from m5.SimObject import *

class NVMainWritePMU(SimObject):
    type = 'NVMainWritePMU'
    cxx_class = 'Nvmain_Write_PMU'
    cxx_header = 'Simulators/gem5/nvmain_write_pmu.hh'