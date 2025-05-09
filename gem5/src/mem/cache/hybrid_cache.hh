#if HYBRID_CACHE == 1
/**
 * @file
 * Describes a hybrid cache with both volatile and non-volatile cache blocks
 */

#ifndef __MEM_HYBRID_CACHE_HH__
#define __MEM_HYBRID_CACHE_HH__

#include <cstdint>
#include <unordered_set>

#include "base/compiler.hh"
#include "base/types.hh"
#include "mem/cache/base.hh"
#include "mem/cache/cache.hh"
#include "mem/cache/tags/base.hh"
#include "mem/cache/tags/hybrid_set_assoc.hh"
#include "mem/packet.hh"

namespace gem5
{

class CacheBlk;
class HybridCacheBlk;
struct HybridCacheParams;
class MSHR;

/**
 * A coherent cache that can be arranged in flexible topologies.
 */
class HybridCache : public Cache
{
  protected:
    bool access(PacketPtr pkt, CacheBlk *&blk, Cycles &lat,
                PacketList &writebacks) override;

    void satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                        bool deferred_response = false,
                        bool pending_downgrade = false) override;
    CacheBlk *handleFill(PacketPtr pkt, CacheBlk *blk,
                         PacketList &writebacks, bool allocate);

    Cycles handleAtomicReqMiss(PacketPtr pkt, CacheBlk *&blk,
                           PacketList &writebacks) override;
    void recvTimingResp(PacketPtr pkt) override;

    /**
     * Allocate a new block and perform any necessary writebacks
     *
     * Find a victim block and if necessary prepare writebacks for any
     * existing data. May return nullptr if there are no replaceable
     * blocks. If a replaceable block is found, it inserts the new block in
     * its place. The new block, however, is not set as valid yet.
     *
     * @param pkt Packet holding the address to update
     * @param writebacks A list of writeback packets for the evicted blocks
     * @return the allocated block
     */
    CacheBlk *allocateBlock(const PacketPtr pkt, PacketList &writebacks);

    /**
     * Calculate access latency in ticks given a tag lookup latency
     * the type of access, and whether access was a hit or miss.
     *
     * @param pkt The memory request to perform.
     * @param blk The cache block that was accessed.
     * @return The number of ticks that pass due to a block access.
     */
    Cycles calculateAccessLatency(PacketPtr pkt,
        const HybridCacheBlk* blk) const;

    /**
     * The latency of data read access of a cache. It occurs when there is
     * an access reading from the non-volatile cache.
     */
    const Cycles dataReadLatency;

    /**
     * The latency of data write access of a cache. It occurs when there is
     * an access writing to the non-volatile cache.
     */
    const Cycles dataWriteLatency;

    /**
     * Energy consumption in nJ per access depending on the type of access
     * and the cache section that is accessed
     */
    const float volReadEnergy;
    const float nonVolReadEnergy;
    const float volWriteEnergy;
    const float nonVolWriteEnergy;

  public:
    /** Instantiates a basic cache object. */
    HybridCache(const HybridCacheParams &p);

    struct HybridCacheStats : public statistics::Group
    {
        HybridCacheStats(HybridCache &c);
        statistics::Scalar nonVolReads;
        statistics::Scalar volReads;
        statistics::Scalar nonVolWrites;
        statistics::Scalar volWrites;
        statistics::Scalar noOfNonVolReads;
        statistics::Scalar noOfVolReads;
        statistics::Scalar noOfNonVolWrites;
        statistics::Scalar noOfVolWrites;
        statistics::Scalar dynEnergy;
        statistics::Scalar tmpBlockUsages;
    } hybrid_stats;

    /**
     * Interface to allow for the MSHR queue to drain in case of
     * a power outage.
     * @param writebacks Return a list of packets with writebacks
     * @return True if the MSHR Queue has been drained
     */
    bool awaitMSHRDrain();

    /**
     * Collect the data to be written back to
     * non-volatile memory sections in the by-reference parameter.
     * @param writebacks Return a list of packets with writebacks
     */

    void collectWritebacks(PacketList &writebacks);

    /**
     * Interface to attempt to store as many writebacks as possible
     * in the write buffer in case of a power outage.
     * @param writebacks List of packets with outstanding writebacks
     * @return True if the writebacks have all succesfully been
     * stored in the write buffer, false if the write buffer is currently full
     */
    bool tryWritebacks(PacketList &writebacks);

     /**
     * Interface to allow for the write buffer to drain
     * in case of a power outage.
     * @return True if the write buffer has been drained
     */
    bool awaitWriteBufferDrain();

};

} // namespace gem5

#endif // __MEM_HYBRID_CACHE_HH__
#endif
