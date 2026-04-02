#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tt::service::reticulum::msgpack {

bool appendNil(std::vector<uint8_t>& output);
bool appendBool(std::vector<uint8_t>& output, bool value);
bool appendUint(std::vector<uint8_t>& output, uint64_t value);
bool appendInt(std::vector<uint8_t>& output, int64_t value);
bool appendDouble(std::vector<uint8_t>& output, double value);
bool appendBin(std::vector<uint8_t>& output, const uint8_t* data, size_t size);
bool appendBin(std::vector<uint8_t>& output, const std::vector<uint8_t>& data);
bool appendString(std::vector<uint8_t>& output, std::string_view value);
bool appendArrayHeader(std::vector<uint8_t>& output, size_t count);
bool appendMapHeader(std::vector<uint8_t>& output, size_t count);

class Reader final {

    const std::vector<uint8_t>& input;
    size_t offset = 0;

    bool readMarker(uint8_t& marker);
    bool readBytes(uint8_t* output, size_t size);
    bool readLength(uint8_t marker, uint8_t fixBase, uint8_t fixMask, uint32_t& length);

public:

    explicit Reader(const std::vector<uint8_t>& input) : input(input) {}

    bool atEnd() const { return offset == input.size(); }
    size_t remaining() const { return input.size() - offset; }
    size_t getOffset() const { return offset; }
    void setOffset(size_t newOffset) { offset = newOffset <= input.size() ? newOffset : input.size(); }

    bool readNil();
    bool readBool(bool& value);
    bool readUint(uint64_t& value);
    bool readInt(int64_t& value);
    bool readDouble(double& value);
    bool readBin(std::vector<uint8_t>& value);
    bool readString(std::string& value);
    bool readArrayHeader(size_t& count);
    bool readMapHeader(size_t& count);
    bool skip();
};

} // namespace tt::service::reticulum::msgpack
