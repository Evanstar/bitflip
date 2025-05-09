#if HYBRID_CACHE == 1
/**
 * @file
 * Declaration of a round robin replacement policy.
 * The victim is chosen in a round robin manner
 * cycling through the way indices
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_RR_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_RR_RP_HH__

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"

namespace gem5
{

struct RRRPParams;

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

class RoundRobin : public Base
{
  protected:
    const unsigned assoc;
    const unsigned nvBlockRatio;
    const unsigned numSets;
    std::vector<unsigned> replCandPointers;

    /** RR-specific implementation of replacement data. */
    struct RRReplData : ReplacementData
    {
        //TODO We dont really need anything here right?

        /**
         * Default constructor. Invalidate data.
         */
        RRReplData() {}
    };
  public:
    typedef RRRPParams Params;
    RoundRobin(const Params &p);
    ~RoundRobin() = default;

    /**
     * Invalidate replacement data to set it as the next probable victim.
     * Prioritize replacement data for victimization.
     *
     * @param replacement_data Replacement data to be invalidated.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
                                                                    override;

    /**
     * Touch an entry to update its replacement data.
     * Does not do anything.
     *
     * @param replacement_data Replacement data to be touched.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     * Unprioritize replacement data for victimization.
     *
     * @param replacement_data Replacement data to be reset.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Find replacement victim at random.
     *
     * @param candidates Replacement candidates, selected by indexing policy.
     * @return Replacement entry to be replaced.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const
                                                                     override;

    /**
     * Instantiate a replacement data entry.
     *
     * @return A shared pointer to the new replacement data.
     */
    std::shared_ptr<ReplacementData> instantiateEntry() override;

    /**
     * Useful for power outages:
     * Reset all cyclic counters so that the volatile cache lines are
     * chosen as the replacement candidates in the following rounds.
     */
    void resetPointersToVolSection();

    void nextVictimCycle(unsigned setNo);
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_RR_RP_HH__
#endif
