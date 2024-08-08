from m5.params import *
from m5.SimObject import *


class RevizorIPC(SimObject):
    type = "RevizorIPC"
    cxx_header = "revizor/revizor_ipc.hh"
    cxx_class = "gem5::RevizorIPC"
    cxx_exports = [PyBindMethod("prepareNext")]
    l1d_cache = Param.InspectableCache("L1 data cache")
    l1i_cache = Param.InspectableCache("L1 instruction cache")
    l2_cache = Param.InspectableCache("L2 cache")
    cpu = Param.BaseCPU("CPU")
    dram = Param.AbstractMemory("DRAM")
    process = Param.Process("Process")
    executable_path = Param.String("path to base executable")
    socket_name = Param.String("name of UNIX abstract domain socket for communication with Revizor (not including initial nul byte)")
