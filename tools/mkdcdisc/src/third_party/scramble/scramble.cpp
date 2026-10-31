//
// Based on Marcus Comstedt's tool (Public Domain)
// from https://mc.pp.se/dc/files/scramble.c
//

#include "scramble.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <string.h>

// 2MB Max Chunk
const int MAX_CHUNK = (2 * 1024 * 1024);
const int SLICE_SIZE = 32;

std::vector<char> scramble(std::vector<char>& unscrambled_bin)
{
    // seed value is initialized with chunk size
    unsigned int seed = unscrambled_bin.size() & 0xffff;

    // Returns next rand value used for scramble algo
    auto rand = [&seed]() -> unsigned int
    {
        seed = (seed * 2109 + 9273) & 0x7fff;
        return (seed + 0xc000) & 0xffff;
    };

    // Lambda to do slicing and restitching of chunks
    auto reorder_slices = [rand](char * dest, const char* src, int chunk_size)
    {
        // Number of 32byte slices in chunk
        int num_slices = chunk_size / SLICE_SIZE;

        // Array to hold slice index mapping
        int idx[MAX_CHUNK / SLICE_SIZE];
        // Initialize Array with Indexs
        for (int i = 0; i < num_slices; i++)
            idx[i] = i;

        int slice_index = 0;
        // Start at last slice of chunk
        for (int i = num_slices - 1; i >= 0; --i)
        {   
            // Get new index using rand algo
            int x = (rand() * i) >> 16;

            // Swap new and old indexs
            std::swap(idx[i], idx[x]);

            // Find the location of the slice inside the chunk
            // that we want to copy
            const char * start = &src[SLICE_SIZE * idx[i]];
            
            // Write slice to output buffer
            // slice_index stays below num_slices, so the writes span exactly
            // chunk_size, which the caller only passes while that much remains.
            // nosemgrep: insecure-use-memcpy-fn
            memcpy(&dest[slice_index * SLICE_SIZE], start, SLICE_SIZE);
            slice_index++;
        }
    };

    // Start attempting to read max chunk of 2MB and reorder slices in chunk
    // When remaining data is <2MB then shrink the chunk size down a power of 2
    // Example: My binary for 2ndmix is 660816 bytes
    //      chunk_size = 2MB    None                660816B Remaining
    //      chunk_size = 1MB    None                660816B Remaining
    //      chunk_size = 512KB  1 Chunk Sliced      136528B Remaining
    //      chunk_size = 256KB  None                136528B Remaining
    //      chunk_size = 128KB  1 Chunk Sliced      5456B   Remaining
    //      chunk_size = 64KB   None                5456B   Remaining
    //      chunk_size = 32KB   None                5456B   Remaining
    //      chunk_size = 16KB   None                5456B   Remaining
    //      chunk_size = 8KB    None                5456B   Remaining
    //      chunk_size = 4KB    1 Chunk Sliced      1360B   Remaining
    //      chunk_size = 2KB    None                1360B   Remaining
    //      chunk_size = 1KB    1 Chunk Sliced      336B    Remaining
    //      chunk_size = 512B   None                336B    Remaining
    //      chunk_size = 256B   1 Chunk Sliced      80B     Remaining
    //      chunk_size = 128B   None                80B     Remaining
    //      chunk_size = 64B    1 Chunk Sliced      16B     Remaining
    //      chunk_size = 32B    None                16B     Remaining
    //      Write Remaining 16B without slicing since <32B slice size

    
    int size = unscrambled_bin.size();
    std::vector<char> scrambled_bin(size);
    
    int pos = 0;
    // Loop though each valid chunk size starting at 2MB and
    // decreasing by powers of 2 down to the slice min (32B)
    for (int chunk_size = MAX_CHUNK; chunk_size >= SLICE_SIZE; chunk_size >>= 1)
    {
        while (size >= chunk_size)
        {
            reorder_slices(&scrambled_bin[pos], &unscrambled_bin[pos], chunk_size);
            pos += chunk_size;
            size -= chunk_size;
        }
    }

    // Save final partial slice if any data is remaining
    if (size)
    {
        // size is what the chunk loop left over, and both vectors were sized
        // from the same input length. nosemgrep: insecure-use-memcpy-fn
        memcpy(&scrambled_bin[pos], &unscrambled_bin[pos], size);
    }

    return scrambled_bin;
}

std::vector<char> unscramble(std::vector<char>& scrambled_bin)
{
    // seed value is initialized with chunk size
    unsigned int seed = scrambled_bin.size() & 0xffff;

    // Returns next rand value used for scramble algo
    auto rand = [&seed]() -> unsigned int
    {
        seed = (seed * 2109 + 9273) & 0x7fff;
        return (seed + 0xc000) & 0xffff;
    };

    // Lambda to undo slicing and restitching of chunks
    auto restore_slices = [rand](char * dest, const char* src, int chunk_size)
    {
        // Number of 32byte slices in chunk
        int num_slices = chunk_size / SLICE_SIZE;

        // Array to hold slice index mapping
        int idx[MAX_CHUNK / SLICE_SIZE];
        // Initialize Array with Indexs
        for (int i = 0; i < num_slices; i++)
            idx[i] = i;

        int slice_index = 0;
        // Start at last slice of chunk
        for (int i = num_slices - 1; i >= 0; --i)
        {
            // Get new index using rand algo
            int x = (rand() * i) >> 16;

            // Swap new and old indexs
            std::swap(idx[i], idx[x]);

            // Find the location inside the chunk where this
            // sequentially-read slice belongs
            char * dst = &dest[SLICE_SIZE * idx[i]];

            // Read slice sequentially from scrambled input,
            // write it back to its original position
            // Bounded as in scramble()'s reorder_slices above.
            // nosemgrep: insecure-use-memcpy-fn
            memcpy(dst, &src[slice_index * SLICE_SIZE], SLICE_SIZE);
            slice_index++;
        }
    };

    int size = scrambled_bin.size();
    std::vector<char> unscrambled_bin(size);

    int pos = 0;
    // Same chunk-size progression as scramble(): 2MB down to 32B
    for (int chunk_size = MAX_CHUNK; chunk_size >= SLICE_SIZE; chunk_size >>= 1)
    {
        while (size >= chunk_size)
        {
            restore_slices(&unscrambled_bin[pos], &scrambled_bin[pos], chunk_size);
            pos += chunk_size;
            size -= chunk_size;
        }
    }

    // Restore final partial slice if any data is remaining
    if (size)
    {
        // size is what the chunk loop left over, and both vectors were sized
        // from the same input length. nosemgrep: insecure-use-memcpy-fn
        memcpy(&unscrambled_bin[pos], &scrambled_bin[pos], size);
    }

    return unscrambled_bin;
}
