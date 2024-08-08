from m5.objects import *
import math
import os
import argparse

ARCH = "arm"

parser = argparse.ArgumentParser(
    prog="gem5 Revizor server",
    description="Remote execution server for the Revizor fuzzer",
)
parser.add_argument(
    "--socket-name",
    type=str,
    help="which port to use for communication",
    required=True
)
args = parser.parse_args()
socket_name = args.socket_name


def is_newer(file1, file2, help_if_file1_doesnt_exist=""):
    try:
        t1 = os.path.getmtime(file1)
    except FileNotFoundError:
        raise FileNotFoundError(
            f"""expected {file1} to exist but it doesn't
            {help_if_file1_doesnt_exist}"""
        )
    try:
        t2 = os.path.getmtime(file2)
    except FileNotFoundError:
        return True
    return t1 > t2


def print_and_execute(cmd):
    print(cmd)
    os.system(cmd)


if ARCH == "arm":
    base_source_path = "src/revizor/base-arm.s"
    base_object_path = "build/ARM/revizor/base.o"
    m5_library_path = "util/m5/build/arm64/out/libm5.a"
    assembler = "aarch64-linux-gnu-as"
    linker = "aarch64-linux-gnu-ld"
    executable_path = "build/ARM/revizor/base"
else:
    raise NotImplementedError(f"Unsupported architecture: {ARCH}")

if is_newer(
    base_source_path,
    base_object_path,
    "make sure you run this from the root gem5 directory",
):
    print_and_execute(
        f"{assembler} -c {base_source_path} -o {base_object_path}"
    )
if is_newer(
    m5_library_path,
    executable_path,
    f"see https://www.gem5.org/documentation/general_docs/m5ops/ for build instructions",
) or is_newer(base_object_path, executable_path):
    print_and_execute(
        f"{linker} {base_object_path} {m5_library_path} -o {executable_path}"
    )


class L1Cache(InspectableCache):
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20


class L1ICache(L1Cache):
    size = "16kB"


class L1DCache(L1Cache):
    size = "64kB"


class L2Cache(InspectableCache):
    size = "256kB"
    assoc = 8
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 20
    tgts_per_mshr = 12


# set up system
system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MB")]
system.membus = SystemXBar()
system.cpu = O3CPU()
l1i_cache = L1ICache()
l1d_cache = L1DCache()
system.cpu.icache = l1i_cache
system.cpu.dcache = l1d_cache
l1i_cache.cpu_side = system.cpu.icache_port
l1d_cache.cpu_side = system.cpu.dcache_port
system.l2bus = L2XBar()
l1i_cache.mem_side = system.l2bus.cpu_side_ports
l1d_cache.mem_side = system.l2bus.cpu_side_ports
l2_cache = L2Cache()
system.l2cache = l2_cache
system.l2cache.cpu_side = system.l2bus.mem_side_ports
system.membus = SystemXBar()
system.l2cache.mem_side = system.membus.cpu_side_ports
system.cpu.createInterruptController()
system.system_port = system.membus.cpu_side_ports
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# create process to run executable
system.workload = SEWorkload.init_compatible(executable_path)
process = Process()
process.cmd = [executable_path]
system.cpu.workload = process
system.cpu.createThreads()

# start simulation
ipc = RevizorIPC(
    l1d_cache=l1d_cache,
    l1i_cache=l1i_cache,
    l2_cache=l2_cache,
    cpu=system.cpu,
    dram=system.mem_ctrl.dram,
    process=process,
    executable_path=executable_path,
    socket_name=socket_name,
)
root = Root(full_system=False, system=system, ipc=ipc)
m5.instantiate()
m5.simulate()
while root.ipc.prepareNext():
    m5.simulate()
