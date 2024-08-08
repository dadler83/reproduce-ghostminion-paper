#ifndef INSPECTABLE_CACHE_HH_
#define INSPECTABLE_CACHE_HH_

#include "mem/cache/cache.hh"
#include "params/InspectableCache.hh"
#include "sim/sim_object.hh"

namespace gem5 {

class InspectableCache : public Cache
{
  public:
    InspectableCache(InspectableCacheParams *params);
    void inspect();
    // write dirty blocks back to memory
    void writeback();
    void invalidate();
    std::vector<bool> validSet() const;
    void debug();
};

} // namespace gem5

#endif // INSPECTABLE_CACHE_HH_
