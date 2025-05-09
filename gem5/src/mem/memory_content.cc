#if TU_DORTMUND == 1
#include "mem/memory_content.hh"
#include <algorithm>
#include <bits/stdc++.h>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>

namespace gem5
{
    
    void
    memory_content::addFlipCount(std::bitset<64> set)
    {   
        for (int i = 0; i < 64 ; i++) {
            this->getBitFlips()[i] = this->getBitFlips()[i] + set[i];
        }
    }

    void 
    memory_content::setContent(Addr address, bool cmd, uint8_t *oldContent, uint8_t *newerContent, uint64_t tick)
    {
        this->address = address;
        this->cmd = cmd;
        this->oldContent = oldContent;
        this->newContent = newerContent;
        this->tick = tick;
    }
    
    void
    memory_content::updateContent(Addr address, bool cmd, uint8_t *newerContent, uint64_t curtick)
    {
        std::bitset<64> set((*getNewContent())^(*newerContent));
        addFlipCount(set);
        setTick(curtick);
        setCmd(cmd);
        setOldContent(getNewContent());
        setNewContent(newerContent);
    }
} //namespace gem5
#endif
