#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <Tactility/PubSub.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/reticulum/Reticulum.h>

#include <algorithm>
#include <cctype>
#include <format>

#include <lvgl.h>

#include <tactility/lvgl_fonts.h>
#include <tactility/lvgl_icon_shared.h>

namespace tt::app::contacts {

static const auto LOGGER = Logger("Contacts");

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
        return "local destination";
    }

    if (!record.interfaceId.empty()) {
        return std::format("{} via {}", service::reticulum::interfaceKindToString(record.interfaceKind), record.interfaceId);
    }

    return "interface unknown";
}

class ContactsApp final : public App {

    using ReticulumEvent = service::reticulum::ReticulumEvent;

    PubSub<ReticulumEvent>::SubscriptionHandle reticulumSubscription = nullptr;
    Mutex mutex;
    std::vector<ContactRecord> contacts {};
    std::string latestEvent = "Waiting for Reticulum events";
    service::reticulum::RuntimeState runtimeState = service::reticulum::RuntimeState::Stopped;
    bool viewEnabled = false;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* summaryLabel = nullptr;
    lv_obj_t* eventLabel = nullptr;
    lv_obj_t* list = nullptr;

    static void onRefreshPressed(lv_event_t* event) {
        auto* self = static_cast<ContactsApp*>(lv_event_get_user_data(event));
        self->reloadFromService("Manual refresh");
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
            entry.subtitle = std::format("{} ({})", destination.acceptsLinks ? "accepts links" : "no links", shortenHash(destination.hash));
            entry.detail = destination.provisionalHash ? "provisional local destination" : "local destination";
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
                ? std::format("path-response announce, hops={}", announce.hops)
                : std::format("announce observed, hops={}", announce.hops);
        }

        for (const auto& path : service::reticulum::getPaths()) {
            auto& entry = upsertContact(path.destination);
            entry.hasPath = true;
            entry.interfaceId = path.interfaceId;
            entry.hops = path.hops;
            const auto pathSummary = path.interfaceId.empty() ? std::string("path learned") : std::format("path via {}", path.interfaceId);
            entry.subtitle = std::format("{} | {}", shortenHash(path.destination), pathSummary);
            entry.detail = std::format("reachable in {} hop(s){}", path.hops, path.unresponsive ? ", marked unresponsive" : "");
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
        lv_label_set_text_fmt(summaryLabel,
            "Reticulum: %s\nContacts: %d | announces: %d | paths: %d",
            service::reticulum::runtimeStateToString(runtimeState),
            static_cast<int>(contacts.size()),
            announceCount,
            pathCount
        );
        lv_label_set_text(eventLabel, latestEvent.c_str());

        lv_obj_clean(list);
        if (contacts.empty()) {
            lv_list_add_text(list, "No contacts observed yet");
            return;
        }

        for (const auto& contact : contacts) {
            const auto line = std::format("{}\n{}\n{}",
                contact.title,
                contact.subtitle.empty() ? shortenHash(contact.destination) : contact.subtitle,
                contact.detail.empty() ? "no additional detail" : contact.detail
            );

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
                rebuildContactsLocked(event.detail.empty() ? "Reticulum updated" : event.detail);
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

        reloadFromService("Contacts subscribed to Reticulum");
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
        lvgl::toolbar_add_text_button_action(toolbar, "Refresh", onRefreshPressed, this);

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
    .appIcon = LVGL_ICON_SHARED_FORUM,
    .appCategory = Category::System,
    .createApp = create<ContactsApp>
};

} // namespace tt::app::contacts
