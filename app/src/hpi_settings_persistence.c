#include "hpi_settings_persistence.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(hpi_settings_persistence, LOG_LEVEL_INF);

// Current settings instance
static struct hpi_user_settings current_settings;
static bool settings_initialized = false;

// Settings load callback function
static int settings_load_handler(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(key, SETTINGS_USER_HEIGHT_KEY, &next) && !next) {
        if (len != sizeof(current_settings.height)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.height, sizeof(current_settings.height));
        if (rc >= 0) {
            LOG_INF("Loaded height: %d cm", current_settings.height);
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_USER_WEIGHT_KEY, &next) && !next) {
        if (len != sizeof(current_settings.weight)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.weight, sizeof(current_settings.weight));
        if (rc >= 0) {
            LOG_INF("Loaded weight: %d kg", current_settings.weight);
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_HAND_WORN_KEY, &next) && !next) {
        if (len != sizeof(current_settings.hand_worn)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.hand_worn, sizeof(current_settings.hand_worn));
        if (rc >= 0) {
            LOG_INF("Loaded hand worn: %s", current_settings.hand_worn ? "Right" : "Left");
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_TIME_FORMAT_KEY, &next) && !next) {
        if (len != sizeof(current_settings.time_format)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.time_format, sizeof(current_settings.time_format));
        if (rc >= 0) {
            LOG_INF("Loaded time format: %s", current_settings.time_format ? "12H" : "24H");
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_TEMP_UNIT_KEY, &next) && !next) {
        if (len != sizeof(current_settings.temp_unit)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.temp_unit, sizeof(current_settings.temp_unit));
        if (rc >= 0) {
            LOG_INF("Loaded temp unit: %s", current_settings.temp_unit ? "Fahrenheit" : "Celsius");
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_AUTO_SLEEP_KEY, &next) && !next) {
        if (len != sizeof(current_settings.auto_sleep_enabled)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.auto_sleep_enabled, sizeof(current_settings.auto_sleep_enabled));
        if (rc >= 0) {
            LOG_INF("Loaded auto sleep: %s", current_settings.auto_sleep_enabled ? "Enabled" : "Disabled");
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_SLEEP_TIMEOUT_KEY, &next) && !next) {
        if (len != sizeof(current_settings.sleep_timeout)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.sleep_timeout, sizeof(current_settings.sleep_timeout));
        if (rc >= 0) {
            LOG_INF("Loaded sleep timeout: %d seconds", current_settings.sleep_timeout);
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_BACKLIGHT_TIMEOUT_KEY, &next) && !next) {
        if (len != sizeof(current_settings.backlight_timeout)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.backlight_timeout, sizeof(current_settings.backlight_timeout));
        if (rc >= 0) {
            LOG_INF("Loaded backlight timeout: %d seconds", current_settings.backlight_timeout);
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_RAISE_TO_WAKE_KEY, &next) && !next) {
        if (len != sizeof(current_settings.raise_to_wake)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.raise_to_wake, sizeof(current_settings.raise_to_wake));
        if (rc >= 0) {
            LOG_INF("Loaded raise to wake: %s", current_settings.raise_to_wake ? "Enabled" : "Disabled");
        }
        return rc;
    }

    if (settings_name_steq(key, SETTINGS_BUTTON_SOUNDS_KEY, &next) && !next) {
        if (len != sizeof(current_settings.button_sounds)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &current_settings.button_sounds, sizeof(current_settings.button_sounds));
        if (rc >= 0) {
            LOG_INF("Loaded button sounds: %s", current_settings.button_sounds ? "Enabled" : "Disabled");
        }
        return rc;
    }

    return -ENOENT;
}

// Settings handler structure
static struct settings_handler settings_conf = {
    .name = "hpi",
    .h_set = settings_load_handler
};

static void settings_load_defaults(void)
{
    current_settings.height = DEFAULT_USER_HEIGHT;
    current_settings.weight = DEFAULT_USER_WEIGHT;
    current_settings.hand_worn = DEFAULT_HAND_WORN;
    current_settings.time_format = DEFAULT_TIME_FORMAT;
    current_settings.temp_unit = DEFAULT_TEMP_UNIT;
    current_settings.auto_sleep_enabled = DEFAULT_AUTO_SLEEP;
    current_settings.sleep_timeout = DEFAULT_SLEEP_TIMEOUT;
    current_settings.backlight_timeout = DEFAULT_BACKLIGHT_TIMEOUT;
    current_settings.raise_to_wake = DEFAULT_RAISE_TO_WAKE;
    current_settings.button_sounds = DEFAULT_BUTTON_SOUNDS;

    LOG_INF("Default settings loaded");
}

int hpi_settings_persistence_init(void)
{
    int rc;

    if (settings_initialized) {
        return 0;
    }

    // Load default values first
    settings_load_defaults();

    // Initialize settings subsystem
    rc = settings_subsys_init();
    if (rc) {
        LOG_ERR("Settings subsystem init failed: %d", rc);
        return rc;
    }

    // Register our settings handler
    rc = settings_register(&settings_conf);
    if (rc) {
        LOG_ERR("Settings register failed: %d", rc);
        return rc;
    }

    // Load settings from storage
    rc = settings_load();
    if (rc) {
        LOG_WRN("Settings load failed: %d", rc);
        // Continue with defaults if load fails
    }

    settings_initialized = true;
    LOG_INF("Settings persistence initialized successfully");

    return 0;
}

int hpi_settings_load_all(struct hpi_user_settings *settings)
{
    if (!settings_initialized) {
        return -ENODEV;
    }

    if (!settings) {
        return -EINVAL;
    }

    memcpy(settings, &current_settings, sizeof(struct hpi_user_settings));
    return 0;
}

int hpi_settings_save_all(const struct hpi_user_settings *settings)
{
    int rc;

    if (!settings_initialized) {
        return -ENODEV;
    }

    if (!settings) {
        return -EINVAL;
    }

    // Save each setting individually
    rc = settings_save_one(SETTINGS_USER_HEIGHT_KEY, &settings->height, sizeof(settings->height));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_USER_WEIGHT_KEY, &settings->weight, sizeof(settings->weight));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_HAND_WORN_KEY, &settings->hand_worn, sizeof(settings->hand_worn));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_TIME_FORMAT_KEY, &settings->time_format, sizeof(settings->time_format));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_TEMP_UNIT_KEY, &settings->temp_unit, sizeof(settings->temp_unit));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_AUTO_SLEEP_KEY, &settings->auto_sleep_enabled, sizeof(settings->auto_sleep_enabled));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_SLEEP_TIMEOUT_KEY, &settings->sleep_timeout, sizeof(settings->sleep_timeout));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_BACKLIGHT_TIMEOUT_KEY, &settings->backlight_timeout, sizeof(settings->backlight_timeout));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_RAISE_TO_WAKE_KEY, &settings->raise_to_wake, sizeof(settings->raise_to_wake));
    if (rc) goto error;

    rc = settings_save_one(SETTINGS_BUTTON_SOUNDS_KEY, &settings->button_sounds, sizeof(settings->button_sounds));
    if (rc) goto error;

    // Update current settings
    memcpy(&current_settings, settings, sizeof(struct hpi_user_settings));

    LOG_INF("All settings saved successfully");
    return 0;

error:
    LOG_ERR("Failed to save settings: %d", rc);
    return rc;
}

int hpi_settings_save_single(const char *key, const void *value, size_t value_len)
{
    int rc;

    if (!settings_initialized) {
        return -ENODEV;
    }

    if (!key || !value) {
        return -EINVAL;
    }

    rc = settings_save_one(key, value, value_len);
    if (rc) {
        LOG_ERR("Failed to save setting %s: %d", key, rc);
        return rc;
    }

    LOG_DBG("Setting %s saved successfully", key);
    return 0;
}

int hpi_settings_factory_reset(void)
{
    int rc;

    if (!settings_initialized) {
        return -ENODEV;
    }

    // Load defaults
    settings_load_defaults();

    // Save all default values
    rc = hpi_settings_save_all(&current_settings);
    if (rc) {
        LOG_ERR("Factory reset failed: %d", rc);
        return rc;
    }

    LOG_INF("Factory reset completed successfully");
    return 0;
}

const struct hpi_user_settings *hpi_settings_get_current(void)
{
    if (!settings_initialized) {
        return NULL;
    }

    return &current_settings;
}

int hpi_settings_update_and_save(const struct hpi_user_settings *settings)
{
    int rc;

    if (!settings_initialized) {
        return -ENODEV;
    }

    if (!settings) {
        return -EINVAL;
    }

    // Validate settings ranges
    if (settings->height < 100 || settings->height > 250) {
        LOG_ERR("Invalid height: %d", settings->height);
        return -EINVAL;
    }

    if (settings->weight < 30 || settings->weight > 200) {
        LOG_ERR("Invalid weight: %d", settings->weight);
        return -EINVAL;
    }

    if (settings->hand_worn > 1) {
        LOG_ERR("Invalid hand worn: %d", settings->hand_worn);
        return -EINVAL;
    }

    if (settings->time_format > 1) {
        LOG_ERR("Invalid time format: %d", settings->time_format);
        return -EINVAL;
    }

    if (settings->temp_unit > 1) {
        LOG_ERR("Invalid temp unit: %d", settings->temp_unit);
        return -EINVAL;
    }

    if (settings->sleep_timeout < 10 || settings->sleep_timeout > 120) {
        LOG_ERR("Invalid sleep timeout: %d", settings->sleep_timeout);
        return -EINVAL;
    }

    // Save the settings
    rc = hpi_settings_save_all(settings);
    if (rc) {
        return rc;
    }

    return 0;
}
