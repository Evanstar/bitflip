#if HYBRID_CACHE == 1
#ifndef __MEM_CACHE_HYBRID_CACHE_BLK_HH__
#define __MEM_CACHE_HYBRID_CACHE_BLK_HH__

#include <cassert>
#include <cstdint>
#include <iosfwd>
#include <list>
#include <string>

#include "base/printable.hh"
#include "base/types.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/cache/tags/tagged_entry.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"

namespace gem5
{
class HybridCacheBlk : public CacheBlk
{
  private:

    bool  _isVolatile;

  public:

    HybridCacheBlk() : CacheBlk()
    {
        _isVolatile = true;
    }

    /**
     * Return whether this cache block is located within the volatile
     * or non-volatile part of the cache
     */
    bool isVolatile() const {
        return _isVolatile;
    }

    /**
     * @param isVolatile Indication whether this cache block is located
     * within the volatile or non-volatile part of the cache.
     */
    void setVolatile(bool isVolatile) {
        _isVolatile = isVolatile;
    }
};

/**
 * Simple class to provide virtual print() method on cache blocks
 * without allocating a vtable pointer for every single cache block.
 * Just wrap the CacheBlk object in an instance of this before passing
 * to a function that requires a Printable object.
 */
class HybridCacheBlkPrintWrapper : public Printable
{
    HybridCacheBlk *blk;
  public:
    HybridCacheBlkPrintWrapper(HybridCacheBlk *_blk) : blk(_blk) {}
    virtual ~HybridCacheBlkPrintWrapper() {}
    void print(std::ostream &o, int verbosity = 0,
               const std::string &prefix = "") const;
};

} // namespace gem5

#endif //__MEM_CACHE_HYBRID_CACHE_BLK_HH__
#endif
