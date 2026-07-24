/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "hpi_settings_store.h"
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include <stdint.h>

LOG_MODULE_REGISTER(hpi_settings_store, LOG_LEVEL_DBG);

// Settings file path
#define SETTINGS_FILE_PATH "/lfs/user_settings.bin"
#define SETTINGS_FILE_VERSION 2   /* v2: 12-hour time default (migrated from v1) */

// File header structure for version control and validation
struct settings_file_header {
    uint32_t magic;       // Magic number for file validation
    uint16_t version;     // File format version
    uint16_t crc;         // CRC16 of settings data
    uint32_t size;        // Size of settings data
};

#define SETTINGS_MAGIC 0x48505321  // "HPS!" in ASCII

// Current settings instance
static struct hpi_user_settings current_settings;
static bool settings_initialized = false;

// CRC16 calculation for data integrity
static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

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
    current_settings.utc_offset_sec = DEFAULT_UTC_OFFSET;

    /*LOG_INF("Default settings loaded:");
    LOG_INF("  Height: %d cm", current_settings.height);
    LOG_INF("  Weight: %d kg", current_settings.weight);
    LOG_INF("  Hand worn: %s", current_settings.hand_worn ? "Right" : "Left");
    LOG_INF("  Time format: %s", current_settings.time_format ? "12H" : "24H");
    LOG_INF("  Temperature unit: %s", current_settings.temp_unit ? "Fahrenheit" : "Celsius");
    LOG_INF("  Auto sleep: %s", current_settings.auto_sleep_enabled ? "Enabled" : "Disabled");
    LOG_INF("  Sleep timeout: %d seconds", current_settings.sleep_timeout);
    */
}

/* Range-check every field, repairing anything out of range to its default.
 * Returns the number of fields healed (0 = the record was already sound).
 *
 * This exists because a corrupted record ALREADY SHIPPED. The old
 * device-user-settings menu saved via hpi_settings_save_all() -- a wholesale,
 * unvalidated memcpy -- from a `current_ui_settings` struct that only the menu
 * itself ever populated. P6 made that menu unreachable while leaving its
 * Height/Weight rollers live, so the struct stayed ZEROED in BSS and moving
 * either roller persisted every other field as 0. (Fixed at the source: the
 * pickers now use the per-key API, and the orphan file is gone.)
 *
 * But the fix only stops NEW corruption. A watch bitten before it still has
 * auto_sleep_enabled = 0 on flash, which makes get_sleep_timeout_ms() return
 * UINT32_MAX -- "never sleep" -- so it just quietly burns battery forever. There
 * is no UI to repair it: auto_sleep_enabled has a getter and no setter, and
 * settings_read_from_file() accepts any CRC-valid record without looking at the
 * values. Healing on load is the only path back for those units.
 *
 * NOTE the bool asymmetry: auto_sleep_enabled = false is a LEGAL value, so it is
 * indistinguishable from the corruption. It is healed to the default anyway --
 * nothing can currently set it false on purpose (no setter, no UI), so a false on
 * flash is far likelier to be damage than intent. Revisit if a real
 * "never sleep" toggle is ever added; it would then need its own key.
 *
 * (Two comments -- hpi_user_settings_api.c and scr_settings.c -- already referred
 * to "hpi_settings_validate()" as though it existed. It did not; the only range
 * check was inlined in hpi_settings_update_and_save(), which itself has no
 * callers. This is that function, made real.) */
static int hpi_settings_validate(struct hpi_user_settings *s)
{
    int healed = 0;

    if (s->height < 100 || s->height > 250) {
        LOG_WRN("settings: height %u out of range -> %u", s->height, DEFAULT_USER_HEIGHT);
        s->height = DEFAULT_USER_HEIGHT;
        healed++;
    }
    if (s->weight < 30 || s->weight > 200) {
        LOG_WRN("settings: weight %u out of range -> %u", s->weight, DEFAULT_USER_WEIGHT);
        s->weight = DEFAULT_USER_WEIGHT;
        healed++;
    }
    if (s->hand_worn > 1) {
        LOG_WRN("settings: hand_worn %u invalid -> %u", s->hand_worn, DEFAULT_HAND_WORN);
        s->hand_worn = DEFAULT_HAND_WORN;
        healed++;
    }
    if (s->time_format > 1) {
        LOG_WRN("settings: time_format %u invalid -> %u", s->time_format, DEFAULT_TIME_FORMAT);
        s->time_format = DEFAULT_TIME_FORMAT;
        healed++;
    }
    if (s->temp_unit > 1) {
        LOG_WRN("settings: temp_unit %u invalid -> %u", s->temp_unit, DEFAULT_TEMP_UNIT);
        s->temp_unit = DEFAULT_TEMP_UNIT;
        healed++;
    }
    if (s->sleep_timeout < 10 || s->sleep_timeout > 120) {
        LOG_WRN("settings: sleep_timeout %u out of range -> %u",
                s->sleep_timeout, DEFAULT_SLEEP_TIMEOUT);
        s->sleep_timeout = DEFAULT_SLEEP_TIMEOUT;
        healed++;
    }
    if (!s->auto_sleep_enabled && DEFAULT_AUTO_SLEEP) {
        /* see the bool-asymmetry note above */
        LOG_WRN("settings: auto_sleep_enabled was false (never-sleep) -> default");
        s->auto_sleep_enabled = DEFAULT_AUTO_SLEEP;
        healed++;
    }

    return healed;
}

static int settings_write_to_file(void);

static int settings_read_from_file(void)
{
    struct fs_file_t file;
    struct settings_file_header header;
    int rc;
    
    fs_file_t_init(&file);
    
    LOG_DBG("Attempting to read settings from %s", SETTINGS_FILE_PATH);
    
    rc = fs_open(&file, SETTINGS_FILE_PATH, FS_O_READ);
    if (rc) {
        LOG_DBG("Settings file does not exist or cannot be opened: %d", rc);
        return rc;
    }
    
    // Read header
    rc = fs_read(&file, &header, sizeof(header));
    if (rc < 0) {
        LOG_ERR("Failed to read settings file header: %d", rc);
        goto close_file;
    }
    
    if (rc != sizeof(header)) {
        LOG_ERR("Settings file header incomplete: %d/%d bytes", rc, sizeof(header));
        rc = -EIO;
        goto close_file;
    }
    
    // Validate header
    if (header.magic != SETTINGS_MAGIC) {
        LOG_ERR("Invalid settings file magic: 0x%08x", header.magic);
        rc = -EINVAL;
        goto close_file;
    }
    
    if (header.version != SETTINGS_FILE_VERSION) {
        LOG_WRN("Settings file version mismatch: %d (expected %d)",
                header.version, SETTINGS_FILE_VERSION);
        /* v1 -> v2: preserve the user's settings but adopt the new time default
         * (12-hour). Anything else falls through to a defaults rewrite. */
        if (header.version == 1 && header.size == sizeof(struct hpi_user_settings)) {
            rc = fs_read(&file, &current_settings, sizeof(current_settings));
            fs_close(&file);
            if (rc == sizeof(current_settings)) {
                current_settings.time_format = DEFAULT_TIME_FORMAT;
                LOG_INF("Migrated settings v1->v2 (time format -> 12H)");
                (void)settings_write_to_file();
                return 0;
            }
            return -EIO;
        }
        rc = -EINVAL;
        goto close_file;
    }
    
    if (header.size != sizeof(struct hpi_user_settings)) {
        LOG_ERR("Settings file size mismatch: %d (expected %d)", 
                header.size, sizeof(struct hpi_user_settings));
        rc = -EINVAL;
        goto close_file;
    }
    
    // Read settings data
    rc = fs_read(&file, &current_settings, sizeof(current_settings));
    if (rc < 0) {
        LOG_ERR("Failed to read settings data: %d", rc);
        goto close_file;
    }
    
    if (rc != sizeof(current_settings)) {
        LOG_ERR("Settings data incomplete: %d/%d bytes", rc, sizeof(current_settings));
        rc = -EIO;
        goto close_file;
    }
    
    // Verify CRC
    uint16_t calculated_crc = crc16((uint8_t *)&current_settings, sizeof(current_settings));
    if (calculated_crc != header.crc) {
        LOG_ERR("Settings file CRC mismatch: 0x%04x (expected 0x%04x)", 
                calculated_crc, header.crc);
        rc = -EINVAL;
        goto close_file;
    }
    
    LOG_INF("Settings loaded successfully from file");
    LOG_DBG("Loaded settings: height=%d, weight=%d, hand_worn=%d, time_format=%d", 
            current_settings.height, current_settings.weight, 
            current_settings.hand_worn, current_settings.time_format);
    rc = 0;
    
close_file:
    fs_close(&file);
    return rc;
}

static int settings_write_to_file(void)
{
    struct fs_file_t file;
    struct settings_file_header header;
    int rc;
    
    LOG_DBG("Attempting to write settings to %s", SETTINGS_FILE_PATH);
    LOG_DBG("Settings to save: height=%d, weight=%d, hand_worn=%d, time_format=%d", 
            current_settings.height, current_settings.weight, 
            current_settings.hand_worn, current_settings.time_format);
    
    // Prepare header
    header.magic = SETTINGS_MAGIC;
    header.version = SETTINGS_FILE_VERSION;
    header.size = sizeof(struct hpi_user_settings);
    header.crc = crc16((uint8_t *)&current_settings, sizeof(current_settings));
    
    fs_file_t_init(&file);
    
    rc = fs_open(&file, SETTINGS_FILE_PATH, FS_O_CREATE | FS_O_WRITE);
    if (rc) {
        LOG_ERR("Failed to create settings file: %d", rc);
        return rc;
    }
    
    // Write header
    rc = fs_write(&file, &header, sizeof(header));
    if (rc < 0) {
        LOG_ERR("Failed to write settings file header: %d", rc);
        goto close_file;
    }
    
    if (rc != sizeof(header)) {
        LOG_ERR("Settings file header write incomplete: %d/%d bytes", rc, sizeof(header));
        rc = -EIO;
        goto close_file;
    }
    
    // Write settings data
    rc = fs_write(&file, &current_settings, sizeof(current_settings));
    if (rc < 0) {
        LOG_ERR("Failed to write settings data: %d", rc);
        goto close_file;
    }
    
    if (rc != sizeof(current_settings)) {
        LOG_ERR("Settings data write incomplete: %d/%d bytes", rc, sizeof(current_settings));
        rc = -EIO;
        goto close_file;
    }
    
    // Sync to ensure data is written
    rc = fs_sync(&file);
    if (rc) {
        LOG_ERR("Failed to sync settings file: %d", rc);
        goto close_file;
    }
    
    LOG_INF("Settings saved successfully to file");
    rc = 0;
    
close_file:
    fs_close(&file);
    return rc;
}

int hpi_settings_store_init(void)
{
    int rc;

    if (settings_initialized) {
        return 0;
    }

    // Load default values first
    settings_load_defaults();

    // Try to read settings from file (filesystem should already be mounted)
    rc = settings_read_from_file();
    if (rc) {
        LOG_WRN("Failed to read settings from file: %d, using defaults", rc);
        // Save defaults to create initial file
        rc = settings_write_to_file();
        if (rc) {
            LOG_ERR("Failed to create initial settings file: %d", rc);
            return rc;
        }
    } else {
        /* A CRC-valid record is not necessarily a SANE one: the read path checks
         * the checksum, never the values. Heal anything out of range and write
         * the repair straight back, so a watch corrupted by the old
         * save_all-from-a-zeroed-struct bug fixes itself on the next boot instead
         * of silently never sleeping forever. See hpi_settings_validate(). */
        int healed = hpi_settings_validate(&current_settings);
        if (healed > 0) {
            LOG_WRN("settings: healed %d out-of-range field(s) on load; rewriting", healed);
            if (settings_write_to_file() != 0) {
                /* Not fatal - the in-RAM copy is already repaired, so this boot
                 * behaves. It just heals again next time. */
                LOG_ERR("settings: could not persist healed record");
            }
        }
    }

    settings_initialized = true;
    LOG_INF("Settings store initialized successfully");

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

    // Update current settings
    memcpy(&current_settings, settings, sizeof(struct hpi_user_settings));

    // Write to file
    rc = settings_write_to_file();
    if (rc) {
        LOG_ERR("Failed to save settings to file: %d", rc);
        return rc;
    }

    LOG_INF("All settings saved successfully");
    return 0;
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

    // Update the specific setting in current_settings
    if (strcmp(key, SETTINGS_USER_HEIGHT_KEY) == 0 && value_len == sizeof(current_settings.height)) {
        memcpy(&current_settings.height, value, value_len);
        LOG_INF("Updated height: %d cm", current_settings.height);
    }
    else if (strcmp(key, SETTINGS_USER_WEIGHT_KEY) == 0 && value_len == sizeof(current_settings.weight)) {
        memcpy(&current_settings.weight, value, value_len);
        LOG_INF("Updated weight: %d kg", current_settings.weight);
    }
    else if (strcmp(key, SETTINGS_HAND_WORN_KEY) == 0 && value_len == sizeof(current_settings.hand_worn)) {
        memcpy(&current_settings.hand_worn, value, value_len);
        LOG_INF("Updated hand worn: %s", current_settings.hand_worn ? "Right" : "Left");
    }
    else if (strcmp(key, SETTINGS_TIME_FORMAT_KEY) == 0 && value_len == sizeof(current_settings.time_format)) {
        memcpy(&current_settings.time_format, value, value_len);
        LOG_INF("Updated time format: %s", current_settings.time_format ? "12H" : "24H");
    }
    else if (strcmp(key, SETTINGS_TEMP_UNIT_KEY) == 0 && value_len == sizeof(current_settings.temp_unit)) {
        memcpy(&current_settings.temp_unit, value, value_len);
        LOG_INF("Updated temp unit: %s", current_settings.temp_unit ? "Fahrenheit" : "Celsius");
    }
    else if (strcmp(key, SETTINGS_AUTO_SLEEP_KEY) == 0 && value_len == sizeof(current_settings.auto_sleep_enabled)) {
        memcpy(&current_settings.auto_sleep_enabled, value, value_len);
        LOG_INF("Updated auto sleep: %s", current_settings.auto_sleep_enabled ? "Enabled" : "Disabled");
    }
    else if (strcmp(key, SETTINGS_SLEEP_TIMEOUT_KEY) == 0 && value_len == sizeof(current_settings.sleep_timeout)) {
        memcpy(&current_settings.sleep_timeout, value, value_len);
        LOG_INF("Updated sleep timeout: %d seconds", current_settings.sleep_timeout);
    }
    else if (strcmp(key, SETTINGS_BACKLIGHT_TIMEOUT_KEY) == 0 && value_len == sizeof(current_settings.backlight_timeout)) {
        memcpy(&current_settings.backlight_timeout, value, value_len);
        LOG_INF("Updated backlight timeout: %d seconds", current_settings.backlight_timeout);
    }
    else if (strcmp(key, SETTINGS_RAISE_TO_WAKE_KEY) == 0 && value_len == sizeof(current_settings.raise_to_wake)) {
        memcpy(&current_settings.raise_to_wake, value, value_len);
        LOG_INF("Updated raise to wake: %s", current_settings.raise_to_wake ? "Enabled" : "Disabled");
    }
    else if (strcmp(key, SETTINGS_BUTTON_SOUNDS_KEY) == 0 && value_len == sizeof(current_settings.button_sounds)) {
        memcpy(&current_settings.button_sounds, value, value_len);
        LOG_INF("Updated button sounds: %s", current_settings.button_sounds ? "Enabled" : "Disabled");
    }
    else if (strcmp(key, SETTINGS_UTC_OFFSET_KEY) == 0 && value_len == sizeof(current_settings.utc_offset_sec)) {
        memcpy(&current_settings.utc_offset_sec, value, value_len);
        LOG_INF("Updated UTC offset: %d sec", current_settings.utc_offset_sec);
    }
    else {
        LOG_ERR("Unknown setting key: %s", key);
        return -EINVAL;
    }

    // Save entire settings structure to file
    rc = settings_write_to_file();
    if (rc) {
        LOG_ERR("Failed to save settings to file: %d", rc);
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

    // Save default values to file
    rc = settings_write_to_file();
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
