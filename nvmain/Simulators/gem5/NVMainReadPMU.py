from m5.params import *
from m5.SimObject import *

class NVMainReadPMU(SimObject):
    type = 'NVMainReadPMU'
    cxx_class = 'Nvmain_Read_PMU'
    cxx_header = 'Simulators/gem5/nvmain_read_pmu.hh'