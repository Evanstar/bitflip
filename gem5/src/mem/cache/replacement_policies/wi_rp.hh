#if HYBRID_CACHE == 1
/**
 * @file
 * Declaration of a Write Intensity based replacement policy.
 * The victim is chosen using the last touch timestamp.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_WI_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_WI_RP_HH__

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "base/types.hh"
#include "mem/cache/hybrid_cache_blk.hh"
#include "mem/cache/replacement_policies/base.hh"

namespace gem5
{

struct WIRPParams;

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

class WI : public Base
{
  protected:

    /**
     * Table containing write intensity state for a
     * parameterisable number of entries.
     * There are four different write intensity states:
     * 0 = read intensive
     * 1 = weakly read intensive
     * 2 = weaky write intensive
     * 3 = write intensive
     */
    std::vector<short> wiTable;
    /** Number of entries in the write intensity and deadblock state tables */
    const unsigned noOfTableEntries;
    /** Number of bits for addresses accessing the wi and db tables */
    const unsigned noOfTableAddrBits;
    /** Number of bits for accesses to the same cache block. */
    const unsigned noOfByteAddrBits;
    /**
     * Threshold determining whether wiTable entry is incremented
     * or decremented depending on the wiCost of data to be evicted.
     */
    int threshold;

    /** Table containg current reuse state for individual dates */
    std::vector<int> deadBlockTable;
    int deadBlockThreshold;

    bool dbMigrationEnabled;

    bool hashPCforPredictionTable;

    /** WI-specific implementation of replacement data. */
    struct WIReplData : ReplacementData
    {
        /** Address that initially led to the cache block fill */
        Addr addr;
        /** Tick on which the entry was last touched. */
        Tick lastTouchTick;
        /**
         * Write intensity of accesses to the cache block
         * (incremented on write/decremented on read)
         */
        int wiCost;
        /**
         * Default constructor. Invalidate data.
         */
        WIReplData() : addr(0), lastTouchTick(0), wiCost(0) {}
    };

  public:
    typedef WIRPParams Params;
    WI(const Params &p);
    ~WI() = default;

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
     * Sets its last touch tick as the current tick and
     * updates the write intensity cost according to the
     * type of access.
     *
     * @param replacement_data Replacement data to be touched.
     * @param pkt Packet that generated this access.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data,
               const PacketPtr pkt) override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     * Sets its last touch tick as the current tick and its address
     * to the one of the Packet generating the access.
     *
     * @param replacement_data Replacement data to be reset.
     * @param pkt Packet that generated this access.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data,
               const PacketPtr pkt) override;

    /**
     * Find replacement victim using LRU timestamps.
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
                                                                      override;
    /**
     * Instantiate a replacement data entry.
     *
     * @return A shared pointer to the new replacement data.
     */
    std::shared_ptr<ReplacementData> instantiateEntry() override;

    /**
     * Update wiTable entry on eviction
     *
     * @param replacement_data Replacement data of data to be evicted.
     */
    void updateWiTable(
      const std::shared_ptr<ReplacementData>& replacement_data);

    /**
     * @param addr Address of the cache access that lead to a miss
     * @return whether the victim is to be looked for in the (non-)volatile
     * section of the caceh
     */
    bool victimInVolSection(Addr addr);

    HybridCacheBlk* lookForDeadBlock(std::vector<ReplaceableEntry*> blks);
    HybridCacheBlk*
      findMostReadIntensiveBlock(std::vector<ReplaceableEntry*> blks);
    HybridCacheBlk*
      findMostWriteIntensiveBlock(std::vector<ReplaceableEntry*> blks);

    /**
     * Reset all wiTable entries
     */
    void clearWiTable();
    void clearDbTable() {
      for (int i = 0; i < noOfTableEntries; i++) {
        int* dbState = &(deadBlockTable.at(i));
        *dbState = 0;
      }
    }

    int getWiThreshold() {
      return threshold;
    }

    void setWiThreshold(int new_threshold) {
      threshold = new_threshold;
    }

    int getDbThreshold() {
      return deadBlockThreshold;
    }

    void setDbThreshold(int new_threshold) {
      deadBlockThreshold = new_threshold;
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

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_WI_RP_HH__
#endif
