#include <Tactility/app/files/View.h>
#include <Tactility/app/files/State.h>
#include <Tactility/app/files/TextResources.h>
#include <Tactility/app/AppContext.h>

#include <Tactility/service/loader/Loader.h>
#include <Tactility/settings/Language.h>

#include <memory>

namespace tt::app::files {

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/Files/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/Files/i18n";
#endif

static std::string getLocalizedAppName() {
    static tt::i18n::TextResources textResources(TEXT_RESOURCE_PATH);
    static std::string loadedLocale;
    static std::string appName;

    const auto currentLocale = tt::settings::toString(tt::settings::getLanguage());
    if (loadedLocale != currentLocale) {
        textResources.load();
        loadedLocale = currentLocale;
        appName = textResources[i18n::Text::APP_NAME];
    }

    return appName;
}

extern const AppManifest manifest;

class FilesApp final : public App {

    std::unique_ptr<View> view;
    std::shared_ptr<State> state;

public:

    FilesApp() {
        state = std::make_shared<State>();
        view = std::make_unique<View>(state);
    }

    void onShow(AppContext& appContext, lv_obj_t* parent) override {
        view->init(appContext, parent);
    }

    void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle) override {
        view->onResult(launchId, result, std::move(bundle));
    }

    void onHide(AppContext& appContext) override {
        view->deinit(appContext);
    }
};

extern const AppManifest manifest = {
    .appId = "Files",
    .appName = "Files",
    .resolveLocalizedAppName = &getLocalizedAppName,
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<FilesApp>
};

void start() {
    app::start(manifest.appId);
}

} // namespace
