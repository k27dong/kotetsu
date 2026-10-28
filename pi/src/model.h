#pragma once

struct civil_date {
    int year;
    int month;
    int day;
};

struct date_line {
    const char *pill;
    const char *prefix;
    char lunar[32];
    bool rest_day;
};

enum weather_icon {
    WEATHER_ICON_SUN,
    WEATHER_ICON_PARTLY,
    WEATHER_ICON_CLOUD,
    WEATHER_ICON_FOG,
    WEATHER_ICON_DRIZZLE,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_HEAVY_RAIN,
    WEATHER_ICON_STORM,
    WEATHER_ICON_SLEET,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_HEAVY_SNOW,
    WEATHER_ICON_OFFLINE,
};

enum weather_line_kind {
    WEATHER_LINE_WORD,
    WEATHER_LINE_BADGE,
    WEATHER_LINE_UPDATED,
    WEATHER_LINE_UNAVAILABLE,
};

struct weather_view {
    enum weather_icon icon;
    char temperature[16];
    char range[32];
    char line[64];
    enum weather_line_kind line_kind;
};

enum small_icon {
    SMALL_ICON_HEART,
    SMALL_ICON_RINGS,
    SMALL_ICON_CAKE,
    SMALL_ICON_CAT,
    SMALL_ICON_HEARTS,
    SMALL_ICON_STAR,
};

struct screen_model {
    bool wifi_setup;
    const char *phrase;
    int day_of_month;
    const char *month_name;
    const char *weekday_name;
    struct date_line date_line;
    struct weather_view weather;
    int counter_days;
    bool has_greeting;
    enum small_icon greeting_icon;
    const char *greeting;
};
