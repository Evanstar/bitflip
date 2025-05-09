#if HYBRID_CACHE == 1
/**
 * @file
 * Definitions of a hybrid set associative tag store.
 */

#include "mem/cache/tags/hybrid_set_assoc.hh"

#include <string>

#include "base/intmath.hh"
#include "debug/CacheRepl.hh"

namespace gem5
{

HybridSetAssoc::HybridSetAssoc(const Params &p)
    :BaseTags(p), allocAssoc(p.assoc),
     nvBlockRatio(p.nv_block_ratio),
     blks(p.size / p.block_size),
     sequentialAccess(p.sequential_access),
     replacementPolicy(p.replacement_policy),
     dbMigrationEnabled(p.db_migration_enabled),
     hashPCforPredictionTable(p.prediction_table_hash_access),
     dataWriteLatency(p.data_write_latency),
     numSets(p.size / (p.entry_size * p.assoc)),
     swapTicksLeft(numSets, 0),
     blockSetsTickEvent([this]{ decrementBlockTicks(); }, name()),
     hybrid_tag_stats(*this)
{
    // There must be a indexing policy
    fatal_if(!p.indexing_policy, "An indexing policy is required");

    // Check parameters
    if (blkSize < 4 || !isPowerOf2(blkSize)) {
        fatal("Block size must be at least 4 and a power of 2");
    }

    if (nvBlockRatio > 100) {
        fatal("non-volatile block ratio cannot be > 100");
    }

}

void
HybridSetAssoc::tagsInit()
{
    unsigned numNvBlocksPerSet = static_cast<unsigned>(
        round((static_cast<float>(nvBlockRatio)/100)*allocAssoc));

    // Number of non-volatile blocks in currently observed set
    unsigned numNvBlocks = 0;
    // Initialize all blocks
    for (unsigned blk_index = 0; blk_index < numBlocks; blk_index++) {
        // Locate next cache block
        HybridCacheBlk* blk = &blks[blk_index];

        if (blk_index % allocAssoc == 0) {
            numNvBlocks = 0;
        }
        // Set whether block is located in
        // volatile or non-volatile part of the cache
        if (numNvBlocks < numNvBlocksPerSet) {
            blk->setVolatile(false);
            numNvBlocks++;
        } else {
            blk->setVolatile(true);
        }

        // Link block to indexing policy
        indexingPolicy->setEntry(blk, blk_index);

        // Associate a data chunk to the block
        blk->data = &dataBlks[blkSize*blk_index];

        // Associate a replacement data entry to the block
        blk->replacementData = replacementPolicy->instantiateEntry();
    }
}

void
HybridSetAssoc::invalidate(CacheBlk *blk)
{
    HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
    gem5_assert(hyblk, "Got non-hybrid cache block in hybrid tag store");
    if (hyblk->isVolatile()) {
        hybrid_tag_stats.occupanciesVol[blk->getSrcRequestorId()]--;
    } else {
        hybrid_tag_stats.occupanciesNonVol[blk->getSrcRequestorId()]--;
    }

    BaseTags::invalidate(blk);

    // Decrease the number of tags in use
    stats.tagsInUse--;

    // Update the state tables if applicable
    replacement_policy::WI *wiReplacementPolicy = dynamic_cast<
            replacement_policy::WI *>(replacementPolicy);

    replacement_policy::CM *cmReplacementPolicy = dynamic_cast<
            replacement_policy::CM *>(replacementPolicy);

    if (wiReplacementPolicy) {
        wiReplacementPolicy->updateWiTable(blk->replacementData);
    } else if (cmReplacementPolicy) {
        cmReplacementPolicy->updatePrTable(hyblk);
    }

    // Invalidate replacement data
    replacementPolicy->invalidate(blk->replacementData);
}

void
HybridSetAssoc::moveBlock(CacheBlk *src_blk, CacheBlk *dest_blk)
{
    BaseTags::moveBlock(src_blk, dest_blk);

    // Since the blocks were using different replacement data pointers,
    // we must touch the replacement data of the new entry, and invalidate
    // the one that is being moved.

    //TODO: This is interesting for migration policies, maybe we need to add
    // further differentiations in here
    replacementPolicy->invalidate(src_blk->replacementData);
    replacementPolicy->reset(dest_blk->replacementData);
}

void
HybridSetAssoc::swapBlock(HybridCacheBlk *src_blk, HybridCacheBlk *dest_blk)
{

    DPRINTF(CacheRepl, "Swapping potentially invalid: %s with: %s\n",
                src_blk->print(), dest_blk->print());
    /*
    CacheBlk* tmpBlk = new CacheBlk();
    tmpBlk->replacementData = replacementPolicy->instantiateEntry();
    tmpBlk->data = new uint8_t(0);
    // dest_blk might be invalid anyway
    // then we don't need the extra steps over the tmpBlk
    bool destValid = dest_blk->isValid();
    if (destValid) {
        DPRINTF(CacheRepl, "Using tmpBlk for swap\n");
        *tmpBlk->data = *dest_blk->data;
        moveBlock(dest_blk, tmpBlk);

    }
    *dest_blk->data = *src_blk->data;
    moveBlock(src_blk, dest_blk);
    if (destValid) {
        *src_blk->data = *tmpBlk->data;
        moveBlock(tmpBlk, src_blk);
    }
    */


    // swapping volatility between the two cache blocks is sufficient
    // as they are always part of the same cache set
    if (src_blk->isVolatile()) {
        assert(!dest_blk->isVolatile());
        src_blk->setVolatile(false);
        dest_blk->setVolatile(true);
    } else {
        assert(dest_blk->isVolatile());
        src_blk->setVolatile(true);
        dest_blk->setVolatile(false);
    }
    assert(dest_blk->isValid());
    // Calculate set the cache blocks belong to and
    // block access to the set for the time necessary to perform the swap
    Addr addr = indexingPolicy->regenerateAddr(dest_blk->getTag(), dest_blk);
    unsigned setNo = (addr >> floorLog2(blkSize)) & (numSets -1);
    swapTicksLeft[setNo] += cyclesToTicks(dataWriteLatency);
    if (!blockSetsTickEvent.scheduled()) {
        schedule(blockSetsTickEvent, curTick() + 1);
    }

    hybrid_tag_stats.totalSwaps++;
}

void
HybridSetAssoc::saveImportantData() {
    replacement_policy::CM *cmRP = dynamic_cast<
        replacement_policy::CM *>(replacementPolicy);
    if (cmRP) {
        // Traverse through the blocks, set by set
        for (int i = 0; i < numBlocks; i += allocAssoc) {
            std::vector<HybridCacheBlk *> blksInThisSet;
            for (int offset = 0; offset < allocAssoc; offset++) {
                blksInThisSet.push_back(&(blks[i+offset]));
            }
            bool doneLookingForSwapPairs = false;
            // swap for as long as we can find volatile data
            // more important than non-volatile data
            while (!doneLookingForSwapPairs) {
                HybridCacheBlk* toSave = nullptr;
                HybridCacheBlk* toSacrifice = nullptr;
                for (HybridCacheBlk* blk : blksInThisSet) {
                    if (blk->isVolatile()) {
                        if (blk->isValid()) {
                            bool moreImportant =
                                cmRP->getConfidence(blk->replacementData) >
                                (toSave ? cmRP->getConfidence(
                                    toSave->replacementData) : -1);
                            if (moreImportant) {
                                toSave = blk;
                            }
                        }
                    } else {
                        bool lessImportant =
                            cmRP->getConfidence(blk->replacementData) <
                            (toSacrifice ? cmRP->getConfidence(
                                toSacrifice->replacementData) : 999);
                        if (lessImportant || !blk->isValid()) {
                            toSacrifice = blk;
                        }
                    }
                }

                if (!toSave || !toSacrifice) {
                    // This case occurs if there are no
                    // (non-)volatile cache blocks
                    doneLookingForSwapPairs = true;
                } else if (cmRP->getConfidence(toSave->replacementData) >
                    cmRP->getConfidence(toSacrifice->replacementData)) {
                        swapBlock(toSacrifice, toSave);
                } else {
                    // There is no data in the volatile the section that is
                    // more important than the least important data in the
                    // non-volatile section
                    doneLookingForSwapPairs = true;
                }

            }
        }
    }
}

HybridSetAssoc::HybridTagStats::HybridTagStats(HybridSetAssoc &_tags)
    : statistics::Group(&_tags),
    tags(_tags),
    ADD_STAT(occupanciesVol, statistics::units::Rate<
                statistics::units::Count, statistics::units::Tick>::get(),
             "Average occupied blocks per tick, per requestor"),
    ADD_STAT(avgOccsVol, statistics::units::Rate<
                statistics::units::Ratio, statistics::units::Tick>::get(),
             "Average percentage of cache occupancy"),
    ADD_STAT(occupanciesNonVol, statistics::units::Rate<
                statistics::units::Count, statistics::units::Tick>::get(),
             "Average occupied blocks per tick, per requestor"),
    ADD_STAT(avgOccsNonVol, statistics::units::Rate<
                statistics::units::Ratio, statistics::units::Tick>::get(),
             "Average percentage of cache occupancy"),
    ADD_STAT(totalSwaps, statistics::units::Count::get(),
             "Number of migrations between the volatile "
             "and non-volatile Cache section")
{
}

void
HybridSetAssoc::HybridTagStats::regStats()
{
    using namespace statistics;

    statistics::Group::regStats();

    System *system = tags.system;

    occupanciesVol
        .init(system->maxRequestors())
        .flags(nozero | nonan)
        ;
    for (int i = 0; i < system->maxRequestors(); i++) {
        occupanciesVol.subname(i, system->getRequestorName(i));
    }

    occupanciesNonVol
        .init(system->maxRequestors())
        .flags(nozero | nonan)
        ;
    for (int i = 0; i < system->maxRequestors(); i++) {
        occupanciesNonVol.subname(i, system->getRequestorName(i));
    }

    avgOccsVol.flags(nozero | total);
    for (int i = 0; i < system->maxRequestors(); i++) {
        avgOccsVol.subname(i, system->getRequestorName(i));
    }

    avgOccsNonVol.flags(nozero | total);
    for (int i = 0; i < system->maxRequestors(); i++) {
        avgOccsNonVol.subname(i, system->getRequestorName(i));
    }
    int numVolBlocks = tags.numBlocks*(1-(tags.nvBlockRatio/100.0));
    int numNonVolBlocks = tags.numBlocks*(tags.nvBlockRatio/100.0);
    avgOccsVol = occupanciesVol /
        statistics::constant(numVolBlocks);
    avgOccsNonVol = occupanciesNonVol /
        statistics::constant(numNonVolBlocks);

}

} // namespace gem5
#endif
