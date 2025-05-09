#if HYBRID_CACHE == 1
/**
 * @file
 * Declaration of a replacement policy featuring confidence values
 * and on-the-fly migration in order to i.e. save important data
 * during power outages
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_CM_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_CM_RP_HH__

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "base/types.hh"
#include "mem/cache/hybrid_cache_blk.hh"
#include "mem/cache/replacement_policies/base.hh"

namespace gem5
{

struct CMRPParams;

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

class CM : public Base
{
  protected:

    /**
     * Table containing the previous region for a
     * parameterisable number of entries.
     * True if previous region was the volatile cache section
     * False if otherwise
     */
    std::vector<bool> prVolTable;
    /** Number of entries in the previous region table */
    const unsigned noOfTableEntries;
    /** Number of bits for addresses accessing the pr table */
    const unsigned noOfTableAddrBits;
    /** Number of bits for accesses to the same cache block. */
    const unsigned noOfByteAddrBits;

    /**
     * Thresholds determining how many read/write accesses are to be
     * performed before a cache block is either migrated or incremented in
     * its confidence value depending on its cache section.
     */
    int readThreshold;

    int writeThreshold;

    bool hashPCforPredictionTable;

    /** CM-specific implementation of replacement data. */
    struct CMReplData : ReplacementData
    {
        /** Address that initially led to the cache block fill */
        Addr addr;

        /**
         * Write intensity of accesses to the cache block
         * incremented on write
         */
        int wiCounter;

        /**
         * Read intensity of accesses to the cache block
         * incremented on read
         */
        int riCounter;

        /**
         * Confidence value incremented after the number of
         * "correct accesses", as determined by the cache section
         * this cache block belongs to, has hit the threshold
         * (does not exceed above a value of 3)
         */
        short conf;
        /**
         * Default constructor. Invalidate data.
         */
        CMReplData() : addr(0), wiCounter(-1), riCounter(-1), conf(-1) {}
    };

  public:
    typedef CMRPParams Params;
    CM(const Params &p);
    ~CM() = default;

    /**
     * Invalidate replacement data to set it as the next probable victim.
     * Sets its last touch tick as the starting tick.
     *
     * @param replacement_data Replacement data to be invalidated.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
                                                                    override;

    /**
     * Touch an entry to update its replacement data.
     * Updates the read/write intensity counters according to the
     * type of access and increments the confidence if applicable.
     *
     * @param replacement_data Replacement data to be touched.
     * @param pkt Packet that generated this access.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data,
               const PacketPtr pkt) override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     * Resets its last touch tick as the current tick and its address
     * to the one of the Packet generating the access.
     *
     * @param replacement_data Replacement data to be reset.
     * @param pkt Packet that generated this access.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data,
               const PacketPtr pkt) override;

    /**
     * Find replacement victim using Read/Write intesity counters.
     *
     * @param candidates Replacement candidates, selected by indexing policy.
     * @return Replacement entry to be replaced.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const
                                                                     override;

    /**
     * dummy implementations to satisfy compiler as
     * interface with PacketPtr argument is used instead
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                      override
    {
    }

    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                      override
    {
    }

    /**
     * Instantiate a replacement data entry.
     *
     * @return A shared pointer to the new replacement data.
     */
    std::shared_ptr<ReplacementData> instantiateEntry() override;


    /**
     *
     * @param addr Address of the cache access that lead to a miss
     * @return whether the victim is to be looked for in the (non-)volatile
     * section of the caceh
     */
    bool victimInVolSection(Addr addr);


    void updatePrTable(HybridCacheBlk *blk);

    HybridCacheBlk*
      findLeastReadIntensiveBlock(std::vector<ReplaceableEntry*> blks);
    HybridCacheBlk*
      findLeastWriteIntensiveBlock(std::vector<ReplaceableEntry*> blks);


    int getWriteThreshold() {
      return writeThreshold;
    }

    void setWriteThreshold(int new_threshold) {
      writeThreshold = new_threshold;
    }

    int getReadThreshold() {
      return readThreshold;
    }

    void setReadThreshold(int new_threshold) {
      readThreshold = new_threshold;
    }

    bool
    hasHitReadThreshold(const std::shared_ptr<ReplacementData>& repl_data) {
      return std::static_pointer_cast<CMReplData>(
        repl_data)->riCounter >= readThreshold;
    }

    bool
    hasHitWriteThreshold(const std::shared_ptr<ReplacementData>& repl_data) {
      return std::static_pointer_cast<CMReplData>(
        repl_data)->wiCounter >= writeThreshold;
    }

    void
    resetWriteCounter(const std::shared_ptr<ReplacementData>& repl_data) {
      std::static_pointer_cast<CMReplData>(
        repl_data)->wiCounter = 0;
    }

    void
    resetReadCounter(const std::shared_ptr<ReplacementData>& repl_data) {
      std::static_pointer_cast<CMReplData>(
        repl_data)->riCounter = 0;
    }

    void
    increaseConfidence(const std::shared_ptr<ReplacementData>& repl_data) {
      if (std::static_pointer_cast<CMReplData>(
          repl_data)->conf < 3) {
            std::static_pointer_cast<CMReplData>(
              repl_data)->conf =
               std::static_pointer_cast<CMReplData>(repl_data)->conf + 1;
      }
    }

    int
    getConfidence(const std::shared_ptr<ReplacementData>& repl_data) {
      return std::static_pointer_cast<CMReplData>(
        repl_data)->conf;
    }


  private:
    // helper function
    unsigned calcTableAddress(Addr addr) {

      std::bitset<64> tmp(addr);
      std::bitset<64> addrBitRepr(0);

      // 1st variant: simply shift address causing the access
      if (!hashPCforPredictionTable) {
        // cut off byte address and use the rest as the index
        for (int i = 0; i < noOfTableAddrBits; i++) {
            addrBitRepr.set(i, tmp[i+noOfByteAddrBits]);
        }
        return addrBitRepr.to_ulong();
      } else {
      // 2nd variant: xor fold to hash PC

        // each section should be noOfTableAddrBits large
        unsigned noOfSectionToHash = 64/noOfTableAddrBits;
        unsigned remainingBits = 64 - (noOfSectionToHash*noOfTableAddrBits);
        unsigned tableIndex = 0;
        // iterate over sections
        for (int i = 0; i < noOfSectionToHash; i++) {
          // hash noOfTableAddrBits large section i
          // with remaining sections
          std::bitset<64> section(0);
          // iterate within section
          for (int j = 0; j < noOfTableAddrBits; j++) {
            section.set(j,tmp[i*noOfTableAddrBits+j]);
          }
            tableIndex ^= section.to_ulong();
        }

        // Special case if address can not be properly
        // split into evenly large sections
        if (remainingBits  > 0) {
          // simply use remaining bits as its own section
          // and fill up with zeroes
          std::bitset<64> section(0);
          for (int i = 0; i < remainingBits; i++) {
            section.set(i,tmp[64-remainingBits+i]);
          }
          tableIndex ^= section.to_ulong();
        }
        return tableIndex;
      }
    }
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_CM_RP_HH__
#endif
