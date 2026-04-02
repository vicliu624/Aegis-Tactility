#pragma once

#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <string>
#include <vector>

namespace tt::service::reticulum::crypto {

bool init();

bool fillRandom(uint8_t* output, size_t outputSize);

bool sha256(const uint8_t* input, size_t inputSize, FullHashBytes& output);

bool hkdfSha256(
    const uint8_t* inputKeyMaterial,
    size_t inputKeyMaterialSize,
    const uint8_t* salt,
    size_t saltSize,
    const uint8_t* info,
    size_t infoSize,
    uint8_t* output,
    size_t outputSize
);

bool generateX25519KeyPair(
    Curve25519PrivateKeyBytes& privateKey,
    Curve25519PublicKeyBytes& publicKey
);

bool generateEd25519KeyPair(
    Ed25519PrivateKeyBytes& privateKey,
    Ed25519PublicKeyBytes& publicKey
);

bool ed25519Sign(
    const Ed25519PrivateKeyBytes& privateKey,
    const uint8_t* message,
    size_t messageSize,
    SignatureBytes& signature
);

bool ed25519Verify(
    const Ed25519PublicKeyBytes& publicKey,
    const uint8_t* message,
    size_t messageSize,
    const SignatureBytes& signature
);

bool x25519SharedSecret(
    const Curve25519PrivateKeyBytes& privateKey,
    const Curve25519PublicKeyBytes& peerPublicKey,
    std::array<uint8_t, CURVE25519_KEY_LENGTH>& sharedSecret
);

bool tokenEncrypt(
    const std::array<uint8_t, 64>& derivedKey,
    const std::vector<uint8_t>& plaintext,
    std::vector<uint8_t>& token
);

bool tokenDecrypt(
    const std::array<uint8_t, 64>& derivedKey,
    const std::vector<uint8_t>& token,
    std::vector<uint8_t>& plaintext
);

std::string describeStatus(int status);

std::string describeStatus(long status);

} // namespace tt::service::reticulum::crypto
