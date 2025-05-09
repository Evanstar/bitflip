#if HYBRID_CACHE == 1
#include "mem/cache/replacement_policies/wi_rp.hh"

#include <cassert>
#include <memory>

#include "base/intmath.hh"
#include "params/WIRP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

WI::WI(const Params &p)
  : Base(p),
     wiTable(p.table_entries, 2),
     noOfTableEntries(p.table_entries),
     noOfTableAddrBits(ceilLog2(p.table_entries)),
     noOfByteAddrBits(ceilLog2(p.block_size)),
     threshold(p.wi_threshold),
     deadBlockTable(p.table_entries, 0),
     deadBlockThreshold(p.db_threshold),
     dbMigrationEnabled(p.db_migration_enabled),
     hashPCforPredictionTable(p.prediction_table_hash_access)
{
    // Check parameters
    if (!isPowerOf2(noOfTableEntries)) {
        fatal("Table size has to be a power of 2!");
    }
}

void
WI::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Reset last touch timestamp
    std::static_pointer_cast<WIReplData>(
        replacement_data)->lastTouchTick = Tick(0);
    // Reset write intensity
    std::static_pointer_cast<WIReplData>(
        replacement_data)->wiCost = 0;
}

void
WI::touch(const std::shared_ptr<ReplacementData>& replacement_data,
    const PacketPtr pkt)
{
    Addr addr;
    if (hashPCforPredictionTable) {
        addr = pkt->req->hasPC() ? pkt->req->getPC() : 0;
    } else {
        addr = pkt->getAddr();
    }

    if (dbMigrationEnabled) {
        unsigned tableAddr = calcTableAddress(addr);
        int* dbState = &(deadBlockTable.at(tableAddr));
        (*dbState)++;
    }
    // Update last touch timestamp
    std::static_pointer_cast<WIReplData>(
        replacement_data)->lastTouchTick = curTick();
    // Update write intensity
    if (pkt->isRead()) {
        std::static_pointer_cast<WIReplData>(
            replacement_data)->wiCost--;
    } else {
        std::static_pointer_cast<WIReplData>(
            replacement_data)->wiCost += 24;
    }
}

void
WI::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Set last touch timestamp
    std::static_pointer_cast<WIReplData>(
        replacement_data)->lastTouchTick = curTick();
    std::static_pointer_cast<WIReplData>(
    replacement_data)->wiCost = 0;
}


void
WI::reset(const std::shared_ptr<ReplacementData>& replacement_data,
    const PacketPtr pkt)
{
    Addr addr;
    if (hashPCforPredictionTable) {
        addr = pkt->req->hasPC() ? pkt->req->getPC() : 0;
    } else {
        addr = pkt->getAddr();
    }

    if (dbMigrationEnabled) {
        unsigned tableAddr = calcTableAddress(addr);
        int* dbState = &(deadBlockTable.at(tableAddr));
        (*dbState)--;
    }
    std::static_pointer_cast<WIReplData>(
        replacement_data)->addr = addr;
    // Set last touch timestamp
    std::static_pointer_cast<WIReplData>(
        replacement_data)->lastTouchTick = curTick();
    // Reset write intensity
    std::static_pointer_cast<WIReplData>(
        replacement_data)->wiCost = 0;
    if (pkt->isRead()) {
        std::static_pointer_cast<WIReplData>(
            replacement_data)->wiCost = -1;
    } else {
        std::static_pointer_cast<WIReplData>(
            replacement_data)->wiCost = 24;
    }
}

ReplaceableEntry*
WI::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Visit all candidates to find victim
    ReplaceableEntry* victim = candidates[0];
    for (const auto& candidate : candidates) {
        // Update victim entry if necessary
        if (std::static_pointer_cast<WIReplData>(
                    candidate->replacementData)->lastTouchTick <
                std::static_pointer_cast<WIReplData>(
                    victim->replacementData)->lastTouchTick) {
            victim = candidate;
        }
    }

    return victim;
}

std::shared_ptr<ReplacementData>
WI::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new WIReplData());
}


void
WI::updateWiTable(const std::shared_ptr<ReplacementData>& replacement_data)
{
    Addr addr = std::static_pointer_cast<WIReplData>(
        replacement_data)->addr;
    int wiCost = std::static_pointer_cast<WIReplData>(
        replacement_data)->wiCost;

    unsigned wiTableAddr = calcTableAddress(addr);
    short* wiState = &(wiTable.at(wiTableAddr));

    // if write intensity costs are higher than the
    // threshold go to next more write intensive state
    // else to more read intensive state
    if (wiCost >= threshold) {
        // increment wiTable entry if possilble
        if (*wiState < 3) {
            (*wiState)++;
        }
    } else {
        if (*wiState > 0) {
            (*wiState)--;
        }
    }
}

bool
WI::victimInVolSection(Addr addr)
{
    // there are 4 write intensity states.
    // if the date is in one of the top 2 states,
    // it is predicted write intensive and should
    // therefore be stored in the volatile section
    unsigned wiTableAddr = calcTableAddress(addr);
    short wiState = wiTable.at(wiTableAddr);
    return wiState >= 2;
}

HybridCacheBlk*
WI::lookForDeadBlock(std::vector<ReplaceableEntry*> blks)
{
    HybridCacheBlk* candidate = nullptr;
    for (ReplaceableEntry* entry : blks) {
        HybridCacheBlk* hyblk = static_cast<HybridCacheBlk*>(entry);
        if (hyblk->isValid()) {
            // check if valid block is predicted dead
            Addr addr = std::static_pointer_cast<WIReplData>(
                hyblk->replacementData)->addr;
            unsigned tableAddr = calcTableAddress(addr);
            int dbState = deadBlockTable.at(tableAddr);
            if (dbState < deadBlockThreshold) {
                // if there are multiple candidates, choose the LRU
                if (candidate) {
                    if (std::static_pointer_cast<WIReplData>(
                          hyblk->replacementData)->lastTouchTick <
                          std::static_pointer_cast<WIReplData>(
                          candidate->replacementData)->lastTouchTick) {
                        candidate = hyblk;
                    }
                } else {
                    candidate = hyblk;
                }
            }
        } else {
            // invalid blocks are naturally as dead as it gets
            return hyblk;
        }
    }
    return candidate;
}

HybridCacheBlk*
WI::findMostReadIntensiveBlock(std::vector<ReplaceableEntry*> blks)
{
    assert(blks.size() > 0);
    ReplaceableEntry* candidate = blks[0];
    // find block that is currently the most read intensive
    for (ReplaceableEntry* entry : blks) {
        if (std::static_pointer_cast<WIReplData>(
                    entry->replacementData)->wiCost <
                std::static_pointer_cast<WIReplData>(
                    candidate->replacementData)->wiCost) {
            candidate = entry;
        }
    }
    HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(candidate);
    gem5_assert(hyblk,
        "This method should only be called with Hybrid Cache Blocks");
    return hyblk;
}

HybridCacheBlk*
WI::findMostWriteIntensiveBlock(std::vector<ReplaceableEntry*> blks)
{
    assert(blks.size() > 0);
    ReplaceableEntry* candidate = blks[0];
    // find block that is currently the most write intensive
    for (ReplaceableEntry* entry : blks) {
        if (std::static_pointer_cast<WIReplData>(
                    entry->replacementData)->wiCost >
                std::static_pointer_cast<WIReplData>(
                    candidate->replacementData)->wiCost) {
            candidate = entry;
        }
    }
    HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(candidate);
    gem5_assert(hyblk,
        "This method should only be called with Hybrid Cache Blocks");
    return hyblk;
}

void
WI::clearWiTable()
{
    for (int i = 0; i < noOfTableEntries; i++) {
        short* wiState = &(wiTable.at(i));
        *wiState = 1;
    }
}

} // namespace replacement_policy
} // namespace gem5
#endif
