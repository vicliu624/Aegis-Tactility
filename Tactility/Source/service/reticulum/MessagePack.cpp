#include <Tactility/service/reticulum/MessagePack.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace tt::service::reticulum::msgpack {

namespace {

bool appendRaw(std::vector<uint8_t>& output, const uint8_t* data, size_t size) {
    if (data == nullptr && size != 0) {
        return false;
    }

    output.insert(output.end(), data, data + size);
    return true;
}

void appendUint16(std::vector<uint8_t>& output, uint16_t value) {
    output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendUint32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    output.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendUint64(std::vector<uint8_t>& output, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

bool readUint16(const std::vector<uint8_t>& input, size_t& offset, uint16_t& value) {
    if (offset + 2 > input.size()) {
        return false;
    }

    value = static_cast<uint16_t>((input[offset] << 8) | input[offset + 1]);
    offset += 2;
    return true;
}

bool readUint32(const std::vector<uint8_t>& input, size_t& offset, uint32_t& value) {
    if (offset + 4 > input.size()) {
        return false;
    }

    value = (static_cast<uint32_t>(input[offset]) << 24)
        | (static_cast<uint32_t>(input[offset + 1]) << 16)
        | (static_cast<uint32_t>(input[offset + 2]) << 8)
        | static_cast<uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

bool readUint64(const std::vector<uint8_t>& input, size_t& offset, uint64_t& value) {
    if (offset + 8 > input.size()) {
        return false;
    }

    value = 0;
    for (size_t index = 0; index < 8; index++) {
        value = (value << 8) | input[offset + index];
    }
    offset += 8;
    return true;
}

} // namespace

bool appendNil(std::vector<uint8_t>& output) {
    output.push_back(0xC0);
    return true;
}

bool appendBool(std::vector<uint8_t>& output, bool value) {
    output.push_back(value ? 0xC3 : 0xC2);
    return true;
}

bool appendUint(std::vector<uint8_t>& output, uint64_t value) {
    if (value <= 0x7F) {
        output.push_back(static_cast<uint8_t>(value));
    } else if (value <= 0xFF) {
        output.push_back(0xCC);
        output.push_back(static_cast<uint8_t>(value));
    } else if (value <= 0xFFFF) {
        output.push_back(0xCD);
        appendUint16(output, static_cast<uint16_t>(value));
    } else if (value <= 0xFFFFFFFFULL) {
        output.push_back(0xCE);
        appendUint32(output, static_cast<uint32_t>(value));
    } else {
        output.push_back(0xCF);
        appendUint64(output, value);
    }

    return true;
}

bool appendInt(std::vector<uint8_t>& output, int64_t value) {
    if (value >= 0) {
        return appendUint(output, static_cast<uint64_t>(value));
    }

    if (value >= -32) {
        output.push_back(static_cast<uint8_t>(value));
    } else if (value >= INT8_MIN) {
        output.push_back(0xD0);
        output.push_back(static_cast<uint8_t>(value));
    } else if (value >= INT16_MIN) {
        output.push_back(0xD1);
        appendUint16(output, static_cast<uint16_t>(value));
    } else if (value >= INT32_MIN) {
        output.push_back(0xD2);
        appendUint32(output, static_cast<uint32_t>(value));
    } else {
        output.push_back(0xD3);
        appendUint64(output, static_cast<uint64_t>(value));
    }

    return true;
}

bool appendDouble(std::vector<uint8_t>& output, double value) {
    output.push_back(0xCB);

    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    appendUint64(output, bits);
    return true;
}

bool appendBin(std::vector<uint8_t>& output, const uint8_t* data, size_t size) {
    if (size <= 0xFF) {
        output.push_back(0xC4);
        output.push_back(static_cast<uint8_t>(size));
    } else if (size <= 0xFFFF) {
        output.push_back(0xC5);
        appendUint16(output, static_cast<uint16_t>(size));
    } else {
        output.push_back(0xC6);
        appendUint32(output, static_cast<uint32_t>(size));
    }

    return appendRaw(output, data, size);
}

bool appendBin(std::vector<uint8_t>& output, const std::vector<uint8_t>& data) {
    return appendBin(output, data.data(), data.size());
}

bool appendString(std::vector<uint8_t>& output, std::string_view value) {
    if (value.size() <= 31) {
        output.push_back(static_cast<uint8_t>(0xA0 | value.size()));
    } else if (value.size() <= 0xFF) {
        output.push_back(0xD9);
        output.push_back(static_cast<uint8_t>(value.size()));
    } else if (value.size() <= 0xFFFF) {
        output.push_back(0xDA);
        appendUint16(output, static_cast<uint16_t>(value.size()));
    } else {
        output.push_back(0xDB);
        appendUint32(output, static_cast<uint32_t>(value.size()));
    }

    return appendRaw(output, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

bool appendArrayHeader(std::vector<uint8_t>& output, size_t count) {
    if (count <= 15) {
        output.push_back(static_cast<uint8_t>(0x90 | count));
    } else if (count <= 0xFFFF) {
        output.push_back(0xDC);
        appendUint16(output, static_cast<uint16_t>(count));
    } else {
        output.push_back(0xDD);
        appendUint32(output, static_cast<uint32_t>(count));
    }

    return true;
}

bool appendMapHeader(std::vector<uint8_t>& output, size_t count) {
    if (count <= 15) {
        output.push_back(static_cast<uint8_t>(0x80 | count));
    } else if (count <= 0xFFFF) {
        output.push_back(0xDE);
        appendUint16(output, static_cast<uint16_t>(count));
    } else {
        output.push_back(0xDF);
        appendUint32(output, static_cast<uint32_t>(count));
    }

    return true;
}

bool Reader::readMarker(uint8_t& marker) {
    if (offset >= input.size()) {
        return false;
    }

    marker = input[offset++];
    return true;
}

bool Reader::readBytes(uint8_t* output, size_t size) {
    if (offset + size > input.size() || (output == nullptr && size != 0)) {
        return false;
    }

    std::copy_n(input.begin() + offset, size, output);
    offset += size;
    return true;
}

bool Reader::readLength(uint8_t marker, uint8_t fixBase, uint8_t fixMask, uint32_t& length) {
    if ((marker & fixBase) == fixBase) {
        length = marker & fixMask;
        return true;
    }

    switch (marker) {
        case 0xC4:
        case 0xD9:
        case 0xDC:
        case 0xDE:
            if (offset >= input.size()) {
                return false;
            }
            length = input[offset++];
            return true;

        case 0xC5:
        case 0xDA:
            {
                uint16_t value = 0;
                if (!readUint16(input, offset, value)) {
                    return false;
                }
                length = value;
                return true;
            }

        case 0xC6:
        case 0xDB:
        case 0xDD:
        case 0xDF:
            return readUint32(input, offset, length);

        default:
            return false;
    }
}

bool Reader::readNil() {
    uint8_t marker = 0;
    return readMarker(marker) && marker == 0xC0;
}

bool Reader::readBool(bool& value) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    if (marker == 0xC2) {
        value = false;
        return true;
    }
    if (marker == 0xC3) {
        value = true;
        return true;
    }
    return false;
}

bool Reader::readUint(uint64_t& value) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    if (marker <= 0x7F) {
        value = marker;
        return true;
    }

    switch (marker) {
        case 0xCC:
            if (offset >= input.size()) {
                return false;
            }
            value = input[offset++];
            return true;

        case 0xCD:
            {
                uint16_t parsed = 0;
                if (!readUint16(input, offset, parsed)) {
                    return false;
                }
                value = parsed;
                return true;
            }

        case 0xCE:
            {
                uint32_t parsed = 0;
                if (!readUint32(input, offset, parsed)) {
                    return false;
                }
                value = parsed;
                return true;
            }

        case 0xCF:
            return readUint64(input, offset, value);

        default:
            return false;
    }
}

bool Reader::readInt(int64_t& value) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    if (marker <= 0x7F) {
        value = marker;
        return true;
    }

    if (marker >= 0xE0) {
        value = static_cast<int8_t>(marker);
        return true;
    }

    switch (marker) {
        case 0xD0:
            if (offset >= input.size()) {
                return false;
            }
            value = static_cast<int8_t>(input[offset++]);
            return true;

        case 0xD1:
            {
                uint16_t parsed = 0;
                if (!readUint16(input, offset, parsed)) {
                    return false;
                }
                value = static_cast<int16_t>(parsed);
                return true;
            }

        case 0xD2:
            {
                uint32_t parsed = 0;
                if (!readUint32(input, offset, parsed)) {
                    return false;
                }
                value = static_cast<int32_t>(parsed);
                return true;
            }

        case 0xD3:
            {
                uint64_t parsed = 0;
                if (!readUint64(input, offset, parsed)) {
                    return false;
                }
                value = static_cast<int64_t>(parsed);
                return true;
            }

        default:
            return false;
    }
}

bool Reader::readDouble(double& value) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    if (marker == 0xCA) {
        uint32_t bits = 0;
        if (!readUint32(input, offset, bits)) {
            return false;
        }

        float intermediate = std::bit_cast<float>(bits);
        value = intermediate;
        return true;
    }

    if (marker == 0xCB) {
        uint64_t bits = 0;
        if (!readUint64(input, offset, bits)) {
            return false;
        }

        value = std::bit_cast<double>(bits);
        return true;
    }

    return false;
}

bool Reader::readBin(std::vector<uint8_t>& value) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    uint32_t size = 0;
    switch (marker) {
        case 0xC4:
            if (offset >= input.size()) {
                return false;
            }
            size = input[offset++];
            break;

        case 0xC5:
            {
                uint16_t parsed = 0;
                if (!readUint16(input, offset, parsed)) {
                    return false;
                }
                size = parsed;
                break;
            }

        case 0xC6:
            if (!readUint32(input, offset, size)) {
                return false;
            }
            break;

        default:
            return false;
    }

    value.resize(size);
    return readBytes(value.data(), value.size());
}

bool Reader::readString(std::string& value) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    uint32_t size = 0;
    if (!readLength(marker, 0xA0, 0x1F, size)) {
        return false;
    }

    const bool isString = (marker & 0xE0) == 0xA0 || marker == 0xD9 || marker == 0xDA || marker == 0xDB;
    if (!isString) {
        return false;
    }

    value.resize(size);
    return readBytes(reinterpret_cast<uint8_t*>(value.data()), value.size());
}

bool Reader::readArrayHeader(size_t& count) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    uint32_t parsed = 0;
    if (!readLength(marker, 0x90, 0x0F, parsed)) {
        return false;
    }

    const bool isArray = (marker & 0xF0) == 0x90 || marker == 0xDC || marker == 0xDD;
    if (!isArray) {
        return false;
    }

    count = parsed;
    return true;
}

bool Reader::readMapHeader(size_t& count) {
    uint8_t marker = 0;
    if (!readMarker(marker)) {
        return false;
    }

    uint32_t parsed = 0;
    if (!readLength(marker, 0x80, 0x0F, parsed)) {
        return false;
    }

    const bool isMap = (marker & 0xF0) == 0x80 || marker == 0xDE || marker == 0xDF;
    if (!isMap) {
        return false;
    }

    count = parsed;
    return true;
}

bool Reader::skip() {
    if (offset >= input.size()) {
        return false;
    }

    const auto savedOffset = offset;
    const auto marker = input[offset];

    if (marker == 0xC0) {
        offset += 1;
        return true;
    }

    bool boolValue = false;
    if (readBool(boolValue)) {
        return true;
    }
    offset = savedOffset;

    uint64_t uintValue = 0;
    if (readUint(uintValue)) {
        return true;
    }
    offset = savedOffset;

    int64_t intValue = 0;
    if (readInt(intValue)) {
        return true;
    }
    offset = savedOffset;

    double doubleValue = 0.0;
    if (readDouble(doubleValue)) {
        return true;
    }
    offset = savedOffset;

    std::vector<uint8_t> binValue;
    if (readBin(binValue)) {
        return true;
    }
    offset = savedOffset;

    std::string stringValue;
    if (readString(stringValue)) {
        return true;
    }
    offset = savedOffset;

    size_t count = 0;
    if (readArrayHeader(count)) {
        for (size_t index = 0; index < count; index++) {
            if (!skip()) {
                offset = savedOffset;
                return false;
            }
        }
        return true;
    }
    offset = savedOffset;

    if (readMapHeader(count)) {
        for (size_t index = 0; index < count; index++) {
            if (!skip() || !skip()) {
                offset = savedOffset;
                return false;
            }
        }
        return true;
    }

    offset = savedOffset;
    return false;
}

} // namespace tt::service::reticulum::msgpack
