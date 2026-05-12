// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// DataGenerator — deterministic pseudo-random data for filling rows
// and columns. Backed by std::mt19937_64 so a given (seed, scenario)
// pair always produces the same sequence — failures are reproducible.

#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace ese::tests
{

class DataGenerator
{
public:
    static constexpr uint64_t DefaultSeed = 0xC0FFEE'DEADBEEFULL;

    explicit DataGenerator(uint64_t seed = DefaultSeed);

    void Reseed(uint64_t seed);

    // Raw integer draws.
    uint8_t NextByte();
    uint16_t NextWord();
    uint32_t NextDoubleWord();
    uint64_t NextQuadWord();

    // Typed draws, full range of the type.
    bool NextBoolean();
    int8_t NextSignedByte();
    int16_t NextSignedShort();
    int32_t NextSignedLong();
    int64_t NextSignedLongLong();
    float NextSingle();
    double NextDouble();

    // Sized payloads.
    std::string NextAsciiString(int length);
    std::wstring NextUnicodeString(int length);
    std::vector<uint8_t> NextBinaryBlob(int length);

private:
    std::mt19937_64 _engine;
};

} // namespace ese::tests
