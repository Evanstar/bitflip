from m5.params import *
from m5.SimObject import SimObject


class CimOperationInterface(SimObject):
    type = "CimOperationInterface"
    cxx_header = "mem/CDNCcim/cim_operation_interface.hh"
    cxx_class = "gem5::memory::CimOperationInterface"


class CimFaultInjection(CimOperationInterface):
    type = "CimFaultInjection"
    cxx_header = "mem/CDNCcim/cim_fault_injection.hh"
    cxx_class = "gem5::memory::CimFaultInjection"