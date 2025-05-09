/**
 * CDNCcim:
 *
 * @file
 * CimFaultInjection declaration
 *      Inherited from CimOperationInterface class
 *          overrides the definition of operations for fault injection
 */

#ifdef CDNCcimFlag

#ifndef __CIMCDNC_FAULT_INJECTION_HH__
#define __CIMCDNC_FAULT_INJECTION_HH__

#include "cim_operation_interface.hh"
#include "params/CimFaultInjection.hh"

namespace gem5
{
namespace memory
{

class CimFaultInjection : public CimOperationInterface
{
    const uint8_t cim_WORD_SIZE { 8 }; // Byte

  public:
    CimFaultInjection(const CimFaultInjectionParams &_p);
    void AND(
        const std::vector<uint8_t *> &rows, uint8_t *dest,
        const uint8_t &byte_mask) override;
    void OR(
        const std::vector<uint8_t *> &rows, uint8_t *dest,
        const uint8_t &byte_mask) override;
    void XOR(
        const std::vector<uint8_t *> &rows, uint8_t *dest,
        const uint8_t &byte_mask) override;

    void COPY(
        uint8_t *src, uint8_t *dest, const uint8_t &rotate_left,
        const uint8_t &byte_mask) override;
    void NOT_COND(
        uint8_t *src, uint8_t *dest, const bool &always_NOT,
        const bool &NOT_if_zero, const uint8_t &byte_mask) override;
};
} // namespace memory
} // namespace gem5

#endif //__CIMCDNC_FAULT_INJECTION_HH__
#endif // CDNCcimFlag
