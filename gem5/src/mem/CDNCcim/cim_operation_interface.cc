#ifdef CDNCcimFlag
/**
 * CDNCcim:
 *
 * @file
 * CimOperationInterface definition
 *      base class for CIM operations
 */

#include "cim_operation_interface.hh"

namespace gem5
{
namespace memory
{
CimOperationInterface::CimOperationInterface(
    const CimOperationInterfaceParams &params)
    : SimObject(params)
{
    DPRINTF(CIMDBG, "CimOperationInterface Constructed! this_ptr: %p\n", this);
}

void
CimOperationInterface::copyCPUtoCimMEM(
    const PacketPtr pkt, uint8_t *host_mem_addr)
{
    pkt->writeData(host_mem_addr);
}

void
CimOperationInterface::copyCimMEMtoCPU(
    PacketPtr pkt, const uint8_t *host_mem_addr)
{
    pkt->setData(host_mem_addr);
}
void
CimOperationInterface::AND(
    const std::vector<uint8_t *> &rows, uint8_t *dest,
    const uint8_t &byte_mask)
{
    for (uint16_t byte_select = 0; byte_select < cim_WORD_SIZE;
         byte_select++) {
        if (byte_mask & (1u << byte_select)) {
            uint8_t result = 0xffu;
            for (uint8_t *row : rows) {
                result &= row[byte_select];
            }
            dest[byte_select] = result;
        }
    }
}
void
CimOperationInterface::OR(
    const std::vector<uint8_t *> &rows, uint8_t *dest,
    const uint8_t &byte_mask)
{
    for (uint16_t byte_select = 0; byte_select < cim_WORD_SIZE;
         byte_select++) {
        if (byte_mask & (1u << byte_select)) {
            uint8_t result = 0x00u;
            for (uint8_t *row : rows) {
                result |= row[byte_select];
            }
            dest[byte_select] = result;
        }
    }
}
void
CimOperationInterface::XOR(
    const std::vector<uint8_t *> &rows, uint8_t *dest,
    const uint8_t &byte_mask)
{
    for (uint16_t byte_select = 0; byte_select < cim_WORD_SIZE;
         byte_select++) {
        if (byte_mask & (1u << byte_select)) {
            uint8_t result = 0x00u;
            for (uint8_t *row : rows) {
                result ^= row[byte_select];
            }
            dest[byte_select] = result;
        }
    }
}
void
CimOperationInterface::COPY(
    uint8_t *src, uint8_t *dest, const uint8_t &rotate_left,
    const uint8_t &byte_mask)
{
    uint64_t tmp = 0;
    for (uint16_t byte_select = 0; byte_select < cim_WORD_SIZE;
         byte_select++) {
        if (byte_mask & (1u << byte_select)) {
            tmp |= (src[byte_select] & 0xfful) << (8 * byte_select);
        }
    }
    if ((rotate_left > 0u) && (rotate_left < 64u)) {
        tmp = (tmp << rotate_left) | (tmp >> (64 - rotate_left));
    }
    for (uint16_t byte_select = 0; byte_select < cim_WORD_SIZE;
         byte_select++) {
        if (byte_mask & (1u << byte_select)) {
            dest[byte_select] = (tmp >> (byte_select * 8)) & 0xfful;
        }
    }
}
void
CimOperationInterface::NOT_COND(
    uint8_t *src, uint8_t *dest, const bool &always_NOT,
    const bool &NOT_if_zero, const uint8_t &byte_mask)
{
    for (uint16_t byte_select = 0; byte_select < cim_WORD_SIZE;
         byte_select++) {
        if (byte_mask & (1u << byte_select)) {
            if (always_NOT) {
                dest[byte_select] = ~src[byte_select];
            } else {
                if (NOT_if_zero) {
                    if (src[byte_select] == 0u)
                        dest[byte_select] = 0xff;
                    else
                        dest[byte_select] = 0x00;
                } else {
                    if (src[byte_select] != 0u)
                        dest[byte_select] = 0xff;
                    else
                        dest[byte_select] = 0x00;
                }
            }
        }
    }
}

} // namespace memory
} // namespace gem5
#endif