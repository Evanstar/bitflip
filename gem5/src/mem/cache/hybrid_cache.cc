#if HYBRID_CACHE == 1
/**
 * @file
 * Definition of HybridCache functions.
 */

#include "mem/cache/hybrid_cache.hh"

#include <cassert>

#include "base/compiler.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/Cache.hh"
#include "debug/CacheRepl.hh"
#include "debug/CacheTags.hh"
#include "debug/CacheVerbose.hh"
#include "enums/Clusivity.hh"
#include "mem/cache/hybrid_cache_blk.hh"
#include "mem/cache/prefetch/base.hh"
#include "params/HybridCache.hh"

namespace gem5
{

HybridCache::HybridCache(const HybridCacheParams &p)
    : Cache(p),
      dataReadLatency(p.data_read_latency),
      dataWriteLatency(p.data_write_latency),
      volReadEnergy(p.vol_read_energy),
      nonVolReadEnergy(p.non_vol_read_energy),
      volWriteEnergy(p.vol_write_energy),
      nonVolWriteEnergy(p.non_vol_write_energy),
      hybrid_stats(*this)
{
    assert(p.tags);
}


bool
HybridCache::access(PacketPtr pkt, CacheBlk *&blk, Cycles &lat,
                  PacketList &writebacks)
{
    // sanity check
    assert(pkt->isRequest());

    gem5_assert(!(isReadOnly && pkt->isWrite()),
                "Should never see a write in a read-only cache %s\n",
                name());

    if (pkt->req->isUncacheable()) {
        DPRINTF(Cache, "%s for %s\n", __func__, pkt->print());

        // flush and invalidate any existing block
        CacheBlk *old_blk(tags->findBlock(pkt->getAddr(), pkt->isSecure()));
        if (old_blk && old_blk->isValid()) {
            BaseCache::evictBlock(old_blk, writebacks);
        }

        blk = nullptr;
        // lookupLatency is the latency in case the request is uncacheable.
        lat = lookupLatency;
        return false;
    }

    // Access block in the tags
    Cycles tag_latency(0);
    blk = tags->accessBlock(pkt, tag_latency);

    DPRINTF(Cache, "%s for %s %s\n", __func__, pkt->print(),
            blk ? "hit " + blk->print() : "miss");

    if (pkt->req->isCacheMaintenance()) {
        // A cache maintenance operation is always forwarded to the
        // memory below even if the block is found in dirty state.

        // We defer any changes to the state of the block until we
        // create and mark as in service the mshr for the downstream
        // packet.

        // Calculate access latency on top of when the packet arrives. This
        // takes into account the bus delay.
        lat = calculateTagOnlyLatency(pkt->headerDelay, tag_latency);

        return false;
    }

    if (pkt->isEviction()) {
        // We check for presence of block in above caches before issuing
        // Writeback or CleanEvict to write buffer. Therefore the only
        // possible cases can be of a CleanEvict packet coming from above
        // encountering a Writeback generated in this cache peer cache and
        // waiting in the write buffer. Cases of upper level peer caches
        // generating CleanEvict and Writeback or simply CleanEvict and
        // CleanEvict almost simultaneously will be caught by snoops sent out
        // by crossbar.
        WriteQueueEntry *wb_entry = writeBuffer.findMatch(pkt->getAddr(),
                                                          pkt->isSecure());
        if (wb_entry) {
            assert(wb_entry->getNumTargets() == 1);
            PacketPtr wbPkt = wb_entry->getTarget()->pkt;
            assert(wbPkt->isWriteback());

            if (pkt->isCleanEviction()) {
                // The CleanEvict and WritebackClean snoops into other
                // peer caches of the same level while traversing the
                // crossbar. If a copy of the block is found, the
                // packet is deleted in the crossbar. Hence, none of
                // the other upper level caches connected to this
                // cache have the block, so we can clear the
                // BLOCK_CACHED flag in the Writeback if set and
                // discard the CleanEvict by returning true.
                wbPkt->clearBlockCached();

                // A clean evict does not need to access the data array
                lat = calculateTagOnlyLatency(pkt->headerDelay, tag_latency);

                return true;
            } else {
                assert(pkt->cmd == MemCmd::WritebackDirty);
                // Dirty writeback from above trumps our clean
                // writeback... discard here
                // Note: markInService will remove entry from writeback buffer.
                markInService(wb_entry);
                delete wbPkt;
            }
        }
    }

    // The critical latency part of a write depends only on the tag access
    if (pkt->isWrite()) {
        lat = calculateTagOnlyLatency(pkt->headerDelay, tag_latency);
    }

    // Writeback handling is special case.  We can write the block into
    // the cache without having a writeable copy (or any copy at all).
    if (pkt->isWriteback()) {
        assert(blkSize == pkt->getSize());

        // we could get a clean writeback while we are having
        // outstanding accesses to a block, do the simple thing for
        // now and drop the clean writeback so that we do not upset
        // any ordering/decisions about ownership already taken
        if (pkt->cmd == MemCmd::WritebackClean &&
            mshrQueue.findMatch(pkt->getAddr(), pkt->isSecure())) {
            DPRINTF(Cache, "Clean writeback %#llx to block with MSHR, "
                    "dropping\n", pkt->getAddr());

            // A writeback searches for the block, then writes the data.
            // As the writeback is being dropped, the data is not touched,
            // and we just had to wait for the time to find a match in the
            // MSHR. As of now assume a mshr queue search takes as long as
            // a tag lookup for simplicity.
            return true;
        }

        const bool has_old_data = blk && blk->isValid();
        if (!blk) {
            // need to do a replacement
            blk = allocateBlock(pkt, writebacks);
            if (!blk) {
                // no replaceable block available: give up, fwd to next level.
                incMissCount(pkt);
                return false;
            }

            blk->setCoherenceBits(CacheBlk::ReadableBit);
        } else if (compressor) {
            // This is an overwrite to an existing block, therefore we need
            // to check for data expansion (i.e., block was compressed with
            // a smaller size, and now it doesn't fit the entry anymore).
            // If that is the case we might need to evict blocks.
            if (!updateCompressionData(blk, pkt->getConstPtr<uint64_t>(),
                writebacks)) {
                invalidateBlock(blk);
                return false;
            }
        }

        // only mark the block dirty if we got a writeback command,
        // and leave it as is for a clean writeback
        if (pkt->cmd == MemCmd::WritebackDirty) {
            // TODO: the coherent cache can assert that the dirty bit is set
            blk->setCoherenceBits(CacheBlk::DirtyBit);
        }
        // if the packet does not have sharers, it is passing
        // writable, and we got the writeback in Modified or Exclusive
        // state, if not we are in the Owned or Shared state
        if (!pkt->hasSharers()) {
            blk->setCoherenceBits(CacheBlk::WritableBit);
        }
        // nothing else to do; writeback doesn't expect response
        assert(!pkt->needsResponse());

        updateBlockData(blk, pkt, has_old_data);
        DPRINTF(Cache, "%s new state is %s\n", __func__, blk->print());
        incHitCount(pkt);

        // When the packet metadata arrives, the tag lookup will be done while
        // the payload is arriving. Then the block will be ready to access as
        // soon as the fill is done
        HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
        gem5_assert(hyblk,
            "HybridCache should only be configured using HybridSetAssoc");
        Cycles fillLat = hyblk->isVolatile() ? dataLatency : dataWriteLatency;
        HybridSetAssoc* hytags = dynamic_cast<HybridSetAssoc*>(tags);
        gem5_assert(hytags,
        "HybridCache should only be configured using HybridSetAssoc");
        blk->setWhenReady(clockEdge(fillLat) + pkt->headerDelay +
            std::max(cyclesToTicks(tag_latency), (uint64_t)pkt->payloadDelay) +
            hytags->swapBlockingTicksLeft(pkt->getAddr()));

        if (hyblk->isVolatile()) {
            hybrid_stats.volWrites += pkt->getSize();
            hybrid_stats.noOfVolWrites++;
            hybrid_stats.dynEnergy += volWriteEnergy;
        } else {
            hybrid_stats.nonVolWrites += pkt->getSize();
            hybrid_stats.noOfNonVolWrites++;
            hybrid_stats.dynEnergy += nonVolWriteEnergy;
        }
        return true;
    } else if (pkt->cmd == MemCmd::CleanEvict) {
        // A CleanEvict does not need to access the data array
        lat = calculateTagOnlyLatency(pkt->headerDelay, tag_latency);

        if (blk) {
            // Found the block in the tags, need to stop CleanEvict from
            // propagating further down the hierarchy. Returning true will
            // treat the CleanEvict like a satisfied write request and delete
            // it.
            return true;
        }
        // We didn't find the block here, propagate the CleanEvict further
        // down the memory hierarchy. Returning false will treat the CleanEvict
        // like a Writeback which could not find a replaceable block so has to
        // go to next level.
        return false;
    } else if (pkt->cmd == MemCmd::WriteClean) {
        // WriteClean handling is a special case. We can allocate a
        // block directly if it doesn't exist and we can update the
        // block immediately. The WriteClean transfers the ownership
        // of the block as well.
        assert(blkSize == pkt->getSize());

        const bool has_old_data = blk && blk->isValid();
        if (!blk) {
            if (pkt->writeThrough()) {
                // if this is a write through packet, we don't try to
                // allocate if the block is not present
                return false;
            } else {
                // a writeback that misses needs to allocate a new block
                blk = allocateBlock(pkt, writebacks);
                if (!blk) {
                    // no replaceable block available: give up, fwd to
                    // next level.
                    incMissCount(pkt);
                    return false;
                }

                blk->setCoherenceBits(CacheBlk::ReadableBit);
            }
        } else if (compressor) {
            // This is an overwrite to an existing block, therefore we need
            // to check for data expansion (i.e., block was compressed with
            // a smaller size, and now it doesn't fit the entry anymore).
            // If that is the case we might need to evict blocks.
            if (!updateCompressionData(blk, pkt->getConstPtr<uint64_t>(),
                writebacks)) {
                invalidateBlock(blk);
                return false;
            }
        }

        // at this point either this is a writeback or a write-through
        // write clean operation and the block is already in this
        // cache, we need to update the data and the block flags
        assert(blk);
        // TODO: the coherent cache can assert that the dirty bit is set
        if (!pkt->writeThrough()) {
            blk->setCoherenceBits(CacheBlk::DirtyBit);
        }
        // nothing else to do; writeback doesn't expect response
        assert(!pkt->needsResponse());

        updateBlockData(blk, pkt, has_old_data);
        DPRINTF(Cache, "%s new state is %s\n", __func__, blk->print());

        incHitCount(pkt);

        // When the packet metadata arrives, the tag lookup will be done while
        // the payload is arriving. Then the block will be ready to access as
        // soon as the fill is done
        HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
        gem5_assert(hyblk,
                "HybridCache should only be configured using HybridSetAssoc");
        HybridSetAssoc* hytags = dynamic_cast<HybridSetAssoc*>(tags);
        gem5_assert(hytags,
        "HybridCache should only be configured using HybridSetAssoc");
        Cycles fillLat = hyblk->isVolatile() ? dataLatency : dataWriteLatency;
        blk->setWhenReady(clockEdge(fillLat) + pkt->headerDelay +
            std::max(cyclesToTicks(tag_latency), (uint64_t)pkt->payloadDelay) +
            hytags->swapBlockingTicksLeft(pkt->getAddr()));

        if (hyblk->isVolatile()) {
            hybrid_stats.volWrites += pkt->getSize();
            hybrid_stats.noOfVolWrites++;
            hybrid_stats.dynEnergy += volWriteEnergy;
        } else {
            hybrid_stats.nonVolWrites += pkt->getSize();
            hybrid_stats.noOfNonVolWrites++;
            hybrid_stats.dynEnergy += nonVolWriteEnergy;
        }
        // If this a write-through packet it will be sent to cache below
        return !pkt->writeThrough();
    } else if (blk && (pkt->needsWritable() ?
            blk->isSet(CacheBlk::WritableBit) :
            blk->isSet(CacheBlk::ReadableBit))) {
        // OK to satisfy access
        incHitCount(pkt);

        HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
        gem5_assert(hyblk,
            "HybridCache should only be configured using HybridSetAssoc");

        // Calculate access latency based on the need to access the data array
        if (pkt->isRead()) {
            lat = calculateAccessLatency(pkt, hyblk);

            // When a block is compressed, it must first be decompressed
            // before being read. This adds to the access latency.
            if (compressor) {
                lat += compressor->getDecompressionLatency(blk);
            }
        } else {
            lat = calculateTagOnlyLatency(pkt->headerDelay, tag_latency);
        }

        satisfyRequest(pkt, blk);
        maintainClusivity(pkt->fromCache(), blk);

        return true;
    }

    // Can't satisfy access normally... either no block (blk == nullptr)
    // or have block but need writable

    incMissCount(pkt);

    HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
    if (blk) {
        gem5_assert(hyblk,
            "HybridCache should only be configured using HybridSetAssoc");
    }
    lat = calculateAccessLatency(pkt, hyblk);

    if (!blk && pkt->isLLSC() && pkt->isWrite()) {
        // complete miss on store conditional... just give up now
        pkt->req->setExtraData(0);
        return true;
    }

    return false;
}

void
HybridCache::satisfyRequest(PacketPtr pkt, CacheBlk *blk,
    bool deferred_response, bool pending_downgrade)
{
    if (blk != tempBlock) {
        HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
        gem5_assert(hyblk,
            "HybridCache should only be configured using HybridSetAssoc");

        if (hyblk->isVolatile()) {
            if (pkt->cmd == MemCmd::SwapReq) {
                hybrid_stats.volWrites += pkt->getSize();
                hybrid_stats.volReads += pkt->getSize();
                hybrid_stats.noOfVolWrites++;
                hybrid_stats.dynEnergy += volWriteEnergy;
                hybrid_stats.noOfVolReads++;
                hybrid_stats.dynEnergy += volReadEnergy;
            } else if (pkt->isRead()) {
                hybrid_stats.volReads += pkt->getSize();
                hybrid_stats.noOfVolReads++;
                hybrid_stats.dynEnergy += volReadEnergy;
            } else if (pkt->isWrite()) {
                hybrid_stats.volWrites += pkt->getSize();
                hybrid_stats.noOfVolWrites++;
                hybrid_stats.dynEnergy += volWriteEnergy;
            }
        } else {
            if (pkt->cmd == MemCmd::SwapReq) {
                hybrid_stats.nonVolWrites += pkt->getSize();
                hybrid_stats.nonVolReads += pkt->getSize();
                hybrid_stats.noOfNonVolWrites++;
                hybrid_stats.dynEnergy += nonVolWriteEnergy;
                hybrid_stats.noOfNonVolReads++;
                hybrid_stats.dynEnergy += nonVolReadEnergy;
            } else if (pkt->isRead()) {
                hybrid_stats.nonVolReads += pkt->getSize();
                hybrid_stats.noOfNonVolReads++;
                hybrid_stats.dynEnergy += nonVolReadEnergy;
            } else if (pkt->isWrite()) {
                hybrid_stats.nonVolWrites += pkt->getSize();
                hybrid_stats.noOfNonVolWrites++;
                hybrid_stats.dynEnergy += nonVolWriteEnergy;
            }
        }
     }
        Cache::satisfyRequest(pkt,blk,deferred_response,pending_downgrade);
}

CacheBlk*
HybridCache::handleFill(PacketPtr pkt, CacheBlk *blk,
                         PacketList &writebacks, bool allocate)
{
    assert(pkt->isResponse());
    Addr addr = pkt->getAddr();
    bool is_secure = pkt->isSecure();
    const bool has_old_data = blk && blk->isValid();
    const std::string old_state = (debug::Cache && blk) ? blk->print() : "";

    // When handling a fill, we should have no writes to this line.
    assert(addr == pkt->getBlockAddr(blkSize));
    assert(!writeBuffer.findMatch(addr, is_secure));

    if (!blk) {
        // better have read new data...
        assert(pkt->hasData() || pkt->cmd == MemCmd::InvalidateResp);

        // need to do a replacement if allocating, otherwise we stick
        // with the temporary storage
        blk = allocate ? allocateBlock(pkt, writebacks) : nullptr;

        if (!blk) {
            // No replaceable block or a mostly exclusive
            // cache... just use temporary storage to complete the
            // current request and then get rid of it
            blk = tempBlock;
            tempBlock->insert(addr, is_secure);
            DPRINTF(Cache, "using temp block for %#llx (%s)\n", addr,
                    is_secure ? "s" : "ns");
        }
    } else {
        // existing block... probably an upgrade
        // don't clear block status... if block is already dirty we
        // don't want to lose that
    }

    // Block is guaranteed to be valid at this point
    assert(blk->isValid());
    assert(blk->isSecure() == is_secure);
    assert(regenerateBlkAddr(blk) == addr);

    blk->setCoherenceBits(CacheBlk::ReadableBit);

    // sanity check for whole-line writes, which should always be
    // marked as writable as part of the fill, and then later marked
    // dirty as part of satisfyRequest
    if (pkt->cmd == MemCmd::InvalidateResp) {
        assert(!pkt->hasSharers());
    }

    // here we deal with setting the appropriate state of the line,
    // and we start by looking at the hasSharers flag, and ignore the
    // cacheResponding flag (normally signalling dirty data) if the
    // packet has sharers, thus the line is never allocated as Owned
    // (dirty but not writable), and always ends up being either
    // Shared, Exclusive or Modified, see Packet::setCacheResponding
    // for more details
    if (!pkt->hasSharers()) {
        // we could get a writable line from memory (rather than a
        // cache) even in a read-only cache, note that we set this bit
        // even for a read-only cache, possibly revisit this decision
        blk->setCoherenceBits(CacheBlk::WritableBit);

        // check if we got this via cache-to-cache transfer (i.e., from a
        // cache that had the block in Modified or Owned state)
        if (pkt->cacheResponding()) {
            // we got the block in Modified state, and invalidated the
            // owners copy
            blk->setCoherenceBits(CacheBlk::DirtyBit);

            gem5_assert(!isReadOnly, "Should never see dirty snoop response "
                        "in read-only cache %s\n", name());

        }
    }

    DPRINTF(Cache, "Block addr %#llx (%s) moving from %s to %s\n",
            addr, is_secure ? "s" : "ns", old_state, blk->print());

    // if we got new data, copy it in (checking for a read response
    // and a response that has data is the same in the end)
    if (pkt->isRead()) {
        // sanity checks
        assert(pkt->hasData());
        assert(pkt->getSize() == blkSize);

        updateBlockData(blk, pkt, has_old_data);
    }
    // The block will be ready when the payload arrives and the fill is done

    if (blk != tempBlock) {
        HybridCacheBlk* hyblk = dynamic_cast<HybridCacheBlk*>(blk);
        gem5_assert(hyblk,
            "HybridCache should only be configured using HybridSetAssoc");
        HybridSetAssoc* hytags = dynamic_cast<HybridSetAssoc*>(tags);
        gem5_assert(hytags,
            "HybridCache should only be configured using HybridSetAssoc");
        Cycles fillLat = hyblk->isVolatile() ? dataLatency : dataWriteLatency;
        blk->setWhenReady(clockEdge(fillLat) + pkt->headerDelay +
            pkt->payloadDelay + hytags->swapBlockingTicksLeft(pkt->getAddr()));

        // Update statistic if response was actually carrying data
        if (pkt->isRead()) {
            if (hyblk->isVolatile()) {
                hybrid_stats.volWrites += blkSize;
                hybrid_stats.noOfVolWrites++;
                hybrid_stats.dynEnergy += volWriteEnergy;
            } else {
                hybrid_stats.nonVolWrites += blkSize;
                hybrid_stats.noOfNonVolWrites++;
                hybrid_stats.dynEnergy += nonVolWriteEnergy;
            }
        }
    } else {
        hybrid_stats.tmpBlockUsages++;
        blk->setWhenReady(clockEdge(dataWriteLatency) + pkt->headerDelay +
                    pkt->payloadDelay);
    }

    return blk;
}

Cycles
HybridCache::handleAtomicReqMiss(PacketPtr pkt, CacheBlk *&blk,
                           PacketList &writebacks)
{
    // deal with the packets that go through the write path of
    // the cache, i.e. any evictions and writes
    if (pkt->isEviction() || pkt->cmd == MemCmd::WriteClean ||
        (pkt->req->isUncacheable() && pkt->isWrite())) {
        Cycles latency = ticksToCycles(memSidePort.sendAtomic(pkt));

        // at this point, if the request was an uncacheable write
        // request, it has been satisfied by a memory below and the
        // packet carries the response back
        assert(!(pkt->req->isUncacheable() && pkt->isWrite()) ||
               pkt->isResponse());

        return latency;
    }

    // only misses left

    PacketPtr bus_pkt = createMissPacket(pkt, blk, pkt->needsWritable(),
                                         pkt->isWholeLineWrite(blkSize));

    bool is_forward = (bus_pkt == nullptr);

    if (is_forward) {
        // just forwarding the same request to the next level
        // no local cache operation involved
        bus_pkt = pkt;
    }

    DPRINTF(Cache, "%s: Sending an atomic %s\n", __func__,
            bus_pkt->print());

    const std::string old_state = blk ? blk->print() : "";

    Cycles latency = ticksToCycles(memSidePort.sendAtomic(bus_pkt));

    bool is_invalidate = bus_pkt->isInvalidate();

    // We are now dealing with the response handling
    DPRINTF(Cache, "%s: Receive response: %s for %s\n", __func__,
            bus_pkt->print(), old_state);

    // If packet was a forward, the response (if any) is already
    // in place in the bus_pkt == pkt structure, so we don't need
    // to do anything.  Otherwise, use the separate bus_pkt to
    // generate response to pkt and then delete it.
    if (!is_forward) {
        if (pkt->needsResponse()) {
            assert(bus_pkt->isResponse());
            if (bus_pkt->isError()) {
                pkt->makeAtomicResponse();
                pkt->copyError(bus_pkt);
            } else if (pkt->isWholeLineWrite(blkSize)) {
                // note the use of pkt, not bus_pkt here.

                // write-line request to the cache that promoted
                // the write to a whole line
                const bool allocate = allocOnFill(pkt->cmd) &&
                    (!writeAllocator || writeAllocator->allocate());
                blk = handleFill(bus_pkt, blk, writebacks, allocate);
                assert(blk != NULL);
                is_invalidate = false;
                satisfyRequest(pkt, blk);
            } else if (bus_pkt->isRead() ||
                       bus_pkt->cmd == MemCmd::UpgradeResp) {
                // we're updating cache state to allow us to
                // satisfy the upstream request from the cache
                blk = handleFill(bus_pkt, blk, writebacks,
                                 allocOnFill(pkt->cmd));
                satisfyRequest(pkt, blk);
                maintainClusivity(pkt->fromCache(), blk);
            } else {
                // we're satisfying the upstream request without
                // modifying cache state, e.g., a write-through
                pkt->makeAtomicResponse();
            }
        }
        delete bus_pkt;
    }

    if (is_invalidate && blk && blk->isValid()) {
        invalidateBlock(blk);
    }

    return latency;
}

void
HybridCache::recvTimingResp(PacketPtr pkt)
{
    assert(pkt->isResponse());

    // all header delay should be paid for by the crossbar, unless
    // this is a prefetch response from above
    panic_if(pkt->headerDelay != 0 && pkt->cmd != MemCmd::HardPFResp,
             "%s saw a non-zero packet delay\n", name());

    const bool is_error = pkt->isError();

    if (is_error) {
        DPRINTF(Cache, "%s: Cache received %s with error\n", __func__,
                pkt->print());
    }

    DPRINTF(Cache, "%s: Handling response %s\n", __func__,
            pkt->print());

    // if this is a write, we should be looking at an uncacheable
    // write
    if (pkt->isWrite()) {
        assert(pkt->req->isUncacheable());
        handleUncacheableWriteResp(pkt);
        return;
    }

    // we have dealt with any (uncacheable) writes above, from here on
    // we know we are dealing with an MSHR due to a miss or a prefetch
    MSHR *mshr = dynamic_cast<MSHR*>(pkt->popSenderState());
    assert(mshr);

    if (mshr == noTargetMSHR) {
        // we always clear at least one target
        clearBlocked(Blocked_NoTargets);
        noTargetMSHR = nullptr;
    }

    // Initial target is used just for stats
    const QueueEntry::Target *initial_tgt = mshr->getTarget();
    const Tick miss_latency = curTick() - initial_tgt->recvTime;
    if (pkt->req->isUncacheable()) {
        assert(pkt->req->requestorId() < system->maxRequestors());
        stats.cmdStats(initial_tgt->pkt)
            .mshrUncacheableLatency[pkt->req->requestorId()] += miss_latency;
    } else {
        assert(pkt->req->requestorId() < system->maxRequestors());
        stats.cmdStats(initial_tgt->pkt)
            .mshrMissLatency[pkt->req->requestorId()] += miss_latency;
    }

    PacketList writebacks;

    bool is_fill = !mshr->isForward &&
        (pkt->isRead() || pkt->cmd == MemCmd::UpgradeResp ||
         mshr->wasWholeLineWrite);

    // make sure that if the mshr was due to a whole line write then
    // the response is an invalidation
    assert(!mshr->wasWholeLineWrite || pkt->isInvalidate());

    CacheBlk *blk = tags->findBlock(pkt->getAddr(), pkt->isSecure());

    if (is_fill && !is_error) {
        DPRINTF(Cache, "Block for addr %#llx being updated in Cache\n",
                pkt->getAddr());

        const bool allocate = (writeAllocator && mshr->wasWholeLineWrite) ?
            writeAllocator->allocate() : mshr->allocOnFill();
        blk = handleFill(pkt, blk, writebacks, allocate);
        assert(blk != nullptr);
        ppFill->notify(pkt);
    }

    if (blk && blk->isValid() && pkt->isClean() && !pkt->isInvalidate()) {
        // The block was marked not readable while there was a pending
        // cache maintenance operation, restore its flag.
        blk->setCoherenceBits(CacheBlk::ReadableBit);

        // This was a cache clean operation (without invalidate)
        // and we have a copy of the block already. Since there
        // is no invalidation, we can promote targets that don't
        // require a writable copy
        mshr->promoteReadable();
    }

    if (blk && blk->isSet(CacheBlk::WritableBit) &&
        !pkt->req->isCacheInvalidate()) {
        // If at this point the referenced block is writable and the
        // response is not a cache invalidate, we promote targets that
        // were deferred as we couldn't guarrantee a writable copy
        mshr->promoteWritable();
    }

    serviceMSHRTargets(mshr, pkt, blk);

    if (mshr->promoteDeferredTargets()) {
        // avoid later read getting stale data while write miss is
        // outstanding.. see comment in timingAccess()
        if (blk) {
            blk->clearCoherenceBits(CacheBlk::ReadableBit);
        }
        mshrQueue.markPending(mshr);
        schedMemSideSendEvent(clockEdge() + pkt->payloadDelay);
    } else {
        // while we deallocate an mshr from the queue we still have to
        // check the isFull condition before and after as we might
        // have been using the reserved entries already
        const bool was_full = mshrQueue.isFull();
        mshrQueue.deallocate(mshr);
        if (was_full && !mshrQueue.isFull()) {
            clearBlocked(Blocked_NoMSHRs);
        }

        // Request the bus for a prefetch if this deallocation freed enough
        // MSHRs for a prefetch to take place
        if (prefetcher && mshrQueue.canPrefetch() && !isBlocked()) {
            Tick next_pf_time = std::max(prefetcher->nextPrefetchReadyTime(),
                                         clockEdge());
            if (next_pf_time != MaxTick)
                schedMemSideSendEvent(next_pf_time);
        }
    }

    // if we used temp block, check to see if its valid and then clear it out
    if (blk == tempBlock && tempBlock->isValid()) {
        BaseCache::evictBlock(blk, writebacks);
    }

    const Tick forward_time = clockEdge(forwardLatency) + pkt->headerDelay;
    // copy writebacks to write buffer
    doWritebacks(writebacks, forward_time);

    DPRINTF(CacheVerbose, "%s: Leaving with %s\n", __func__, pkt->print());
    delete pkt;
}


CacheBlk*
HybridCache::allocateBlock(const PacketPtr pkt, PacketList &writebacks)
{
    // Get address
    const Addr addr = pkt->getAddr();

    // Get secure bit
    const bool is_secure = pkt->isSecure();

    // Block size and compression related access latency. Only relevant if
    // using a compressor, otherwise there is no extra delay, and the block
    // is fully sized
    std::size_t blk_size_bits = blkSize*8;
    Cycles compression_lat = Cycles(0);
    Cycles decompression_lat = Cycles(0);

    // If a compressor is being used, it is called to compress data before
    // insertion. Although in Gem5 the data is stored uncompressed, even if a
    // compressor is used, the compression/decompression methods are called to
    // calculate the amount of extra cycles needed to read or write compressed
    // blocks.
    if (compressor && pkt->hasData()) {
        const auto comp_data = compressor->compress(
            pkt->getConstPtr<uint64_t>(), compression_lat, decompression_lat);
        blk_size_bits = comp_data->getSizeBits();
    }

    // Find replacement victim
    std::vector<CacheBlk*> evict_blks;
    Addr pc = pkt->req->hasPC() ? pkt->req->getPC() : 0;
    HybridSetAssoc* hytags = dynamic_cast<HybridSetAssoc*>(tags);
    gem5_assert(hytags,
        "HybridCache should only be configured using HybridSetAssoc");
    CacheBlk *victim = hytags->findVictim(addr, is_secure, blk_size_bits,
                                        evict_blks, pc);

    // It is valid to return nullptr if there is no victim
    if (!victim)
        return nullptr;

    // Print victim block's information
    DPRINTF(CacheRepl, "Replacement victim: %s\n", victim->print());

    // Try to evict blocks; if it fails, give up on allocation
    if (!handleEvictions(evict_blks, writebacks)) {
        return nullptr;
    }

    // Insert new block at victimized entry
    tags->insertBlock(pkt, victim);

    // If using a compressor, set compression data. This must be done after
    // insertion, as the compression bit may be set.
    if (compressor) {
        compressor->setSizeBits(victim, blk_size_bits);
        compressor->setDecompressionLatency(victim, decompression_lat);
    }

    return victim;
}

Cycles
HybridCache::calculateAccessLatency(PacketPtr pkt,
                    const HybridCacheBlk* blk) const
{
    Cycles lat(0);
    if (blk != nullptr) {
        gem5_assert(pkt->isRead() || pkt->isWrite() || pkt->isUpgrade(),
            "Invalid cache access command: " + pkt->cmdString());
        gem5_assert(!(pkt->isRead() && pkt->isWrite()) ,
            "Cache access that is both read and write");

        Cycles dataAccessLatency =
            (pkt->isRead() ? dataReadLatency : dataWriteLatency);
        dataAccessLatency =
            (blk->isVolatile() ? dataLatency : dataAccessLatency);
        // As soon as the access arrives, for sequential accesses first access
        // tags, then the data entry. In the case of parallel accesses the
        // latency is dictated by the slowest of tag and data latencies.
        if (sequentialAccess) {
            lat = ticksToCycles(pkt->headerDelay)
                + lookupLatency + dataAccessLatency;
        } else {
            lat = ticksToCycles(pkt->headerDelay)
                + std::max(lookupLatency, dataAccessLatency);
        }

        // Check if the block to be accessed is available. If not, apply the
        // access latency on top of when the block is ready to be accessed.
        const Tick tick = curTick() + pkt->headerDelay;
        const Tick when_ready = blk->getWhenReady();
        if (when_ready > tick &&
            ticksToCycles(when_ready - tick) > lat) {
            lat += ticksToCycles(when_ready - tick);
        }

        if (pkt->isRead() && compressor) {
                lat += compressor->getDecompressionLatency(blk);
        }
    } else {
        // In case of a miss, we neglect the data access in a parallel
        // configuration (i.e., the data access will be stopped as soon as
        // we find out it is a miss), and use the tag-only latency.
        lat = calculateTagOnlyLatency(pkt->headerDelay, lookupLatency);
    }

    return lat;

}

bool
HybridCache::awaitMSHRDrain()
{
    // wait for outstanding requests to be satisfied
    if (mshrQueue.drain() == DrainState::Draining) {
        return false;
    } else {
        return true;
    }
}

void
HybridCache::collectWritebacks(PacketList &writebacks) {
    assert(mshrQueue.drain() == DrainState::Drained);
    // mark volatile cache blocks for writeback
    HybridSetAssoc* hytags = dynamic_cast<HybridSetAssoc*>(tags);
    gem5_assert(hytags,
        "HybridCache should only be configured using HybridSetAssoc");
    // Migrate "important" data to the non-volatile section if
    // if there is a policy for that
    hytags->saveImportantData();
    for (HybridCacheBlk* blk : hytags->getVolCacheBlocks()) {
        if (blk->isValid()) {
            BaseCache::evictBlock(blk,writebacks);
        }
    }
    hytags->resetReplData();
}

bool
HybridCache::tryWritebacks(PacketList &writebacks) {
    while (!writebacks.empty()) {
        PacketPtr wbPkt = writebacks.front();
        // We use forwardLatency here because we are copying writebacks to
        // write buffer.

        // Call isCachedAbove for Writebacks, CleanEvicts and
        // WriteCleans to discover if the block is cached above.
        Tick forward_time = clockEdge(forwardLatency) +
                    wbPkt->headerDelay;
        if (isCachedAbove(wbPkt)) {
            if (wbPkt->cmd == MemCmd::CleanEvict) {
                // Delete CleanEvict because cached copies exist above. The
                // packet destructor will delete the request object because
                // this is a non-snoop request packet which does not require a
                // response.
                delete wbPkt;
            } else if (wbPkt->cmd == MemCmd::WritebackClean) {
                // clean writeback, do not send since the block is
                // still cached above
                assert(writebackClean);
                delete wbPkt;
            } else {
                assert(wbPkt->cmd == MemCmd::WritebackDirty ||
                       wbPkt->cmd == MemCmd::WriteClean);
                // Set BLOCK_CACHED flag in Writeback and send below, so that
                // the Writeback does not reset the bit corresponding to this
                // address in the snoop filter below.
                if (writeBuffer.isFull()) {
                    return false;
                }
                wbPkt->setBlockCached();
                allocateWriteBuffer(wbPkt, forward_time);
            }
        } else {
            // If the block is not cached above, send packet below. Both
            // CleanEvict and Writeback with BLOCK_CACHED flag cleared will
            // reset the bit corresponding to this address in the snoop filter
            // below.
            if (writeBuffer.isFull()) {
                return false;
            }
            allocateWriteBuffer(wbPkt, forward_time);
        }
        writebacks.pop_front();
    }
    return true;
}

bool
HybridCache::awaitWriteBufferDrain()
{
    // wait for outstanding writebacks to be performed
    if (writeBuffer.drain() == DrainState::Draining) {
        return false;
    }
    assert(writeBuffer.drain() == DrainState::Drained);
    return true;
}



HybridCache::HybridCacheStats::HybridCacheStats(HybridCache &c)
    : statistics::Group(&c),
    ADD_STAT(nonVolReads, statistics::units::Count::get(),
        "number of bytes read from non-volatile cache blocks"),
    ADD_STAT(volReads, statistics::units::Count::get(),
        "number of bytes read from volatile cache blocks"),
    ADD_STAT(nonVolWrites, statistics::units::Count::get(),
        "number of bytes written to non-volatile cache blocks"),
    ADD_STAT(volWrites, statistics::units::Count::get(),
        "number of bytes written to volatile cache blocks"),
    ADD_STAT(noOfNonVolReads, statistics::units::Count::get(),
        "number of read accesses to non-volatile cache blocks"),
    ADD_STAT(noOfVolReads, statistics::units::Count::get(),
        "number of read accesses to volatile cache blocks"),
    ADD_STAT(noOfNonVolWrites, statistics::units::Count::get(),
        "number of write accesses to non-volatile cache blocks"),
    ADD_STAT(noOfVolWrites, statistics::units::Count::get(),
        "number of write accesses to volatile cache blocks"),
    ADD_STAT(dynEnergy, statistics::units::Joule::get(),
             "Dynamic energy caused by cache accesses (nJ)"),
    ADD_STAT(tmpBlockUsages, statistics::units::Count::get(),
        "number of times the tempBlock was used when handling a response")
{
}
} // namespace gem5
#endif
