#ifdef CDNCcimFlag
/**
 * CDNCcim:
 *
 * @file
 * CimFaultInjection definition
 *      Inherited from CimOperationInterface class
 *          overrides the definition of operations for fault injection
 */


#include "cim_fault_injection.hh"

namespace gem5
{
namespace memory
{
CimFaultInjection::CimFaultInjection(const CimFaultInjectionParams &_p)
    : CimOperationInterface(_p)
{
    DPRINTF(CIMDBG, "CimFaultInjection Constructed! this_ptr: %p\n", this);
}
void
CimFaultInjection::AND(
    const std::vector<uint8_t *> &rows, uint8_t *dest,
    const uint8_t &byte_mask)
{
    CimOperationInterface::AND(rows, dest, byte_mask);
}
void
CimFaultInjection::OR(
    const std::vector<uint8_t *> &rows, uint8_t *dest,
    const uint8_t &byte_mask)
{
    CimOperationInterface::OR(rows, dest, byte_mask);
}
void
CimFaultInjection::XOR(
    const std::vector<uint8_t *> &rows, uint8_t *dest,
    const uint8_t &byte_mask)
{
    CimOperationInterface::XOR(rows, dest, byte_mask);
}
void
CimFaultInjection::COPY(
    uint8_t *src, uint8_t *dest, const uint8_t &rotate_left,
    const uint8_t &byte_mask)
{
    CimOperationInterface::COPY(src, dest, rotate_left, byte_mask);
}
void
CimFaultInjection::NOT_COND(
    uint8_t *src, uint8_t *dest, const bool &always_NOT,
    const bool &NOT_if_zero, const uint8_t &byte_mask)
{
    CimOperationInterface::NOT_COND(
        src, dest, always_NOT, NOT_if_zero, byte_mask);
}
} // namespace memory
} // namespace gem5
#endif