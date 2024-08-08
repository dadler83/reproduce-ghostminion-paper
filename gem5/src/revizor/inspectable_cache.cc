#include "revizor/inspectable_cache.hh"

#include <iomanip>

#include "debug/RevizorResults.hh"

namespace gem5 {

InspectableCache::InspectableCache(InspectableCacheParams *params)
    : Cache(params) {
}

void InspectableCache::inspect() {
    tags->forEachBlk([](CacheBlk &blk) {
        printf("%lu\n", blk.tag);
    });
}

void InspectableCache::writeback() {
    memWriteback();
}

void InspectableCache::invalidate() {
    memInvalidate();
}

std::vector<bool> InspectableCache::validSet() const {
    std::vector<bool> set = {};
    tags->forEachBlk([&](CacheBlk &blk) {
        set.push_back(blk.isValid());
    });
    return set;
}

void InspectableCache::debug() {
    DPRINTF(RevizorResults, "valid cache blocks:\n");
    int i = 0;
    tags->forEachBlk([&](CacheBlk &blk) {
        if (blk.isValid()) {
            std::string s = blk.print();
            DPRINTF(RevizorResults, "  %d. %s\n", i, s.c_str());
        }
        ++i;
    });
}

} // namespace gem5


gem5::InspectableCache *InspectableCacheParams::create()
{
    return new gem5::InspectableCache(this);
}
