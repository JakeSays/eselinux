// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/DataGenerator.hxx"

#include <cstring>

namespace ese::tests
{

DataGenerator::DataGenerator(uint64_t seed)
    : _engine(seed)
{
}

void DataGenerator::Reseed(uint64_t seed)
{
    _engine.seed(seed);
}

uint8_t DataGenerator::NextByte()
{
    return static_cast<uint8_t>(_engine() & 0xFFu);
}

uint16_t DataGenerator::NextWord()
{
    return static_cast<uint16_t>(_engine() & 0xFFFFu);
}

uint32_t DataGenerator::NextDoubleWord()
{
    return static_cast<uint32_t>(_engine() & 0xFFFFFFFFu);
}

uint64_t DataGenerator::NextQuadWord()
{
    return _engine();
}

bool DataGenerator::NextBoolean()
{
    return (_engine() & 1u) != 0u;
}

int8_t DataGenerator::NextSignedByte()
{
    return static_cast<int8_t>(NextByte());
}

int16_t DataGenerator::NextSignedShort()
{
    return static_cast<int16_t>(NextWord());
}

int32_t DataGenerator::NextSignedLong()
{
    return static_cast<int32_t>(NextDoubleWord());
}

int64_t DataGenerator::NextSignedLongLong()
{
    return static_cast<int64_t>(NextQuadWord());
}

float DataGenerator::NextSingle()
{
    // Reinterpret a random bit pattern as float, but keep it finite —
    // scenarios that compare via memcmp don't care about NaN payloads.
    const uint32_t bits = NextDoubleWord() & 0x7F7FFFFFu; // clear sign-of-exponent overflow
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

double DataGenerator::NextDouble()
{
    const uint64_t bits = NextQuadWord() & 0x7FEFFFFF'FFFFFFFFULL;
    double result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::string DataGenerator::NextAsciiString(int length)
{
    // Printable ASCII (32..126) — chosen so debug dumps of failing rows
    // read as readable text rather than control codes.
    static constexpr char PrintableMin = 32;
    static constexpr char PrintableMax = 126;
    static constexpr int PrintableSpan = PrintableMax - PrintableMin + 1;

    std::string result;
    result.resize(static_cast<std::string::size_type>(length));
    for (int index = 0; index < length; ++index)
    {
        result[index] = static_cast<char>(PrintableMin + (NextByte() % PrintableSpan));
    }
    return result;
}

std::wstring DataGenerator::NextUnicodeString(int length)
{
    // Basic Multilingual Plane minus surrogates and control characters.
    static constexpr wchar_t PrintableMin = 0x0020;
    static constexpr wchar_t PrintableMax = 0xD7FF;
    static constexpr int PrintableSpan = PrintableMax - PrintableMin + 1;

    std::wstring result;
    result.resize(static_cast<std::wstring::size_type>(length));
    for (int index = 0; index < length; ++index)
    {
        result[index] = static_cast<wchar_t>(PrintableMin + (NextWord() % PrintableSpan));
    }
    return result;
}

std::vector<uint8_t> DataGenerator::NextBinaryBlob(int length)
{
    std::vector<uint8_t> blob(static_cast<std::vector<uint8_t>::size_type>(length));
    for (int index = 0; index < length; ++index)
    {
        blob[static_cast<size_t>(index)] = NextByte();
    }
    return blob;
}

} // namespace ese::tests
