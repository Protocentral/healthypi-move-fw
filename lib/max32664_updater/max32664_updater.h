enum max32664_updater_device_type
{
    MAX32664_UPDATER_DEV_TYPE_MAX32664C,
    MAX32664_UPDATER_DEV_TYPE_MAX32664D,
};

enum max32664_updater_status
{
    MAX32664_UPDATER_STATUS_IDLE,
    MAX32664_UPDATER_STATUS_IN_PROGRESS,
    MAX32664_UPDATER_STATUS_SUCCESS,
    MAX32664_UPDATER_STATUS_FAILED,
};

void max32664_updater_start(const struct device *dev, enum max32664_updater_device_type type);
void max32664_set_progress_callback(void (*callback)(int progress, int status));