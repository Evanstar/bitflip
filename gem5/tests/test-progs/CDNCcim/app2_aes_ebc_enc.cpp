#include "cim_api.hpp"

#include <fcntl.h>

#include <cstdio>
#include <iostream>
#include <random>

#include "gem5/m5ops.h"

// #include "aes/aes.h"
#include "aes/aes.c"

using namespace std;

#if !defined(NUM_ROWS) || NUM_ROWS <= 0
#define NUM_ROWS (1)
#endif

#define NUM_BATCH (512) // per row

#if !defined(inCIM) || inCIM == 0
#define CPU
#else
#undef CPU
#endif

uint8_t Inputs[NUM_ROWS][NUM_BATCH][16] = { 0 };
uint8_t Outputs[NUM_ROWS][NUM_BATCH][16] = { 0 };
uint8_t myKey[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };

#ifdef CPU
void
cpu()
{
    printf("AES-CPU\trow numbers: %d\n", NUM_ROWS);
#ifdef CHECKPOINT_FI
    m5_checkpoint(0, 0);
#endif // CHECKPOINT_FI
    for (size_t R = 0; R < NUM_ROWS; R++) {
        for (size_t B = 0; B < NUM_BATCH; B++) {
            AES128_ECB_encrypt(&Inputs[R][B][0], myKey, &Outputs[R][B][0]);
        }
    }
#ifdef CHECKPOINT_FI
    FILE *M5fp;
    M5fp = fopen("OUTPUTS/output_AES_CPU.txt", "w");
    for (size_t R = 0; R < NUM_ROWS; R++) {
        for (size_t B = 0; B < NUM_BATCH; B++) {
            for (uint32_t ix = 0; ix < 16; ix++) {
                fprintf(M5fp, "%02x", Outputs[R][B][ix]);
            }
        }
        fprintf(M5fp, "\n");
    }
    fflush(M5fp);
    fclose(M5fp);
    m5_switch_cpu();
    write_file("OUTPUTS/output_AES_CPU.txt");
    m5_exit(0);
#elif defined(STD_PRINT_OUTPUT)
    for (size_t R = 0; R < NUM_ROWS; R++) {
        for (size_t B = 0; B < NUM_BATCH; B++) {
            for (uint32_t ix = 0; ix < 16; ix++) {
                printf("%02x", Outputs[R][B][ix]);
            }
        }
        printf("\n");
    }
#endif // CHECKPOINT_FI
}
#else
CimModule cimModule;
uint8_t in80[DEFAULT_ROW_SIZE_BYTE] = { 0 };
uint8_t inFE[DEFAULT_ROW_SIZE_BYTE] = { 0 };
uint8_t in1B[DEFAULT_ROW_SIZE_BYTE] = { 0 };

uint8_t StateTemp[DEFAULT_ROW_SIZE_BYTE] = { 0 };

uint8_t rowTemp[DEFAULT_ROW_SIZE_BYTE] = { 0 };

void
rotateLeft32bit(const uint8_t &rotateLeft32, const uint8_t &dest)
{
    assert(rotateLeft32 <= 32);
    cimModule.COPY(17, 16, rotateLeft32, 0x0f);
    cimModule.COPY(18, 16, (32 + rotateLeft32), 0x0f);
    cimModule.COPY(19, 16, rotateLeft32, 0xf0);
    cimModule.COPY(20, 16, (32 + rotateLeft32), 0xf0);
    cimModule.OR({ 17, 18, 19, 20 });
    cimModule.COPY(dest);
}

void
cim()
{
    printf("AES-CIM\trow numbers: %d\n", NUM_ROWS);

    for (uint32_t i = 0; i < DEFAULT_ROW_SIZE_BYTE; ++i) {
        in80[i] = 0x80;
        inFE[i] = 0xFE;
        in1B[i] = 0x1B;
    }

    cimModule.copy_to_cim(0, inFE);
    cimModule.copy_to_cim(1, in80);
    cimModule.copy_to_cim(2, in1B);

    Key = myKey;
    KeyExpansion();

    for (size_t row = 0; row < 11; ++row) {
        for (size_t B = 0; B < NUM_BATCH; ++B) {
            std::memcpy(&rowTemp[B * 16], &RoundKey[16 * row], 16);
        }
        cimModule.copy_to_cim(32 + row, rowTemp);
    }

    for (size_t F = 0; F < NUM_ROWS; ++F) {
        cimModule.copy_to_cim(3, &Inputs[F][0][0]);

        // Copy my_roundKey(0)
        cimModule.COPY(4, 32);
        cimModule.XOR({ 3, 4 });
        cimModule.COPY(3);

        for (size_t my_round = 1; my_round < Nr; ++my_round) {
            cimModule.copy_to_cpu(StateTemp, 3);
            for (size_t my_columnInx = 0; my_columnInx < DEFAULT_ROW_SIZE_BYTE;
                 my_columnInx += 16) {
                state = (state_t *)(StateTemp + my_columnInx);
                SubBytes();
                ShiftRows();
            }
            cimModule.copy_to_cim(3, StateTemp);

            //****************** MixColumns();

            cimModule.COPY(8, 3);
            cimModule.COPY(16, 3);
            rotateLeft32bit(0x18, 9);
            rotateLeft32bit(0x10, 10);
            rotateLeft32bit(0x08, 11);
            //
            cimModule.XOR({ 8, 9 });
            cimModule.COPY(5); // TM
            cimModule.XOR({ 8, 9, 10, 11 });
            cimModule.COPY(6); // TMP

            // TM = xtime(TM)      -->   ((x<<1) ^ (((x>>7) & 1) * 0x1b));
            //  TM<<1
            cimModule.COPY(16, 5);
            rotateLeft32bit(1, 7);
            // TM <<1  & 0xfe
            cimModule.AND({ 7, 0 });
            cimModule.COPY(12);

            // TM & 0x80
            cimModule.AND({ 5, 1 });
            cimModule.NOT_COND(13, 256, false, false);

            // ... * 0x1B
            cimModule.AND({ 13, 2 });
            cimModule.COPY(14);

            // r12 ^ r14
            cimModule.XOR({ 12, 14 });
            cimModule.COPY(5); // --> TM (r5)

            // TM = TMP ^ TM
            cimModule.XOR({ 5, 6 });
            cimModule.COPY(5); // --> TM (r5)

            // state ^= TM
            cimModule.XOR({ 5, 3 });
            cimModule.COPY(3); // --> state (r3)

            //************** Addmy_roundKey(my_round);
            cimModule.COPY(4, 32 + my_round);
            cimModule.XOR({ 3, 4 });
            cimModule.COPY(3);
        }
        cimModule.copy_to_cpu(StateTemp, 3);
        for (size_t my_columnInx = 0; my_columnInx < DEFAULT_ROW_SIZE_BYTE;
             my_columnInx += 16) {
            state = (state_t *)(StateTemp + my_columnInx);
            SubBytes();
            ShiftRows();
        }
        cimModule.copy_to_cim(3, StateTemp);
        //************** Addmy_roundKey(Nr);
        cimModule.COPY(4, 32 + Nr);
        cimModule.XOR({ 3, 4 });
        cimModule.COPY(3);
        cimModule.copy_to_cpu(&Outputs[F][0][0], 3);
    }

#ifdef CHECKPOINT_FI
    FILE *M5fp;
    M5fp = fopen("OUTPUTS/output_AES_CIM.txt", "w");
    for (size_t R = 0; R < NUM_ROWS; R++) {
        for (size_t B = 0; B < NUM_BATCH; B++) {
            for (uint32_t ix = 0; ix < 16; ix++) {
                fprintf(M5fp, "%02x", Outputs[R][B][ix]);
            }
        }
        fprintf(M5fp, "\n");
    }
    fflush(M5fp);
    fclose(M5fp);
    m5_exit(0);
#elif defined(STD_PRINT_OUTPUT)
    for (size_t R = 0; R < NUM_ROWS; R++) {
        for (size_t B = 0; B < NUM_BATCH; B++) {
            for (uint32_t ix = 0; ix < 16; ix++) {
                printf("%02x", Outputs[R][B][ix]);
            }
        }
        printf("\n");
    }
#endif // CHECKPOINT_FI
}
#endif // CPU

int
main(int argc, char *argv[])
{
    random_device seed_source; // a seed source for the random number engine
    mt19937 gen(0);            // mersenne_twister_engine seeded with rd()
    uniform_int_distribution<uint8_t> distrib(0, 0xff);

    for (size_t R = 0; R < NUM_ROWS; R++) {
        for (size_t B = 0; B < NUM_BATCH; B++) {
            for (uint8_t ix = 0; ix < 16; ix++) {
                Inputs[R][B][ix] = distrib(gen);
            }
        }
    }

#ifdef CPU
    cpu();
#else
    cim();
#endif // CPU

    return 0;
}
