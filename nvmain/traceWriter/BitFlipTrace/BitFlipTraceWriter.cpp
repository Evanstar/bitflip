#if TU_DORTMUND
/*******************************************************************************
 * Copyright (c) 2012-2014, The Microsystems Design Labratory (MDL)
 * Department of Computer Science and Engineering, The Pennsylvania State
 *University All rights reserved.
 *
 * This source code is part of NVMain - A cycle accurate timing, bit accurate
 * energy simulator for both volatile (e.g., DRAM) and non-volatile memory
 * (e.g., PCRAM). The source code is free and you can redistribute and/or
 * modify it by providing that the following conditions are met:
 *
 *  1) Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2) Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author list:
 *   Matt Poremba    ( Email: mrp5060 at psu dot edu
 *                     Website: http://www.cse.psu.edu/~poremba/ )
 *******************************************************************************/

#include "traceWriter/BitFlipTrace/BitFlipTraceWriter.h"

#include <algorithm>>

using namespace NVM;

std::map<unsigned long, unsigned long> BitFlipTraceWriter::pb_age_map;
std::map<unsigned long, unsigned long> BitFlipTraceWriter::pb_remapping_table;

BitFlipTraceWriter::BitFlipTraceWriter() {}

BitFlipTraceWriter::~BitFlipTraceWriter() {
    std::cout << "Closing the trace, writing out results" << std::endl;
    flush_trace_file();
}

void BitFlipTraceWriter::SetTraceFile(std::string file) {
    // Note: This function assumes an absolute path is given, otherwise
    // the current directory is used.

    traceFile = file;

    trace.open(traceFile.c_str());

    if (!trace.is_open()) {
        std::cout << "Warning: Could not open trace file " << file
                  << ". Output will be suppressed." << std::endl;
    }

    /* Write version number of this writer. */
    // trace << "NVMV1" << std::endl;
}

std::string BitFlipTraceWriter::GetTraceFile() { return traceFile; }

#define ENLARGE_SIZE 0x1000
#define MIN_BOUND 0x80000000

bool BitFlipTraceWriter::pb_age_compare(
    const std::pair<unsigned long, unsigned long>& a,
    const std::pair<unsigned long, unsigned long>& b) {
    return pb_age_map[a.second] < pb_age_map[b.second];
}

unsigned long BitFlipTraceWriter::pb_map_to_page(unsigned long page) {
    return pb_remapping_table[page];
}

bool BitFlipTraceWriter::is_mapped(unsigned long page) {
    return pb_remapping_table.find(page) != pb_remapping_table.end();
}

void BitFlipTraceWriter::pb_swap_with_jungest(unsigned long v_page) {
    unsigned long curr_p_page = pb_map_to_page(v_page);
    // std::cout << "[" << v_page << " -> " << curr_p_page << "] identified as old"
    //           << std::endl;
    unsigned long youngest_v_page =
        (*std::min_element(pb_remapping_table.begin(), pb_remapping_table.end(),
                           pb_age_compare))
            .first;
    // std::cout << "swapping with [" << youngest_v_page << " -> "
    //           << pb_remapping_table[youngest_v_page] << "]" << std::endl;
    pb_remapping_table[v_page] = pb_remapping_table[youngest_v_page];
    pb_remapping_table[youngest_v_page] = curr_p_page;
    // std::cout << "[" << v_page << " -> " << pb_remapping_table[v_page] << "], ["
    //           << youngest_v_page << " -> "
    //           << pb_remapping_table[youngest_v_page] << "]" << std::endl;
    //Now age the pages
    unsigned long f_pb=pb_remapping_table[v_page]*8;
    unsigned long s_pb=pb_remapping_table[youngest_v_page]*8;
    for(unsigned long i=0;i<4096*8;i++){
        pb_Overhead_vbf_map[f_pb+i]+=1;
        pb_Overhead_vbf_map[s_pb+i]+=1;
        pb_Overhead_rbf_map[f_pb+i]+=1;
        pb_Overhead_rbf_map[s_pb+i]+=1;
    }
}

void BitFlipTraceWriter::handle_mapping(unsigned long target_v_page) {
    if (!is_mapped(target_v_page)) {
        pb_remapping_table[target_v_page] = target_v_page;
        // pb_age_map[target_v_page] = 0;
        // std::cout << "pushing: " << target_v_page << " -> " <<
        // pb_remapping_table[target_v_page] << std::endl;
    }

    pb_write_count++;
    if (pb_write_count > pb_write_threshold) {
        // std::cout << "Remap triggered for " << target_v_page << std::endl;
        pb_write_count = 0;
        pb_age_map[pb_remapping_table[target_v_page]]++;
        pb_swap_with_jungest(target_v_page);
    }
}

bool BitFlipTraceWriter::SetNextAccess(TraceLine* nextAccess) {
    last_cycle = nextAccess->GetCycle();

    unsigned long phys_address = nextAccess->GetAddress().GetPhysicalAddress();

    handle_mapping(phys_address & ~0xFFF);
    if((phys_address & 0xFFF) >= 0xE00){
        handle_mapping((phys_address & ~0xFFF)+0x1000);
    }
    // std::cout << "A: " << std::hex << (phys_address) << std::endl;

    if (nextAccess->GetOperation() == READ) {
        return true;
    }

    NVMDataBlock& data = nextAccess->GetData();
    NVMDataBlock& oldData = nextAccess->GetOldData();

    if (data.GetSize() != oldData.GetSize()) {
        std::cout << "old and new data have different length!!!" << std::endl;
        while (1)
            ;
    }

    for (unsigned long x = 0; x < data.GetSize(); x++) {
        unsigned char od = oldData.GetByte(x);
        unsigned char nd = data.GetByte(x);

        for (unsigned long i = 0; i < 8; i++) {
            unsigned long baddress = (phys_address - min_address + x) * 8 + i;

            if ((od & (1 << i)) != (nd & (1 << i))) {
                // BitFlip
                if (rbf_map.find(baddress) == rbf_map.end()) {
                    rbf_map[baddress] = 0;
                }
                rbf_map[baddress] = rbf_map[baddress] + 1;
            }
            if (vbf_map.find(baddress) == vbf_map.end()) {
                vbf_map[baddress] = 0;
            }
            vbf_map[baddress] = vbf_map[baddress] + 1;

            /**
             * We simulate rotate n write wear-leveling here. This means, within
             * a line granularity, words are being circulary readdressed
             */
            unsigned long RNW_baddress =
                (phys_address - min_address + x + RNW_offset) * 8 + i;
            if ((od & (1 << i)) != (nd & (1 << i))) {
                // BitFlip
                if (RNW_rbf_map.find(RNW_baddress) == RNW_rbf_map.end()) {
                    RNW_rbf_map[RNW_baddress] = 0;
                    RNW_Overhead_rbf_map[RNW_baddress] = 0;
                }
                RNW_rbf_map[RNW_baddress] = RNW_rbf_map[RNW_baddress] + 1;
                RNW_Overhead_rbf_map[RNW_baddress] = RNW_Overhead_rbf_map[RNW_baddress] + 1;
            }
            if (RNW_vbf_map.find(RNW_baddress) == RNW_vbf_map.end()) {
                RNW_vbf_map[RNW_baddress] = 0;
                RNW_Overhead_vbf_map[RNW_baddress] = 0;
            }
            RNW_vbf_map[RNW_baddress] = RNW_vbf_map[RNW_baddress] + 1;
            RNW_Overhead_vbf_map[RNW_baddress] = RNW_Overhead_vbf_map[RNW_baddress] + 1;

            /**
             * Simulate page based WL on top of rotate n write
             */
            // Determine new page base
            unsigned long pb_new_base =
                (pb_remapping_table[(RNW_baddress >> 3) & ~0xFFF]) << 3;
            // Take in the in page offset
            pb_new_base |= (RNW_baddress & 0x7FFF);
            // if(pb_new_base!=RNW_baddress){
            //     std::cout << "mismatch " << std::hex << pb_new_base << " vs. " << std::hex << RNW_baddress << std::endl;
            //     std::cout << std::hex << (RNW_baddress >> 3) << " and mapped " << pb_remapping_table[(RNW_baddress >> 3) & ~0xFFF] << " -> " << is_mapped((RNW_baddress >> 3) & ~0xFFF) << std::endl;
            //     while(1);
            // }
            if ((od & (1 << i)) != (nd & (1 << i))) {
                // BitFlip
                if (pb_rbf_map.find(pb_new_base) == pb_rbf_map.end()) {
                    pb_rbf_map[pb_new_base] = 0;
                    pb_Overhead_rbf_map[pb_new_base] = 0;
                }
                pb_rbf_map[pb_new_base] = pb_rbf_map[pb_new_base] + 1;
                pb_Overhead_rbf_map[pb_new_base] = pb_Overhead_rbf_map[pb_new_base] + 1;
            }
            if (pb_vbf_map.find(pb_new_base) == pb_vbf_map.end()) {
                pb_vbf_map[pb_new_base] = 0;
                pb_Overhead_vbf_map[pb_new_base] = 0;
            }
            pb_vbf_map[pb_new_base] = pb_vbf_map[pb_new_base] + 1;
            pb_Overhead_vbf_map[pb_new_base] = pb_Overhead_vbf_map[pb_new_base] + 1;
        }
    }

    /**
     * Do the RNW wear-leveling logic here
     */
    RNW_counter++;
    if (RNW_counter >= RNW_threshold) {
        RNW_counter = 0;
        RNW_offset += RNW_word_width;
        if (RNW_offset >= RNW_line_width) {
            RNW_offset = 0;
        }
        //We simulate an additional overhead of one more write access for every accessed line
        for (const auto& [key, value] : RNW_Overhead_rbf_map) {
            RNW_Overhead_rbf_map[key]+=1;
        }
        for (const auto& [key, value] : RNW_Overhead_vbf_map) {
            RNW_Overhead_vbf_map[key]+=1;
        }
        for (const auto& [key, value] : pb_Overhead_rbf_map) {
            pb_Overhead_rbf_map[key]+=1;
        }
        for (const auto& [key, value] : pb_Overhead_vbf_map) {
            pb_Overhead_vbf_map[key]+=1;
        }
        // std::cout << "RNW offste updated to " << std::dec << RNW_offset
        //   << std::endl;
    }
    return true;
}

void BitFlipTraceWriter::flush_trace_file() {
    std::cout << "Having " << rbf_map.size() << " bits in the real map and "
              << vbf_map.size() << " in the virtual map" << std::endl;

    /**
     * Output format is virtual bfs, real bfs, wear leveled virtual bfs,
     * wear-leveled real bfs
     */

    unsigned long i = 0;
    unsigned long pct = 0;
    for (const auto& [key, value] : vbf_map) {
        trace << key << ", " << value << ", "
              << (rbf_map.find(key) == rbf_map.end() ? 0 : rbf_map[key]) << ", " // Bare write accesses logic writes, real flips
              << (RNW_vbf_map.find(key) == RNW_vbf_map.end() ? 0 // With rotate n write leveling
                                                             : RNW_vbf_map[key])
              << ", "
              << (RNW_rbf_map.find(key) == RNW_rbf_map.end() ? 0
                                                             : RNW_rbf_map[key])
              << ", "
              << (RNW_Overhead_vbf_map.find(key) == RNW_Overhead_vbf_map.end() ? 0 // With rotate n write and overhead
                                                             : RNW_Overhead_vbf_map[key])
              << ", "
              << (RNW_Overhead_rbf_map.find(key) == RNW_Overhead_rbf_map.end() ? 0
                                                             : RNW_Overhead_rbf_map[key])
              << ", "
              << (pb_vbf_map.find(key) == pb_vbf_map.end() ? 0 // Page based WL
                                                           : pb_vbf_map[key])
              << ", "
              << (pb_rbf_map.find(key) == pb_rbf_map.end() ? 0
                                                           : pb_rbf_map[key])
              << ", "
              << (pb_Overhead_vbf_map.find(key) == pb_Overhead_vbf_map.end() ? 0 // Page based WL with Overhead
                                                           : pb_Overhead_vbf_map[key])
              << ", "
              << (pb_Overhead_rbf_map.find(key) == pb_Overhead_rbf_map.end() ? 0
                                                           : pb_Overhead_rbf_map[key])
              << std::endl;
        i++;
        unsigned long npct = (i * 100) / vbf_map.size();
        if (npct != pct) {
            std::cout << std::dec << npct << "%" << std::endl;
            pct = npct;
        }
    }

    std::cout << "Simulation stopped at memory controller cycle: " << std::dec
              << last_cycle << std::endl;
}
#endif