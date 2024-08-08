#ifndef REVIZOR_IPC_HH_
#define REVIZOR_IPC_HH_

#include "params/RevizorIPC.hh"
#include "sim/sim_object.hh"
#include "mem/abstract_mem.hh"
#include <unordered_map>

namespace gem5
{

class RevizorIPC : public SimObject
{
  public:
    RevizorIPC(RevizorIPCParams *p);
    ~RevizorIPC();
    void startup() override;
    bool prepareNext();
  private:
    uint8_t *vaddrToHost(Addr vaddr);
    void recv(void *buf, size_t count);
    void send(const void *buf, size_t count);
    void loadTestCase();
    void traceTestCase();
    void analyze();
    int sock = -1;
    bool loadedTestCase = false;
    uint64_t inputId = 0;
    InspectableCache *l1dCache, *l1iCache, *l2Cache;
    BaseCPU *cpu;
    AbstractMemory *dram;
    Process *process;
    std::unordered_map<std::string, uint64_t> addresses;
    uint64_t inputHash = 0, inputIndex = 0;
};

} // namespace gem5

#endif // REVIZOR_IPC_HH_
