#if TU_DORTMUND
/*******************************************************************************
* Copyright (c) 2012-2014, The Microsystems Design Labratory (MDL)
* Department of Computer Science and Engineering, The Pennsylvania State University
* All rights reserved.
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
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
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

#ifndef __BITFLIPTRACEWRITER_H__
#define __BITFLIPTRACEWRITER_H__

#include "traceWriter/GenericTraceWriter.h"
#include <string>
#include <iostream>
#include <fstream>
#include <map>
#include <unordered_map>

namespace NVM {

class BitFlipTraceWriter : public GenericTraceWriter
{
  public:
    BitFlipTraceWriter( );
    ~BitFlipTraceWriter( );
    
    void SetTraceFile( std::string file );
    std::string GetTraceFile( );
    
    bool SetNextAccess( TraceLine *nextAccess );
  
  private:
    std::string traceFile;
    std::ofstream trace;

    unsigned long min_address=0;
    unsigned long max_address=0;

    std::map<unsigned long, unsigned long> rbf_map;
    std::map<unsigned long, unsigned long> vbf_map;

    //Simulate WL approaches (RMW os rotate n write)
    std::map<unsigned long, unsigned long> RNW_rbf_map;
    std::map<unsigned long, unsigned long> RNW_vbf_map;
    std::map<unsigned long, unsigned long> RNW_Overhead_rbf_map;
    std::map<unsigned long, unsigned long> RNW_Overhead_vbf_map;

    const unsigned long RNW_word_width=8;
    const unsigned long RNW_line_width=256;
    const unsigned long RNW_threshold=1000;
    unsigned long RNW_counter=0;
    unsigned long RNW_offset=0;

    unsigned long last_cycle;

    //Simulate page based remapping
    //First, we need sampled paged ages
    static std::map<unsigned long, unsigned long> pb_age_map;
    //Then we need a current remapping table
    static std::map<unsigned long, unsigned long> pb_remapping_table;
    unsigned long pb_map_to_page(unsigned long page);
    bool is_mapped(unsigned long page);
    //And finally a bunch of variables and constants
    unsigned long pb_write_count=0;
    const unsigned long pb_write_threshold=100000;
    void pb_swap_with_jungest(unsigned long v_page);
    std::map<unsigned long, unsigned long> pb_rbf_map;
    std::map<unsigned long, unsigned long> pb_vbf_map;
    std::map<unsigned long, unsigned long> pb_Overhead_rbf_map;
    std::map<unsigned long, unsigned long> pb_Overhead_vbf_map;
    static bool pb_age_compare(const std::pair<unsigned long, unsigned long>& a,const std::pair<unsigned long, unsigned long>& b);
    void handle_mapping(unsigned long target_v_page);

    void flush_trace_file();
};

};

#endif
#endif