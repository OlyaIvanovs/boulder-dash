#include <stdio.h>
#include <stdint.h>


int main() {
    uint32_t number = 0xAABBCCDD;
    uint8_t *bytes = (uint8_t *) &number;
    if (bytes[0] == 0xAA) {
        printf("big endian\n");
    } else {
        printf("little endian\n");
    }
}


// Big endian and little endian. Endianness.

// void *data = get_some_data();

// u8 *bytes = (u8 *)data;

// bytes[2] - 3

// -> FF AA BB CC FF CC BB 00 FF 11 CC 00 FF BB C3

// 0x00FFAAFF

// u32 *pixels = (u32 *)data;
// u32 pixel = pixels[0];

// pixel = 0xFFAABBCC  // big-endian
// pixel = 0xCCBBAAFF  // little-endian


// // Little endian

// -> DD CC BB AA FF FF FF FF

// u32 *big_integer = ..   // points to DD, = 0xAABBCCDD
// u16 *small_integer = (u16 *)big_integer;  // points also to DD, = 0xCCDD


// // Big endian

// -> AA BB CC DD ...

// u32 *big_integer = ..   // points to AA, = 0xAABBCCDD
// u16 *small_integer = (u16 *)big_integer;  // points to CC, = 0xCCDD