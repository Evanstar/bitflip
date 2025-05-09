/**
 * CDNCcim:
 *
 * @file
 * CimOperationInterface declaration
 *      base class for CIM operations
 */

#ifdef CDNCcimFlag

#ifndef __CIMCDNC_OPERATION_INTERFACE_HH__
#define __CIMCDNC_OPERATION_INTERFACE_HH__

#include "debug/CIMDBG.hh"
#include "mem/packet_access.hh"
#include "params/CimOperationInterface.hh"
#include "sim/sim_object.hh"

namespace gem5
{
namespace memory
{

class CimOperationInterface : public SimObject
{
  protected:
    const uint8_t cim_WORD_SIZE { 8 }; // Byte

  public:
    CimOperationInterface(const CimOperationInterfaceParams &_p);

    virtual void copyCPUtoCimMEM(const PacketPtr pkt, uint8_t *host_mem_addr);
    virtual void copyCimMEMtoCPU(PacketPtr pkt, const uint8_t *host_mem_addr);
    //
    virtual void AND(
        const std::vector<uint8_t *> &rows, uint8_t *dest,
        const uint8_t &byte_mask);
    virtual void OR(
        const std::vector<uint8_t *> &rows, uint8_t *dest,
        const uint8_t &byte_mask);
    virtual void XOR(
        const std::vector<uint8_t *> &rows, uint8_t *dest,
        const uint8_t &byte_mask);
    //
    virtual void COPY(
        uint8_t *src, uint8_t *dest, const uint8_t &rotate_left,
        const uint8_t &byte_mask);
    virtual void NOT_COND(
        uint8_t *src, uint8_t *dest, const bool &always_NOT,
        const bool &NOT_if_zero, const uint8_t &byte_mask);
};
} // namespace memory
} // namespace gem5

#endif //__CIMCDNC_OPERATION_INTERFACE_HH__
#endif // CDNCcimFlag
