#if HYBRID_CACHE == 1
#include "mem/cache/hybrid_cache_blk.hh"

#include "base/cprintf.hh"

namespace gem5
{

void
HybridCacheBlkPrintWrapper::print(std::ostream &os, int verbosity,
                            const std::string &prefix) const
{
    ccprintf(os, "%sblk %c%c%c%c\n", prefix,
             blk->isVolatile() ? "Vol" : "nVol",
             blk->isValid()    ? 'V' : '-',
             blk->isSet(CacheBlk::WritableBit) ? 'E' : '-',
             blk->isSet(CacheBlk::DirtyBit)    ? 'M' : '-',
             blk->isSecure()   ? 'S' : '-');
}

} // namespace gem5
#endif
