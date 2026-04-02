#include <Tactility/service/reticulum/IdentityStore.h>

#include <Tactility/Logger.h>
#include <Tactility/file/File.h>
#include <Tactility/service/reticulum/Crypto.h>

#include <cJSON.h>

#include <algorithm>
#include <format>

namespace tt::service::reticulum {

namespace {

static const auto LOGGER = Logger("ReticulumId");
constexpr int IDENTITY_STORE_VERSION = 1;

template<typename ArrayType>
bool readHexField(const cJSON* object, const char* key, ArrayType& output) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || cJSON_GetStringValue(item) == nullptr) {
        return false;
    }
    return parseHex(cJSON_GetStringValue(item), output.data(), output.size());
}

template<typename ArrayType>
void writeHexField(cJSON* object, const char* key, const ArrayType& bytes) {
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        text += std::format("{:02x}", byte);
    }
    cJSON_AddStringToObject(object, key, text.c_str());
}

bool readDestinationHashField(const cJSON* object, const char* key, DestinationHash& output) {
    return readHexField(object, key, output.bytes);
}

void writeDestinationHashField(cJSON* object, const char* key, const DestinationHash& hash) {
    cJSON_AddStringToObject(object, key, toHex(hash).c_str());
}

std::vector<uint8_t> decodeHexVector(const char* value) {
    if (value == nullptr) {
        return {};
    }

    const std::string_view text(value);
    if ((text.size() % 2) != 0) {
        return {};
    }

    std::vector<uint8_t> output(text.size() / 2);
    if (!parseHex(text, output.data(), output.size())) {
        return {};
    }
    return output;
}

} // namespace

std::string IdentityStore::keyFor(const DestinationHash& destinationHash) {
    return toHex(destinationHash);
}

bool IdentityStore::loadOrCreateLocalIdentity() {
    if (file::isFile(localIdentityPath)) {
        const auto data = file::readString(localIdentityPath);
        if (data == nullptr) {
            LOGGER.error("Failed to read {}", localIdentityPath);
            return false;
        }

        auto* root = cJSON_Parse(reinterpret_cast<const char*>(data.get()));
        if (root == nullptr) {
            LOGGER.error("Failed to parse {}", localIdentityPath);
            return false;
        }

        const bool ok =
            readDestinationHashField(root, "identityHash", localIdentity.hash) &&
            readHexField(root, "x25519Private", localIdentity.encryptionPrivateKey) &&
            readHexField(root, "x25519Public", localIdentity.encryptionPublicKey) &&
            readHexField(root, "ed25519Private", localIdentity.signingPrivateKey) &&
            readHexField(root, "ed25519Public", localIdentity.signingPublicKey);

        cJSON_Delete(root);
        if (!ok) {
            LOGGER.error("Invalid local identity state in {}", localIdentityPath);
            return false;
        }

        std::copy(localIdentity.encryptionPublicKey.begin(), localIdentity.encryptionPublicKey.end(), localIdentity.publicKey.begin());
        std::copy(localIdentity.signingPublicKey.begin(), localIdentity.signingPublicKey.end(), localIdentity.publicKey.begin() + CURVE25519_KEY_LENGTH);

        FullHashBytes derivedIdentityHash {};
        if (!crypto::sha256(localIdentity.publicKey.data(), localIdentity.publicKey.size(), derivedIdentityHash)) {
            LOGGER.error("Failed to derive local identity hash from persisted keys");
            return false;
        }

        DestinationHash expectedHash {};
        std::copy_n(derivedIdentityHash.begin(), expectedHash.bytes.size(), expectedHash.bytes.begin());
        if (expectedHash != localIdentity.hash) {
            LOGGER.error("Persisted local identity hash does not match persisted public keys");
            return false;
        }

        LOGGER.info("Loaded Reticulum local identity from {}", localIdentityPath);
        return true;
    }

    if (!crypto::generateX25519KeyPair(localIdentity.encryptionPrivateKey, localIdentity.encryptionPublicKey) ||
        !crypto::generateEd25519KeyPair(localIdentity.signingPrivateKey, localIdentity.signingPublicKey)) {
        LOGGER.error("Failed to generate Reticulum local identity");
        return false;
    }

    std::copy(localIdentity.encryptionPublicKey.begin(), localIdentity.encryptionPublicKey.end(), localIdentity.publicKey.begin());
    std::copy(localIdentity.signingPublicKey.begin(), localIdentity.signingPublicKey.end(), localIdentity.publicKey.begin() + CURVE25519_KEY_LENGTH);

    FullHashBytes fullIdentityHash {};
    if (!crypto::sha256(localIdentity.publicKey.data(), localIdentity.publicKey.size(), fullIdentityHash)) {
        LOGGER.error("Failed to derive local identity hash");
        return false;
    }
    std::copy_n(fullIdentityHash.begin(), localIdentity.hash.bytes.size(), localIdentity.hash.bytes.begin());
    if (!persistLocalIdentity()) {
        LOGGER.error("Failed to persist new Reticulum local identity");
        return false;
    }

    LOGGER.info("Created Reticulum local identity at {}", localIdentityPath);
    return true;
}

bool IdentityStore::loadKnownDestinations() {
    knownDestinations.clear();

    if (!file::isFile(knownDestinationsPath)) {
        return true;
    }

    const auto data = file::readString(knownDestinationsPath);
    if (data == nullptr) {
        LOGGER.error("Failed to read {}", knownDestinationsPath);
        return false;
    }

    auto* root = cJSON_Parse(reinterpret_cast<const char*>(data.get()));
    if (root == nullptr) {
        LOGGER.error("Failed to parse {}", knownDestinationsPath);
        return false;
    }

    const auto* entries = cJSON_GetObjectItemCaseSensitive(root, "destinations");
    if (cJSON_IsArray(entries)) {
        const auto count = cJSON_GetArraySize(entries);
        for (int index = 0; index < count; index++) {
            const auto* item = cJSON_GetArrayItem(entries, index);
            if (!cJSON_IsObject(item)) {
                continue;
            }

            KnownDestination record;
            if (!readDestinationHashField(item, "destinationHash", record.destinationHash) ||
                !readDestinationHashField(item, "identityHash", record.identityHash) ||
                !readHexField(item, "nameHash", record.nameHash) ||
                !readHexField(item, "identityPublicKey", record.identityPublicKey) ||
                !readHexField(item, "lastAnnouncePacketHash", record.lastAnnouncePacketHash) ||
                !readHexField(item, "lastAnnounceRandom", record.lastAnnounceRandom)) {
                continue;
            }

            if (const auto* seenTick = cJSON_GetObjectItemCaseSensitive(item, "lastSeenTick"); cJSON_IsNumber(seenTick)) {
                record.lastSeenTick = static_cast<uint32_t>(cJSON_GetNumberValue(seenTick));
            }

            if (const auto* appData = cJSON_GetObjectItemCaseSensitive(item, "appData");
                cJSON_IsString(appData) && cJSON_GetStringValue(appData) != nullptr) {
                record.appData = decodeHexVector(cJSON_GetStringValue(appData));
            }

            if (const auto* ratchet = cJSON_GetObjectItemCaseSensitive(item, "ratchetPublicKey");
                cJSON_IsString(ratchet) && cJSON_GetStringValue(ratchet) != nullptr) {
                Curve25519PublicKeyBytes ratchetBytes {};
                if (parseHex(cJSON_GetStringValue(ratchet), ratchetBytes.data(), ratchetBytes.size())) {
                    record.latestRatchetPublicKey = ratchetBytes;
                }
            }

            knownDestinations[keyFor(record.destinationHash)] = std::move(record);
        }
    }

    cJSON_Delete(root);
    LOGGER.info("Loaded {} known Reticulum destinations", knownDestinations.size());
    return true;
}

bool IdentityStore::persistLocalIdentity() const {
    auto* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddNumberToObject(root, "version", IDENTITY_STORE_VERSION);
    writeDestinationHashField(root, "identityHash", localIdentity.hash);
    writeHexField(root, "x25519Private", localIdentity.encryptionPrivateKey);
    writeHexField(root, "x25519Public", localIdentity.encryptionPublicKey);
    writeHexField(root, "ed25519Private", localIdentity.signingPrivateKey);
    writeHexField(root, "ed25519Public", localIdentity.signingPublicKey);

    char* text = cJSON_PrintUnformatted(root);
    const bool ok = text != nullptr && file::writeString(localIdentityPath, text);
    if (text != nullptr) {
        cJSON_free(text);
    }
    cJSON_Delete(root);
    return ok;
}

bool IdentityStore::persistKnownDestinationsLocked() const {
    auto* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddNumberToObject(root, "version", IDENTITY_STORE_VERSION);
    auto* entries = cJSON_AddArrayToObject(root, "destinations");
    if (entries == nullptr) {
        cJSON_Delete(root);
        return false;
    }

    for (const auto& [key, destination] : knownDestinations) {
        auto* item = cJSON_CreateObject();
        if (item == nullptr) {
            cJSON_Delete(root);
            return false;
        }

        writeDestinationHashField(item, "destinationHash", destination.destinationHash);
        writeDestinationHashField(item, "identityHash", destination.identityHash);
        writeHexField(item, "nameHash", destination.nameHash);
        writeHexField(item, "identityPublicKey", destination.identityPublicKey);
        writeHexField(item, "lastAnnouncePacketHash", destination.lastAnnouncePacketHash);
        writeHexField(item, "lastAnnounceRandom", destination.lastAnnounceRandom);
        cJSON_AddNumberToObject(item, "lastSeenTick", destination.lastSeenTick);

        std::string appDataHex;
        appDataHex.reserve(destination.appData.size() * 2);
        for (const auto byte : destination.appData) {
            appDataHex += std::format("{:02x}", byte);
        }
        cJSON_AddStringToObject(item, "appData", appDataHex.c_str());

        if (destination.latestRatchetPublicKey.has_value()) {
            cJSON_AddStringToObject(item, "ratchetPublicKey", toHex(*destination.latestRatchetPublicKey).c_str());
        }

        cJSON_AddItemToArray(entries, item);
    }

    char* text = cJSON_PrintUnformatted(root);
    const bool ok = text != nullptr && file::writeString(knownDestinationsPath, text);
    if (text != nullptr) {
        cJSON_free(text);
    }
    cJSON_Delete(root);
    return ok;
}

bool IdentityStore::init(const ServicePaths& paths) {
    rootPath = paths.getUserDataDirectory();
    localIdentityPath = paths.getUserDataPath("identities/local_identity.json");
    knownDestinationsPath = paths.getUserDataPath("identities/known_destinations.json");

    if (!crypto::init()) {
        LOGGER.error("Reticulum crypto init failed");
        return false;
    }

    if (!file::findOrCreateDirectory(rootPath, 0777) ||
        !file::findOrCreateParentDirectory(localIdentityPath, 0777) ||
        !file::findOrCreateParentDirectory(knownDestinationsPath, 0777)) {
        LOGGER.error("Failed to create Reticulum identity directories");
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!loadOrCreateLocalIdentity() || !loadKnownDestinations()) {
        return false;
    }

    initialized = true;
    return true;
}

IdentityStore::LocalIdentity IdentityStore::getLocalIdentity() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return localIdentity;
}

std::optional<IdentityStore::KnownDestination> IdentityStore::getKnownDestination(const DestinationHash& destinationHash) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (const auto iterator = knownDestinations.find(keyFor(destinationHash)); iterator != knownDestinations.end()) {
        return iterator->second;
    }

    return std::nullopt;
}

std::optional<IdentityStore::KnownDestination> IdentityStore::getKnownDestinationByIdentityHash(const DestinationHash& identityHash) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(knownDestinations.begin(), knownDestinations.end(), [&identityHash](const auto& entry) {
        return entry.second.identityHash == identityHash;
    });

    if (iterator != knownDestinations.end()) {
        return iterator->second;
    }

    return std::nullopt;
}

std::vector<IdentityStore::KnownDestination> IdentityStore::getKnownDestinations() const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    std::vector<KnownDestination> result;
    result.reserve(knownDestinations.size());
    for (const auto& [key, destination] : knownDestinations) {
        result.push_back(destination);
    }
    return result;
}

bool IdentityStore::rememberDestination(const KnownDestination& destination) {
    if (destination.destinationHash.empty()) {
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    knownDestinations[keyFor(destination.destinationHash)] = destination;
    return persistKnownDestinationsLocked();
}

bool IdentityStore::updateRatchet(const DestinationHash& destinationHash, const Curve25519PublicKeyBytes& ratchetPublicKey) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = knownDestinations.find(keyFor(destinationHash));
    if (iterator == knownDestinations.end()) {
        return false;
    }

    iterator->second.latestRatchetPublicKey = ratchetPublicKey;
    return persistKnownDestinationsLocked();
}

} // namespace tt::service::reticulum
