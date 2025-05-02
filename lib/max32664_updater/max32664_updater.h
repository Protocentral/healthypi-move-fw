enum max32664_updater_device_type
{
    MAX32664_UPDATER_DEV_TYPE_MAX32664C,
    MAX32664_UPDATER_DEV_TYPE_MAX32664D,
};

void max32664_updater_start(const struct device *dev, enum max32664_updater_device_type type);