#if HYBRID_CACHE == 1
/**
 * @file
 * Declaration of a hybrid set associative tag store.
 */

#ifndef __MEM_CACHE_TAGS_HYBRID_SET_ASSOC_HH__
#define __MEM_CACHE_TAGS_HYBRID_SET_ASSOC_HH__

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "base/logging.hh"
#include "base/types.hh"
#include "mem/cache/base.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/cache/hybrid_cache_blk.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "mem/cache/replacement_policies/cm_rp.hh"
#include "mem/cache/replacement_policies/lru_rp.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"
#include "mem/cache/replacement_policies/rr_rp.hh"
#include "mem/cache/replacement_policies/wi_rp.hh"
#include "mem/cache/tags/base.hh"
#include "mem/cache/tags/indexing_policies/base.hh"
#include "mem/packet.hh"
#include "params/HybridSetAssoc.hh"

namespace gem5
{

/**
 * A basic hybrid cache tag store.
 * @sa  \ref gem5MemorySystem "gem5 Memory System"
 *
 * The HybridSetAssoc placement policy divides the cache into s sets of w
 * cache lines (ways) with "volatileBlockRatio" percent of all cache blocks
 * organized as volatile cache blocks (the rest as non-volatile).
 */
class HybridSetAssoc : public BaseTags
{
 protected:
    /** The allocatable associativity of the cache (alloc mask). */
    unsigned allocAssoc;

    /**
     *  nvBlockRatio of n indicates every n-th cache block to be
     *  organized in a non-volatile manner (value of zero equals to
     *  every cache block being set as volatile)
     */
    unsigned nvBlockRatio;

    /** The cache blocks. */
    std::vector<HybridCacheBlk> blks;

    /** Whether tags and data are accessed sequentially. */
    const bool sequentialAccess;

    /** Replacement policy */
    replacement_policy::Base *replacementPolicy;

    bool dbMigrationEnabled;

    bool hashPCforPredictionTable;

    const Cycles dataWriteLatency;
    unsigned numSets;
    std::vector<unsigned> swapTicksLeft;
    EventFunctionWrapper blockSetsTickEvent;
 private:
    void swapBlock(HybridCacheBlk *src_blk, HybridCacheBlk *dest_blk);
  public:
    /** Convenience typedef. */
     typedef HybridSetAssocParams Params;

    /**
     * Construct and initialize this tag store.
     */
    HybridSetAssoc(const Params &p);

    /**
     * Destructor
     */
    virtual ~HybridSetAssoc() {};

    struct HybridTagStats : public statistics::Group
    {
        HybridTagStats(HybridSetAssoc &tags);

        void regStats() override;
        HybridSetAssoc &tags;
        /** Average occupancy of each requestor using the cache */
        statistics::AverageVector occupanciesVol;

        /** Average occ % of each requestor using the cache */
        statistics::Formula avgOccsVol;

        /** Average occupancy of each requestor using the cache */
        statistics::AverageVector occupanciesNonVol;

        /** Average occ % of each requestor using the cache */
        statistics::Formula avgOccsNonVol;

        /**
         * Number of migrations between
         * volatile and non-volatile Cache section
         */
        statistics::Scalar totalSwaps;
    } hybrid_tag_stats;

    /**
     * Initialize blocks as CacheBlk instances.
     */
    void tagsInit() override;

    /**
     * This function updates the tags when a block is invalidated. It also
     * updates the replacement data.
     *
     * @param blk The block to invalidate.
     */
    void invalidate(CacheBlk *blk) override;

    /**
     * Access block and update replacement data. May not succeed, in which case
     * nullptr is returned. This has all the implications of a cache access and
     * should only be used as such. Returns the tag lookup latency as a side
     * effect.
     *
     * @param pkt The packet holding the address to find.
     * @param lat The latency of the tag lookup.
     * @return Pointer to the cache block if found.
     */
    CacheBlk* accessBlock(const PacketPtr pkt, Cycles &lat) override
    {
        CacheBlk *blk = findBlock(pkt->getAddr(), pkt->isSecure());

        // Access all tags in parallel, hence one in each way.  The data side
        // either accesses all blocks in parallel, or one block sequentially on
        // a hit.  Sequential access with a miss doesn't access data.
        stats.tagAccesses += allocAssoc;
        if (sequentialAccess) {
            if (blk != nullptr) {
                stats.dataAccesses += 1;
            }
        } else {
            stats.dataAccesses += allocAssoc;
        }

        // The tag lookup latency is the same for a hit or a miss
        lat = lookupLatency;

        // If a cache hit
        if (blk != nullptr) {
            // Update number of references to accessed block
            blk->increaseRefCount();

            // Update replacement data of accessed block
            replacementPolicy->touch(blk->replacementData, pkt);

            replacement_policy::CM *cmRP = dynamic_cast<
                replacement_policy::CM *>(replacementPolicy);

            if (cmRP) {
                const std::vector<ReplaceableEntry*> entries =
                    indexingPolicy->getPossibleEntries(pkt->getAddr());
                std::vector<ReplaceableEntry*> volBlocks;
                std::vector<ReplaceableEntry*> nonVolBlocks;

                // Split entries into (non-)volatile cache sections
                for (ReplaceableEntry* entry : entries) {
                    if (static_cast<HybridCacheBlk*>(entry)->isVolatile()) {
                        volBlocks.push_back(entry);
                    } else {
                        nonVolBlocks.push_back(entry);
                    }
                }

                // Check if this access lead to hitting the respective
                // threshold and if so, check if we need to migrate or
                // increase the confidence value
                if (pkt->isRead()) {
                    if (cmRP->hasHitReadThreshold(blk->replacementData)) {
                        HybridCacheBlk* hyblk =
                            dynamic_cast<HybridCacheBlk*>(blk);
                        gem5_assert(hyblk,
                            "Got non-hybrid cache block in hybrid tag store");
                        if (hyblk->isVolatile()) {
                            // We had repeated accesses to the "wrong" section
                            // we need to migrate
                            HybridCacheBlk* swapPartner = cmRP->
                                findLeastReadIntensiveBlock(nonVolBlocks);
                            swapBlock(swapPartner, hyblk);
                            replacementPolicy->
                                reset(blk->replacementData, pkt);
                            if (swapPartner->isValid()) {
                                replacementPolicy->reset(
                                    swapPartner->replacementData, nullptr);
                            }
                            // We are now confident to be in the right section
                            cmRP->increaseConfidence(blk->replacementData);
                        } else {
                            // We are confident that this block
                            // is in the correct section
                            cmRP->increaseConfidence(blk->replacementData);
                            cmRP->resetReadCounter(blk->replacementData);
                        }
                    }
                } else {
                    if (cmRP->hasHitWriteThreshold(blk->replacementData)) {
                        HybridCacheBlk* hyblk =
                            dynamic_cast<HybridCacheBlk*>(blk);
                        gem5_assert(hyblk,
                            "Got non-hybrid cache block in hybrid tag store");
                        if (!hyblk->isVolatile()) {
                            // We had repeated accesses to the "wrong" section
                            // we need to migrate
                            HybridCacheBlk* swapPartner = cmRP->
                                findLeastWriteIntensiveBlock(volBlocks);
                            swapBlock(swapPartner, hyblk);
                            replacementPolicy->
                                reset(blk->replacementData, pkt);
                            if (swapPartner->isValid()) {
                                replacementPolicy->reset(
                                    swapPartner->replacementData, nullptr);
                            }
                            // We are now confident to be in the right section
                            cmRP->increaseConfidence(blk->replacementData);
                        } else {
                            // We are confident that this block
                            // is in the correct section
                            cmRP->increaseConfidence(blk->replacementData);
                            cmRP->resetWriteCounter(blk->replacementData);
                        }
                    }
                }
            }
        }

        return blk;
    }

    /**
     * Find replacement victim based on address. The list of evicted blocks
     * only contains the victim.
     *
     * @param addr Address to find a victim for.
     * @param is_secure True if the target memory space is secure.
     * @param size Size, in bits, of new block to allocate.
     * @param evict_blks Cache blocks to be evicted.
     * @return Cache block to be replaced.
     */
    CacheBlk* findVictim(Addr addr, const bool is_secure,
                         const std::size_t size,
                         std::vector<CacheBlk*>& evict_blks) override
    {
        // unused default interface
        return findVictim(addr, is_secure, size, evict_blks, 0);
    }

    CacheBlk* findVictim(Addr addr, const bool is_secure,
                         const std::size_t size,
                         std::vector<CacheBlk*>& evict_blks, Addr pc)
    {
        HybridCacheBlk* victim;
        // Get possible entries to be victimized
        const std::vector<ReplaceableEntry*> entries =
            indexingPolicy->getPossibleEntries(addr);

        replacement_policy::WI *wiReplacementPolicy = dynamic_cast<
            replacement_policy::WI *>(replacementPolicy);

        replacement_policy::CM *cmReplacementPolicy = dynamic_cast<
            replacement_policy::CM *>(replacementPolicy);
        // If we have a write intensity replacement policy we need to
        // further differentiate the possible entries
        if (wiReplacementPolicy) {
            std::vector<ReplaceableEntry*> volBlocks;
            std::vector<ReplaceableEntry*> nonVolBlocks;

            // Split entries into (non-)volatile cache sections
            for (ReplaceableEntry* entry : entries) {
                if (static_cast<HybridCacheBlk*>(entry)->isVolatile()) {
                    volBlocks.push_back(entry);
                } else {
                    nonVolBlocks.push_back(entry);
                }
            }

            // Only look for entries in section dictated by replacement policy
            // but check if there even are such blocks beforehand
            HybridCacheBlk* db = nullptr;
            Addr index = hashPCforPredictionTable ? pc : addr;
            if (wiReplacementPolicy->victimInVolSection(index)) {

                // sanity check if migration is even possible
                if (dbMigrationEnabled &&
                 volBlocks.size() != 0 && nonVolBlocks.size() != 0) {

                    // check if there is a dead block in the
                    // predicted volatile section
                    if ((db =
                     wiReplacementPolicy->lookForDeadBlock(volBlocks))) {
                        // choose dead block as victim
                        evict_blks.push_back(db);
                        return db;

                    // if there is a dead block in the other section,
                    // swap it with the most read intensive volatile block
                    } else if ((db =
                     wiReplacementPolicy->lookForDeadBlock(nonVolBlocks))) {
                        swapBlock(db,
                         wiReplacementPolicy->findMostReadIntensiveBlock(
                            volBlocks));
                        // dead block should now be swapped to the
                        // volatile section and can be used as victim
                        assert(db->isVolatile());
                        evict_blks.push_back(db);
                        return db;
                    }
                }

                // default case if migration is disabled
                // or no dead block was found
                victim = static_cast<HybridCacheBlk*>(
                        replacementPolicy->getVictim(
                        volBlocks.empty() ? nonVolBlocks : volBlocks));
            } else {

                // sanity check if migration is even possible
                if (dbMigrationEnabled &&
                 volBlocks.size() != 0 && nonVolBlocks.size() != 0) {

                    // check if there is a dead block in the
                    // predicted non-volatile section
                    if ((db =
                     wiReplacementPolicy->lookForDeadBlock(nonVolBlocks))) {
                        // choose dead block as victim
                        evict_blks.push_back(db);
                        return db;

                    // if there is a dead block in the other section,
                    // swap it with the most write intensive non-volatile block
                    } else if ((db =
                     wiReplacementPolicy->lookForDeadBlock(volBlocks))) {
                        swapBlock(db,
                         wiReplacementPolicy->findMostWriteIntensiveBlock(
                            nonVolBlocks));
                        // dead block should now be swapped to the
                        // non-volatile section and can be used as victim
                        assert(!db->isVolatile());
                        evict_blks.push_back(db);
                        return db;
                    }
                }

                // default case if migration is disabled
                // or no dead block was found
                victim = static_cast<HybridCacheBlk*>(
                        replacementPolicy->getVictim(
                        nonVolBlocks.empty() ? volBlocks : nonVolBlocks));
            }
        } else if (cmReplacementPolicy) {
            std::vector<ReplaceableEntry*> volBlocks;
            std::vector<ReplaceableEntry*> nonVolBlocks;

            // Split entries into (non-)volatile cache sections
            for (ReplaceableEntry* entry : entries) {
                if (static_cast<HybridCacheBlk*>(entry)->isVolatile()) {
                    volBlocks.push_back(entry);
                } else {
                    nonVolBlocks.push_back(entry);
                }
            }

            // Only look for entries in section dictated by replacement policy
            // but check if there even are such blocks beforehand
            Addr index = hashPCforPredictionTable ? pc : addr;
            if (cmReplacementPolicy->victimInVolSection(index)) {
                victim = cmReplacementPolicy->
                    findLeastWriteIntensiveBlock(
                    volBlocks.empty() ? nonVolBlocks : volBlocks);
            } else {
                victim = cmReplacementPolicy->
                    findLeastReadIntensiveBlock(
                    nonVolBlocks.empty() ? volBlocks : nonVolBlocks);
            }

        } else {
            // Choose replacement victim from all replacement candidates
            victim = static_cast<HybridCacheBlk*>(
                                replacementPolicy->getVictim(entries));
            replacement_policy::RoundRobin *rrReplacementPolicy = dynamic_cast<
            replacement_policy::RoundRobin *>(replacementPolicy);
            if (rrReplacementPolicy) {
                rrReplacementPolicy->nextVictimCycle(victim->getSet());
            }
        }

        // There is only one eviction for this replacement
        evict_blks.push_back(victim);

        return victim;
    }

    /**
     * Insert the new block into the cache and update replacement data.
     *
     * @param pkt Packet holding the address to update
     * @param blk The block to update.
     */
    void insertBlock(const PacketPtr pkt, CacheBlk *blk) override
    {
        HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
        gem5_assert(hyblk, "Got non-hybrid cache block in hybrid tag store");
        RequestorID requestor_id = pkt->req->requestorId();
        assert(requestor_id < system->maxRequestors());
        if (hyblk->isVolatile()) {
            hybrid_tag_stats.occupanciesVol[requestor_id]++;
        } else {
            hybrid_tag_stats.occupanciesNonVol[requestor_id]++;
        }

        // Insert block
        BaseTags::insertBlock(pkt, blk);

        // Increment tag counter
        stats.tagsInUse++;

        // Update replacement policy
        replacementPolicy->reset(blk->replacementData, pkt);
    }

    void moveBlock(CacheBlk *src_blk, CacheBlk *dest_blk) override;

    /**
     * Limit the allocation for the cache ways.
     * @param ways The maximum number of ways available for replacement.
     */
    virtual void setWayAllocationMax(int ways) override
    {
        fatal_if(ways < 1, "Allocation limit must be greater than zero");
        allocAssoc = ways;
    }

    /**
     * Get the way allocation mask limit.
     * @return The maximum number of ways available for replacement.
     */
    virtual int getWayAllocationMax() const override
    {
        return allocAssoc;
    }

    /**
     * Regenerate the block address from the tag and indexing location.
     *
     * @param block The block.
     * @return the block address.
     */
    Addr regenerateBlkAddr(const CacheBlk* blk) const override
    {
        return indexingPolicy->regenerateAddr(blk->getTag(), blk);
    }

    void forEachBlk(std::function<void(CacheBlk &)> visitor) override {
        for (CacheBlk& blk : blks) {
            visitor(blk);
        }
    }

    bool anyBlk(std::function<bool(CacheBlk &)> visitor) override {
        for (CacheBlk& blk : blks) {
            if (visitor(blk)) {
                return true;
            }
        }
        return false;
    }

    void forEachVolatileBlk(std::function<void(CacheBlk &)> visitor) {
        for (HybridCacheBlk& blk : blks) {
            if (blk.isVolatile()) {
                visitor(blk);
            }
        }
    }

    void forEachNonVolatileBlk(std::function<void(CacheBlk &)> visitor) {
        for (HybridCacheBlk& blk : blks) {
            if (!blk.isVolatile()) {
                visitor(blk);
            }
        }
    }

    bool anyVolatileBlk(std::function<bool(CacheBlk &)> visitor) {
        for (HybridCacheBlk& blk : blks) {
            if (blk.isVolatile() && visitor(blk)) {
                return true;
            }
        }
        return false;
    }

    bool anyNonVolatileBlk(std::function<bool(CacheBlk &)> visitor) {
        for (HybridCacheBlk& blk : blks) {
            if (!blk.isVolatile() && visitor(blk)) {
                return true;
            }
        }
        return false;
    }

    std::vector<HybridCacheBlk*> getVolCacheBlocks() {
        std::vector<HybridCacheBlk*> c;
        for (HybridCacheBlk& blk : blks) {
            if (blk.isVolatile()) {
                c.push_back(&blk);
            }
        }
        return c;
    }

    std::vector<HybridCacheBlk*> getNonVolCacheBlocks() {
        std::vector<HybridCacheBlk*> c;
        for (HybridCacheBlk& blk : blks) {
            if (!blk.isVolatile()) {
                c.push_back(&blk);
            }
        }
        return c;
    }

    /**
     * Interface to reset all meta data for the replacement policy
     * in case of a power outage
     */
    void resetReplData() {
        for (HybridCacheBlk& blk : blks) {
            if (blk.isVolatile()) {
                replacementPolicy->invalidate(blk.replacementData);
            } else if (!(blk.isVolatile()) && blk.isValid()) {
                replacementPolicy->reset(blk.replacementData);
            }

        }
        replacement_policy::WI *wiReplacementPolicy = dynamic_cast<
        replacement_policy::WI *>(replacementPolicy);
        if (wiReplacementPolicy) {
            wiReplacementPolicy->clearWiTable();
        }
        replacement_policy::RoundRobin *rrReplacementPolicy = dynamic_cast<
        replacement_policy::RoundRobin *>(replacementPolicy);
        if (rrReplacementPolicy) {
            rrReplacementPolicy->resetPointersToVolSection();
        }
    }

    /**
     * Interface to migrate important data
     * in case of a power outage
     */
    void saveImportantData();

    Tick swapBlockingTicksLeft(Addr addr) {
        int setNo = (addr >> floorLog2(blkSize)) & (numSets -1);
        return swapTicksLeft[setNo];
    }

    void decrementBlockTicks() {
        bool needToReschedule = false;
        for (int i = 0; i < numSets; i++) {
            if (swapTicksLeft[i] > 0) {
                swapTicksLeft[i]--;
            }
            if (swapTicksLeft[i] != 0) {
                needToReschedule = true;
            }
        }
        if (needToReschedule && !blockSetsTickEvent.scheduled()) {
            schedule(blockSetsTickEvent, curTick() + 1);
        }
    }

};

} // namespace gem5

#endif //__MEM_CACHE_TAGS_HYBRID_SET_ASSOC_HH__
#endif
