#include <Tactility/service/reticulum/Crypto.h>

#include <mbedtls/aes.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <psa/crypto.h>

#include <Tactility/Logger.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace tt::service::reticulum::crypto {

namespace {

static const auto LOGGER = Logger("RNS-Crypto");
static std::once_flag initOnce;
static bool cryptoReady = false;

constexpr size_t AES_BLOCK_SIZE = 16;
constexpr size_t TOKEN_HMAC_LENGTH = 32;
constexpr size_t TOKEN_OVERHEAD = AES_BLOCK_SIZE + TOKEN_HMAC_LENGTH;

bool ensureInit() {
    std::call_once(initOnce, [] {
        const auto status = psa_crypto_init();
        if (status != PSA_SUCCESS) {
            LOGGER.error("psa_crypto_init failed: {}", static_cast<long>(status));
            cryptoReady = false;
            return;
        }
        cryptoReady = true;
    });

    return cryptoReady;
}

bool importKey(
    psa_key_type_t type,
    size_t bits,
    psa_algorithm_t algorithm,
    psa_key_usage_t usage,
    const uint8_t* data,
    size_t dataSize,
    mbedtls_svc_key_id_t& key
) {
    if (!ensureInit() || data == nullptr || dataSize == 0) {
        return false;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_algorithm(&attributes, algorithm);
    psa_set_key_usage_flags(&attributes, usage);

    const auto status = psa_import_key(&attributes, data, dataSize, &key);
    psa_reset_key_attributes(&attributes);
    return status == PSA_SUCCESS;
}

bool generateKey(
    psa_key_type_t type,
    size_t bits,
    psa_algorithm_t algorithm,
    psa_key_usage_t usage,
    uint8_t* privateOutput,
    size_t privateOutputSize,
    size_t expectedPrivateSize,
    uint8_t* publicOutput,
    size_t publicOutputSize,
    size_t expectedPublicSize
) {
    if (!ensureInit() || privateOutput == nullptr || publicOutput == nullptr) {
        return false;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_algorithm(&attributes, algorithm);
    psa_set_key_usage_flags(&attributes, usage | PSA_KEY_USAGE_EXPORT);

    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    const auto generateStatus = psa_generate_key(&attributes, &key);
    psa_reset_key_attributes(&attributes);
    if (generateStatus != PSA_SUCCESS) {
        return false;
    }

    size_t privateSize = 0;
    const auto exportPrivateStatus = psa_export_key(key, privateOutput, privateOutputSize, &privateSize);
    size_t publicSize = 0;
    const auto exportPublicStatus = psa_export_public_key(key, publicOutput, publicOutputSize, &publicSize);
    psa_destroy_key(key);

    return exportPrivateStatus == PSA_SUCCESS &&
        exportPublicStatus == PSA_SUCCESS &&
        privateSize == expectedPrivateSize &&
        publicSize == expectedPublicSize;
}

bool calculateHmacSha256(
    const uint8_t* key,
    size_t keySize,
    const uint8_t* input,
    size_t inputSize,
    uint8_t* output,
    size_t outputSize
) {
    if (key == nullptr || input == nullptr || output == nullptr || outputSize < TOKEN_HMAC_LENGTH) {
        return false;
    }

    const auto* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md != nullptr &&
        mbedtls_md_hmac(md, key, keySize, input, inputSize, output) == 0;
}

void applyPkcs7Padding(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& padded) {
    padded = plaintext;
    auto padLength = static_cast<uint8_t>(AES_BLOCK_SIZE - (plaintext.size() % AES_BLOCK_SIZE));
    if (padLength == 0) {
        padLength = static_cast<uint8_t>(AES_BLOCK_SIZE);
    }

    padded.resize(plaintext.size() + padLength, padLength);
}

bool stripPkcs7Padding(std::vector<uint8_t>& plaintext) {
    if (plaintext.empty()) {
        return false;
    }

    const auto padLength = plaintext.back();
    if (padLength == 0 || padLength > AES_BLOCK_SIZE || padLength > plaintext.size()) {
        return false;
    }

    for (size_t index = plaintext.size() - padLength; index < plaintext.size(); index++) {
        if (plaintext[index] != padLength) {
            return false;
        }
    }

    plaintext.resize(plaintext.size() - padLength);
    return true;
}

bool aes256CbcCrypt(
    bool encrypt,
    const uint8_t* key,
    const uint8_t* iv,
    const uint8_t* input,
    size_t inputSize,
    std::vector<uint8_t>& output
) {
    if (key == nullptr || iv == nullptr || input == nullptr || inputSize % AES_BLOCK_SIZE != 0) {
        return false;
    }

    mbedtls_aes_context context;
    mbedtls_aes_init(&context);

    std::array<uint8_t, AES_BLOCK_SIZE> ivCopy {};
    std::copy_n(iv, ivCopy.size(), ivCopy.begin());

    output.assign(input, input + inputSize);

    const auto setKeyStatus = encrypt
        ? mbedtls_aes_setkey_enc(&context, key, 256)
        : mbedtls_aes_setkey_dec(&context, key, 256);
    if (setKeyStatus != 0) {
        mbedtls_aes_free(&context);
        output.clear();
        return false;
    }

    const auto cryptStatus = mbedtls_aes_crypt_cbc(
        &context,
        encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
        inputSize,
        ivCopy.data(),
        input,
        output.data()
    );

    mbedtls_aes_free(&context);
    if (cryptStatus != 0) {
        output.clear();
        return false;
    }

    return true;
}

} // namespace

bool init() {
    return ensureInit();
}

bool fillRandom(uint8_t* output, size_t outputSize) {
    return ensureInit() &&
        output != nullptr &&
        psa_generate_random(output, outputSize) == PSA_SUCCESS;
}

bool sha256(const uint8_t* input, size_t inputSize, FullHashBytes& output) {
    if (input == nullptr && inputSize != 0) {
        return false;
    }

    return mbedtls_sha256(input, inputSize, output.data(), 0) == 0;
}

bool hkdfSha256(
    const uint8_t* inputKeyMaterial,
    size_t inputKeyMaterialSize,
    const uint8_t* salt,
    size_t saltSize,
    const uint8_t* info,
    size_t infoSize,
    uint8_t* output,
    size_t outputSize
) {
    const auto* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == nullptr || inputKeyMaterial == nullptr || output == nullptr) {
        return false;
    }

    return mbedtls_hkdf(
        md,
        salt,
        saltSize,
        inputKeyMaterial,
        inputKeyMaterialSize,
        info,
        infoSize,
        output,
        outputSize
    ) == 0;
}

bool generateX25519KeyPair(
    Curve25519PrivateKeyBytes& privateKey,
    Curve25519PublicKeyBytes& publicKey
) {
    return generateKey(
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY),
        255,
        PSA_ALG_ECDH,
        PSA_KEY_USAGE_DERIVE,
        privateKey.data(),
        privateKey.size(),
        privateKey.size(),
        publicKey.data(),
        publicKey.size(),
        publicKey.size()
    );
}

bool generateEd25519KeyPair(
    Ed25519PrivateKeyBytes& privateKey,
    Ed25519PublicKeyBytes& publicKey
) {
    return generateKey(
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS),
        255,
        PSA_ALG_PURE_EDDSA,
        PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE,
        privateKey.data(),
        privateKey.size(),
        privateKey.size(),
        publicKey.data(),
        publicKey.size(),
        publicKey.size()
    );
}

bool ed25519Sign(
    const Ed25519PrivateKeyBytes& privateKey,
    const uint8_t* message,
    size_t messageSize,
    SignatureBytes& signature
) {
    if (message == nullptr && messageSize != 0) {
        return false;
    }

    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    if (!importKey(
            PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS),
            255,
            PSA_ALG_PURE_EDDSA,
            PSA_KEY_USAGE_SIGN_MESSAGE,
            privateKey.data(),
            privateKey.size(),
            key)) {
        return false;
    }

    size_t signatureSize = 0;
    const auto status = psa_sign_message(
        key,
        PSA_ALG_PURE_EDDSA,
        message,
        messageSize,
        signature.data(),
        signature.size(),
        &signatureSize
    );
    psa_destroy_key(key);
    return status == PSA_SUCCESS && signatureSize == signature.size();
}

bool ed25519Verify(
    const Ed25519PublicKeyBytes& publicKey,
    const uint8_t* message,
    size_t messageSize,
    const SignatureBytes& signature
) {
    if (message == nullptr && messageSize != 0) {
        return false;
    }

    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    if (!importKey(
            PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_TWISTED_EDWARDS),
            255,
            PSA_ALG_PURE_EDDSA,
            PSA_KEY_USAGE_VERIFY_MESSAGE,
            publicKey.data(),
            publicKey.size(),
            key)) {
        return false;
    }

    const auto status = psa_verify_message(
        key,
        PSA_ALG_PURE_EDDSA,
        message,
        messageSize,
        signature.data(),
        signature.size()
    );
    psa_destroy_key(key);
    return status == PSA_SUCCESS;
}

bool x25519SharedSecret(
    const Curve25519PrivateKeyBytes& privateKey,
    const Curve25519PublicKeyBytes& peerPublicKey,
    std::array<uint8_t, CURVE25519_KEY_LENGTH>& sharedSecret
) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    if (!importKey(
            PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY),
            255,
            PSA_ALG_ECDH,
            PSA_KEY_USAGE_DERIVE,
            privateKey.data(),
            privateKey.size(),
            key)) {
        return false;
    }

    size_t outputSize = 0;
    const auto status = psa_raw_key_agreement(
        PSA_ALG_ECDH,
        key,
        peerPublicKey.data(),
        peerPublicKey.size(),
        sharedSecret.data(),
        sharedSecret.size(),
        &outputSize
    );
    psa_destroy_key(key);
    return status == PSA_SUCCESS && outputSize == sharedSecret.size();
}

bool tokenEncrypt(
    const std::array<uint8_t, 64>& derivedKey,
    const std::vector<uint8_t>& plaintext,
    std::vector<uint8_t>& token
) {
    std::array<uint8_t, AES_BLOCK_SIZE> iv {};
    if (!fillRandom(iv.data(), iv.size())) {
        return false;
    }

    std::vector<uint8_t> padded;
    applyPkcs7Padding(plaintext, padded);

    std::vector<uint8_t> ciphertext;
    if (!aes256CbcCrypt(true, derivedKey.data() + TOKEN_HMAC_LENGTH, iv.data(), padded.data(), padded.size(), ciphertext)) {
        return false;
    }

    token.clear();
    token.reserve(TOKEN_OVERHEAD + ciphertext.size());
    token.insert(token.end(), iv.begin(), iv.end());
    token.insert(token.end(), ciphertext.begin(), ciphertext.end());

    std::array<uint8_t, TOKEN_HMAC_LENGTH> hmac {};
    if (!calculateHmacSha256(derivedKey.data(), TOKEN_HMAC_LENGTH, token.data(), token.size(), hmac.data(), hmac.size())) {
        token.clear();
        return false;
    }

    token.insert(token.end(), hmac.begin(), hmac.end());
    return true;
}

bool tokenDecrypt(
    const std::array<uint8_t, 64>& derivedKey,
    const std::vector<uint8_t>& token,
    std::vector<uint8_t>& plaintext
) {
    if (token.size() < TOKEN_OVERHEAD || ((token.size() - TOKEN_OVERHEAD) % AES_BLOCK_SIZE) != 0) {
        return false;
    }

    const auto signedSize = token.size() - TOKEN_HMAC_LENGTH;
    std::array<uint8_t, TOKEN_HMAC_LENGTH> expectedHmac {};
    if (!calculateHmacSha256(derivedKey.data(), TOKEN_HMAC_LENGTH, token.data(), signedSize, expectedHmac.data(), expectedHmac.size())) {
        return false;
    }

    if (!std::equal(expectedHmac.begin(), expectedHmac.end(), token.end() - TOKEN_HMAC_LENGTH)) {
        return false;
    }

    std::vector<uint8_t> decrypted;
    if (!aes256CbcCrypt(
            false,
            derivedKey.data() + TOKEN_HMAC_LENGTH,
            token.data(),
            token.data() + AES_BLOCK_SIZE,
            signedSize - AES_BLOCK_SIZE,
            decrypted)) {
        return false;
    }

    if (!stripPkcs7Padding(decrypted)) {
        return false;
    }

    plaintext = std::move(decrypted);
    return true;
}

std::string describeStatus(int status) {
    return std::to_string(status);
}

std::string describeStatus(long status) {
    return std::to_string(status);
}

} // namespace tt::service::reticulum::crypto
