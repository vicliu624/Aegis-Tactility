#include <Tactility/app/AppContext.h>
#include <Tactility/app/contacts/TextResources.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <Tactility/PubSub.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/settings/Language.h>
#include <Tactility/service/reticulum/Reticulum.h>

#include <algorithm>
#include <cctype>
#include <format>

#include <lvgl.h>

#include <tactility/lvgl_fonts.h>
#include <tactility/lvgl_icon_shared.h>

namespace tt::app::contacts {

static const auto LOGGER = Logger("Contacts");

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/Contacts/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/Contacts/i18n";
#endif

static tt::i18n::TextResources& getTextResources() {
    static tt::i18n::TextResources textResources(TEXT_RESOURCE_PATH);
    static std::string loadedLocale;

    const auto currentLocale = tt::settings::toString(tt::settings::getLanguage());
    if (loadedLocale != currentLocale) {
        textResources.load();
        loadedLocale = currentLocale;
    }

    return textResources;
}

template <typename... Args>
static std::string formatText(i18n::Text key, Args&&... args) {
    return std::vformat(getTextResources()[key], std::make_format_args(args...));
}

static std::string getLocalizedAppName() {
    return getTextResources()[i18n::Text::APP_NAME];
}

static std::string getRuntimeStateLabel(service::reticulum::RuntimeState state) {
    switch (state) {
        case service::reticulum::RuntimeState::Starting:
            return getTextResources()[i18n::Text::RUNTIME_STARTING];
        case service::reticulum::RuntimeState::Ready:
            return getTextResources()[i18n::Text::RUNTIME_READY];
        case service::reticulum::RuntimeState::Stopping:
            return getTextResources()[i18n::Text::RUNTIME_STOPPING];
        case service::reticulum::RuntimeState::Stopped:
            return getTextResources()[i18n::Text::RUNTIME_STOPPED];
        case service::reticulum::RuntimeState::Faulted:
            return getTextResources()[i18n::Text::RUNTIME_FAULTED];
    }

    return getTextResources()[i18n::Text::RUNTIME_STOPPED];
}

static std::string getInterfaceKindLabel(service::reticulum::InterfaceKind kind) {
    switch (kind) {
        case service::reticulum::InterfaceKind::EspNow:
            return "ESP-NOW";
        case service::reticulum::InterfaceKind::LoRa:
            return "LoRa";
        case service::reticulum::InterfaceKind::Unknown:
            return getTextResources()[i18n::Text::INTERFACE_KIND_UNKNOWN];
    }

    return getTextResources()[i18n::Text::INTERFACE_KIND_UNKNOWN];
}

static std::string describeEvent(const service::reticulum::ReticulumEvent& event) {
    switch (event.type) {
        case service::reticulum::EventType::RuntimeStateChanged:
            return formatText(i18n::Text::EVENT_RUNTIME_STATE_CHANGED_FMT, getRuntimeStateLabel(event.runtimeState));
        case service::reticulum::EventType::InterfaceAttached:
            return getTextResources()[i18n::Text::EVENT_INTERFACE_ATTACHED];
        case service::reticulum::EventType::InterfaceDetached:
            return getTextResources()[i18n::Text::EVENT_INTERFACE_DETACHED];
        case service::reticulum::EventType::InterfaceStarted:
            return getTextResources()[i18n::Text::EVENT_INTERFACE_STARTED];
        case service::reticulum::EventType::InterfaceStopped:
            return getTextResources()[i18n::Text::EVENT_INTERFACE_STOPPED];
        case service::reticulum::EventType::LocalDestinationRegistered:
            return getTextResources()[i18n::Text::EVENT_LOCAL_DESTINATION_REGISTERED];
        case service::reticulum::EventType::AnnounceObserved:
            return getTextResources()[i18n::Text::EVENT_ANNOUNCE_OBSERVED];
        case service::reticulum::EventType::PathTableChanged:
            return getTextResources()[i18n::Text::EVENT_PATH_TABLE_CHANGED];
        case service::reticulum::EventType::Error:
            return getTextResources()[i18n::Text::EVENT_ERROR];
        default:
            return getTextResources()[i18n::Text::RETICULUM_UPDATED];
    }
}

struct ContactRecord {
    service::reticulum::DestinationHash destination {};
    std::string title {};
    std::string subtitle {};
    std::string detail {};
    bool local = false;
    bool hasAnnounce = false;
    bool hasPath = false;
    uint8_t hops = 0;
    std::string interfaceId {};
    service::reticulum::InterfaceKind interfaceKind = service::reticulum::InterfaceKind::Unknown;
    uint32_t observedTick = 0;
};

static std::string safePrintableString(const std::vector<uint8_t>& bytes) {
    std::string output;
    output.reserve(bytes.size());
    for (const auto byte : bytes) {
        if (byte == 0) {
            break;
        }

        const auto ch = static_cast<char>(byte);
        if (std::isprint(static_cast<unsigned char>(ch))) {
            output.push_back(ch);
        } else {
            return {};
        }
    }

    return output;
}

static std::string shortenHash(const service::reticulum::DestinationHash& destination) {
    const auto hex = service::reticulum::toHex(destination);
    if (hex.size() <= 12) {
        return hex;
    }

    return hex.substr(0, 8) + ".." + hex.substr(hex.size() - 4);
}

static std::string makeInterfaceSummary(const ContactRecord& record) {
    if (record.local) {
        return getTextResources()[i18n::Text::LOCAL_DESTINATION];
    }

    if (!record.interfaceId.empty()) {
        return formatText(i18n::Text::VIA_INTERFACE_FMT, getInterfaceKindLabel(record.interfaceKind), record.interfaceId);
    }

    return getTextResources()[i18n::Text::INTERFACE_UNKNOWN];
}

class ContactsApp final : public App {

    using ReticulumEvent = service::reticulum::ReticulumEvent;

    PubSub<ReticulumEvent>::SubscriptionHandle reticulumSubscription = nullptr;
    Mutex mutex;
    std::vector<ContactRecord> contacts {};
    std::string latestEvent = getTextResources()[i18n::Text::WAITING_FOR_EVENTS];
    service::reticulum::RuntimeState runtimeState = service::reticulum::RuntimeState::Stopped;
    bool viewEnabled = false;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* summaryLabel = nullptr;
    lv_obj_t* eventLabel = nullptr;
    lv_obj_t* list = nullptr;

    static void onRefreshPressed(lv_event_t* event) {
        auto* self = static_cast<ContactsApp*>(lv_event_get_user_data(event));
        self->reloadFromService(getTextResources()[i18n::Text::MANUAL_REFRESH]);
    }

    static void configureListButton(lv_obj_t* button) {
        if (button == nullptr) {
            return;
        }

        lv_obj_set_height(button, LV_SIZE_CONTENT);
        if (auto* icon = lv_obj_get_child(button, 0); icon != nullptr) {
            lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_font(), LV_PART_MAIN);
        }

        if (auto* label = lv_obj_get_child(button, 1); label != nullptr) {
            lv_obj_set_width(label, LV_PCT(100));
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_line_space(label, 4, LV_PART_MAIN);
        }
    }

    ContactRecord& upsertContact(const service::reticulum::DestinationHash& destination) {
        auto iterator = std::find_if(contacts.begin(), contacts.end(), [&destination](const auto& entry) {
            return entry.destination == destination;
        });
        if (iterator != contacts.end()) {
            return *iterator;
        }

        contacts.push_back(ContactRecord {
            .destination = destination,
            .title = shortenHash(destination)
        });
        return contacts.back();
    }

    void rebuildContactsLocked(const std::string& reason) {
        runtimeState = service::reticulum::getRuntimeState();
        contacts.clear();

        for (const auto& destination : service::reticulum::getLocalDestinations()) {
            auto& entry = upsertContact(destination.hash);
            entry.local = true;
            entry.hasAnnounce = destination.announceEnabled;
            entry.title = destination.name;
            entry.subtitle = formatText(i18n::Text::LOCAL_SUBTITLE_FMT,
                destination.acceptsLinks ? getTextResources()[i18n::Text::ACCEPTS_LINKS] : getTextResources()[i18n::Text::NO_LINKS],
                shortenHash(destination.hash));
            entry.detail = getTextResources()[i18n::Text::LOCAL_DESTINATION];
        }

        for (const auto& announce : service::reticulum::getAnnounces()) {
            auto& entry = upsertContact(announce.destination);
            entry.hasAnnounce = true;
            entry.local = entry.local || announce.local;
            entry.interfaceId = announce.interfaceId;
            entry.interfaceKind = announce.interfaceKind;
            entry.hops = announce.hops;
            entry.observedTick = std::max(entry.observedTick, announce.observedTick);

            const auto appData = safePrintableString(announce.appData);
            if (!appData.empty() && entry.title == shortenHash(announce.destination)) {
                entry.title = appData;
            }

            entry.subtitle = std::format("{} | {}", shortenHash(announce.destination), makeInterfaceSummary(entry));
            entry.detail = announce.pathResponse
                ? formatText(i18n::Text::PATH_RESPONSE_ANNOUNCE_FMT, announce.hops)
                : formatText(i18n::Text::ANNOUNCE_OBSERVED_FMT, announce.hops);
        }

        for (const auto& path : service::reticulum::getPaths()) {
            auto& entry = upsertContact(path.destination);
            entry.hasPath = true;
            entry.interfaceId = path.interfaceId;
            entry.hops = path.hops;
            const auto pathSummary = path.interfaceId.empty()
                ? getTextResources()[i18n::Text::PATH_LEARNED]
                : formatText(i18n::Text::PATH_VIA_FMT, path.interfaceId);
            entry.subtitle = std::format("{} | {}", shortenHash(path.destination), pathSummary);
            entry.detail = formatText(i18n::Text::REACHABLE_FMT,
                path.hops,
                path.unresponsive ? getTextResources()[i18n::Text::UNRESPONSIVE_SUFFIX] : std::string {});
        }

        std::ranges::sort(contacts, [](const auto& left, const auto& right) {
            if (left.local != right.local) {
                return left.local > right.local;
            }
            if (left.hasPath != right.hasPath) {
                return left.hasPath > right.hasPath;
            }
            if (left.hasAnnounce != right.hasAnnounce) {
                return left.hasAnnounce > right.hasAnnounce;
            }
            return left.title < right.title;
        });

        latestEvent = reason;
    }

    void refreshViewLocked() {
        if (!viewEnabled || summaryLabel == nullptr || eventLabel == nullptr || list == nullptr) {
            return;
        }

        const auto announceCount = static_cast<int>(service::reticulum::getAnnounces().size());
        const auto pathCount = static_cast<int>(service::reticulum::getPaths().size());
        const auto summaryText = formatText(i18n::Text::SUMMARY_FMT,
            getRuntimeStateLabel(runtimeState),
            static_cast<int>(contacts.size()),
            announceCount,
            pathCount);
        lv_label_set_text(summaryLabel, summaryText.c_str());
        lv_label_set_text(eventLabel, latestEvent.c_str());

        lv_obj_clean(list);
        if (contacts.empty()) {
            lv_list_add_text(list, getTextResources()[i18n::Text::NO_CONTACTS_OBSERVED].c_str());
            return;
        }

        for (const auto& contact : contacts) {
            const auto line = formatText(i18n::Text::CONTACT_LINE_FMT,
                contact.title,
                contact.subtitle.empty() ? shortenHash(contact.destination) : contact.subtitle,
                contact.detail.empty() ? getTextResources()[i18n::Text::NO_ADDITIONAL_DETAIL] : contact.detail);

            const char* icon = contact.local
                ? LV_SYMBOL_HOME
                : (contact.hasPath ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);

            auto* button = lv_list_add_button(list, icon, line.c_str());
            configureListButton(button);
        }
    }

    void requestViewUpdateLocked() {
        if (!viewEnabled) {
            return;
        }

        if (lvgl::lock(1000)) {
            refreshViewLocked();
            lvgl::unlock();
        } else {
            LOGGER.warn("Failed to lock LVGL for Contacts refresh");
        }
    }

    void reloadFromService(const std::string& reason) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        rebuildContactsLocked(reason);
        requestViewUpdateLocked();
    }

    void onReticulumEvent(const ReticulumEvent& event) {
        auto lock = mutex.asScopedLock();
        lock.lock();

        switch (event.type) {
            case service::reticulum::EventType::RuntimeStateChanged:
            case service::reticulum::EventType::LocalDestinationRegistered:
            case service::reticulum::EventType::AnnounceObserved:
            case service::reticulum::EventType::PathTableChanged:
            case service::reticulum::EventType::InterfaceAttached:
            case service::reticulum::EventType::InterfaceDetached:
            case service::reticulum::EventType::InterfaceStarted:
            case service::reticulum::EventType::InterfaceStopped:
                rebuildContactsLocked(describeEvent(event));
                requestViewUpdateLocked();
                break;
            default:
                break;
        }
    }

public:

    void onCreate(AppContext& appContext) override {
        if (const auto pubsub = service::reticulum::getPubsub(); pubsub != nullptr) {
            reticulumSubscription = pubsub->subscribe([this](ReticulumEvent event) {
                onReticulumEvent(event);
            });
        }

        reloadFromService(getTextResources()[i18n::Text::SUBSCRIBED]);
    }

    void onDestroy(AppContext& appContext) override {
        if (reticulumSubscription != nullptr) {
            if (const auto pubsub = service::reticulum::getPubsub(); pubsub != nullptr) {
                pubsub->unsubscribe(reticulumSubscription);
            }
            reticulumSubscription = nullptr;
        }
    }

    void onShow(AppContext& appContext, lv_obj_t* parent) override {
        auto lock = mutex.asScopedLock();
        lock.lock();

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        toolbar = lvgl::toolbar_create(parent, appContext);
        lvgl::toolbar_add_text_button_action(toolbar, getTextResources()[i18n::Text::REFRESH].c_str(), onRefreshPressed, this);

        summaryLabel = lv_label_create(parent);
        lv_obj_set_width(summaryLabel, LV_PCT(100));
        lv_label_set_long_mode(summaryLabel, LV_LABEL_LONG_WRAP);

        eventLabel = lv_label_create(parent);
        lv_obj_set_width(eventLabel, LV_PCT(100));
        lv_label_set_long_mode(eventLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_opa(eventLabel, LV_OPA_70, LV_PART_MAIN);

        list = lv_list_create(parent);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_flex_grow(list, 1);

        viewEnabled = true;
        refreshViewLocked();
    }

    void onHide(AppContext& appContext) override {
        auto lock = mutex.asScopedLock();
        lock.lock();
        viewEnabled = false;
        toolbar = nullptr;
        summaryLabel = nullptr;
        eventLabel = nullptr;
        list = nullptr;
    }
};

extern const AppManifest manifest = {
    .appId = "Contacts",
    .appName = "Contacts",
    .resolveLocalizedAppName = &getLocalizedAppName,
    .appIcon = LVGL_ICON_SHARED_FORUM,
    .appCategory = Category::System,
    .createApp = create<ContactsApp>
};

} // namespace tt::app::contacts
