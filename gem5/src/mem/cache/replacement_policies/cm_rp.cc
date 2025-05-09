#if HYBRID_CACHE == 1
#include "mem/cache/replacement_policies/cm_rp.hh"

#include <cassert>
#include <memory>

#include "base/intmath.hh"
#include "params/CMRP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

CM::CM(const Params &p)
  : Base(p),
     prVolTable(p.table_entries, true),
     noOfTableEntries(p.table_entries),
     noOfTableAddrBits(ceilLog2(p.table_entries)),
     noOfByteAddrBits(ceilLog2(p.block_size)),
     readThreshold(p.read_threshold),
     writeThreshold(p.write_threshold),
     hashPCforPredictionTable(p.prediction_table_hash_access)
{
    // Check parameters
    if (!isPowerOf2(noOfTableEntries)) {
        fatal("Table size has to be a power of 2!");
    }
}

void
CM::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Reset confidence
    std::static_pointer_cast<CMReplData>(
        replacement_data)->conf = -1;
    // Reset read/write intensity counters
    std::static_pointer_cast<CMReplData>(
        replacement_data)->wiCounter = -1;
    std::static_pointer_cast<CMReplData>(
        replacement_data)->riCounter = -1;
}

void
CM::touch(const std::shared_ptr<ReplacementData>& replacement_data,
    const PacketPtr pkt)
{
    if (pkt) {
        if (pkt->isRead()) {
            std::static_pointer_cast<CMReplData>(
                replacement_data)->riCounter++;
        } else {
            std::static_pointer_cast<CMReplData>(
                replacement_data)->wiCounter++;
        }
    }
}

void
CM::reset(const std::shared_ptr<ReplacementData>& replacement_data,
    const PacketPtr pkt)
{
    if (pkt) {
        Addr addr;
        if (hashPCforPredictionTable) {
            addr = pkt->req->hasPC() ? pkt->req->getPC() : 0;
        } else {
            addr = pkt->getAddr();
        }

        std::static_pointer_cast<CMReplData>(
            replacement_data)->addr = addr;
    }
    // Reset confidence
    std::static_pointer_cast<CMReplData>(
        replacement_data)->conf = 0;
    // Reset read/write intensity counters
    std::static_pointer_cast<CMReplData>(
        replacement_data)->wiCounter = 0;
    std::static_pointer_cast<CMReplData>(
        replacement_data)->riCounter = 0;

    if (pkt) {
        if (pkt->isRead()) {
            std::static_pointer_cast<CMReplData>(
                replacement_data)->riCounter++;
        } else {
            std::static_pointer_cast<CMReplData>(
                replacement_data)->wiCounter++;
        }
    }
}



ReplaceableEntry*
CM::getVictim(const ReplacementCandidates& candidates) const
{
    /*
     * This should be unused as victim identification
     *  depends on the cache section we look into
     */

    // There must be at least one replacement candidate
    assert(candidates.size() > 0);
    return candidates[0];
}

std::shared_ptr<ReplacementData>
CM::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new CMReplData());
}


bool
CM::victimInVolSection(Addr addr)
{
    unsigned prTableAddr = calcTableAddress(addr);
    return prVolTable.at(prTableAddr);
}


void
CM::updatePrTable(HybridCacheBlk * blk)
{
    Addr addr = std::static_pointer_cast<CMReplData>(
        blk->replacementData)->addr;
    unsigned prTableAddr = calcTableAddress(addr);
    prVolTable.at(prTableAddr) = blk->isVolatile();
}


HybridCacheBlk*
CM::findLeastReadIntensiveBlock(std::vector<ReplaceableEntry*> blks)
{
    assert(blks.size() > 0);
    ReplaceableEntry* candidate = blks[0];
    // find block currently the least read intensive
    // add confidence multiplied with threshold to counter
    // as counter is reset and conf incremented
    // once the threshhold has been hit
    for (ReplaceableEntry* entry : blks) {
        if ((std::static_pointer_cast<CMReplData>(
                entry->replacementData)->riCounter +
                (std::static_pointer_cast<CMReplData>(
                entry->replacementData)->conf * readThreshold))  <
            (std::static_pointer_cast<CMReplData>(
                candidate->replacementData)->riCounter+
                (std::static_pointer_cast<CMReplData>(
                candidate->replacementData)->conf * readThreshold))
            )
        {
            candidate = entry;
        }
    }
    HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(candidate);
    gem5_assert(hyblk,
        "This method should only be called with Hybrid Cache Blocks");
    return hyblk;
}

HybridCacheBlk*
CM::findLeastWriteIntensiveBlock(std::vector<ReplaceableEntry*> blks)
{
    assert(blks.size() > 0);
    ReplaceableEntry* candidate = blks[0];
    // find block currently the least write intensive
    // add confidence multiplied with threshold to counter
    // as counter is reset and conf incremented
    // once the threshhold has been hit
    for (ReplaceableEntry* entry : blks) {
        if ((std::static_pointer_cast<CMReplData>(
                entry->replacementData)->wiCounter +
                (std::static_pointer_cast<CMReplData>(
                entry->replacementData)->conf * writeThreshold)) <
            (std::static_pointer_cast<CMReplData>(
                candidate->replacementData)->wiCounter +
                (std::static_pointer_cast<CMReplData>(
                candidate->replacementData)->conf * writeThreshold))
            )
        {
            candidate = entry;
        }
    }
    HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(candidate);
    gem5_assert(hyblk,
        "This method should only be called with Hybrid Cache Blocks");
    return hyblk;
}

} // namespace replacement_policy
} // namespace gem5
#endif
