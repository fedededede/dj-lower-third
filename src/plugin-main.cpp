#include <obs-module.h>
#include <obs-frontend-api.h>
#include "lower-third-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("dj-lower-third", "en-US")

static LowerThirdDock *dock = nullptr;

bool obs_module_load(void)
{
    auto *mainWindow =
        static_cast<QWidget *>(obs_frontend_get_main_window());

    dock = new LowerThirdDock(mainWindow);

    if (!obs_frontend_add_dock_by_id(
            "dj-lower-third-dock",
            "DJ Lower Third",
            dock)) {
        delete dock;
        dock = nullptr;
        blog(LOG_ERROR, "[DJ Lower Third] Could not create dock.");
        return false;
    }

    blog(LOG_INFO, "[DJ Lower Third] Dock loaded.");
    return true;
}

void obs_module_unload(void)
{
    // OBS owns and destroys the dock widget when the dock is removed.
    obs_frontend_remove_dock("dj-lower-third-dock");
    dock = nullptr;
    blog(LOG_INFO, "[DJ Lower Third] Dock unloaded.");
}
