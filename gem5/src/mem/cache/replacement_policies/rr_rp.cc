#if HYBRID_CACHE == 1
#include "mem/cache/replacement_policies/rr_rp.hh"

#include <cassert>
#include <cmath>
#include <memory>

#include "base/intmath.hh"
#include "params/RRRP.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

RoundRobin::RoundRobin(const Params &p)
  : Base(p),
    assoc(p.assoc),
    nvBlockRatio(p.nvBlockRatio),
    numSets(p.size / (p.entry_size * p.assoc)),
    replCandPointers(numSets,0)
{
}

void
RoundRobin::invalidate(
    const std::shared_ptr<ReplacementData>& replacement_data)
{
}

void
RoundRobin::touch(
    const std::shared_ptr<ReplacementData>& replacement_data) const
{
}

void
RoundRobin::reset(
    const std::shared_ptr<ReplacementData>& replacement_data) const
{
}

ReplaceableEntry*
RoundRobin::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    ReplaceableEntry* victim = candidates[0];
    int setNo = victim->getSet();
    unsigned wayToRepl = replCandPointers[setNo];
    // Visit all candidates to search for the next entry to be evcited
    // and update cyclic couner for corresponding cache set
    for (const auto& candidate : candidates) {
        if (candidate->getWay() == wayToRepl) {
            victim = candidate;
            break;
        }
    }

    return victim;
}

std::shared_ptr<ReplacementData>
RoundRobin::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new RRReplData());
}

void
RoundRobin::resetPointersToVolSection() {
    unsigned numNvBlocksPerSet = static_cast<unsigned>(
        round((static_cast<float>(nvBlockRatio)/100)*assoc));
    for (int i= 0; i< replCandPointers.size(); i++) {
        replCandPointers[i] = numNvBlocksPerSet;
    }

}

void
RoundRobin::nextVictimCycle(unsigned setNo) {
    replCandPointers[setNo] = (replCandPointers[setNo] + 1) % assoc;
}

} // namespace replacement_policy
} // namespace gem5
#endif
