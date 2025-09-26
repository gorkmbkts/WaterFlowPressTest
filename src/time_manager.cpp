#include "time_manager.h"
#include <stdio.h>

TimeManager::TimeManager() {
    time_set = false;
    timezone_offset = 3; // Turkey is UTC+3
    last_update = 0;
    memset(&current_time, 0, sizeof(current_time));
}

void TimeManager::initialize() {
    DEBUG_PRINTLN(F("Initializing time manager..."));
    
    // Set default time if not set
    if (!time_set) {
        current_time.year = 2025;
        current_time.month = 1;
        current_time.day = 1;
        current_time.hour = 12;
        current_time.minute = 0;
        current_time.second = 0;
        current_time.weekday = calculateWeekday(2025, 1, 1);
    }
    
    last_update = millis();
    DEBUG_PRINTLN(F("Time manager initialized"));
}

bool TimeManager::setTime(int hour, int minute, int second) {
    if (!validateTime(hour, minute)) {
        return false;
    }
    
    current_time.hour = hour;
    current_time.minute = minute;
    current_time.second = second;
    
    // Set system time
    struct tm timeinfo;
    timeinfo.tm_year = current_time.year - 1900;
    timeinfo.tm_mon = current_time.month - 1;
    timeinfo.tm_mday = current_time.day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0;
    
    time_t time_val = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = time_val, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    
    last_update = millis();
    
    DEBUG_PRINT(F("Time set to: "));
    DEBUG_PRINT(hour);
    DEBUG_PRINT(F(":"));
    DEBUG_PRINTLN(minute);
    
    return true;
}

bool TimeManager::setDate(int year, int month, int day) {
    if (!validateDate(year, month, day)) {
        return false;
    }
    
    current_time.year = year;
    current_time.month = month;
    current_time.day = day;
    current_time.weekday = calculateWeekday(year, month, day);
    
    // Update system time
    struct tm timeinfo;
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = current_time.hour;
    timeinfo.tm_min = current_time.minute;
    timeinfo.tm_sec = current_time.second;
    timeinfo.tm_isdst = 0;
    
    time_t time_val = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = time_val, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    
    time_set = true;
    last_update = millis();
    
    DEBUG_PRINT(F("Date set to: "));
    DEBUG_PRINT(day);
    DEBUG_PRINT(F("/"));
    DEBUG_PRINT(month);
    DEBUG_PRINT(F("/"));
    DEBUG_PRINTLN(year);
    
    return true;
}

bool TimeManager::setDateTime(const DateTime& dt) {
    if (!validateTime(dt.hour, dt.minute) || !validateDate(dt.year, dt.month, dt.day)) {
        return false;
    }
    
    current_time = dt;
    current_time.weekday = calculateWeekday(dt.year, dt.month, dt.day);
    
    struct tm timeinfo;
    timeinfo.tm_year = dt.year - 1900;
    timeinfo.tm_mon = dt.month - 1;
    timeinfo.tm_mday = dt.day;
    timeinfo.tm_hour = dt.hour;
    timeinfo.tm_min = dt.minute;
    timeinfo.tm_sec = dt.second;
    timeinfo.tm_isdst = 0;
    
    time_t time_val = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = time_val, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    
    time_set = true;
    last_update = millis();
    
    return true;
}

bool TimeManager::setUnixTime(uint32_t unix_time) {
    struct timeval tv = { .tv_sec = unix_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    
    // Update internal time structure
    time_t time_val = unix_time;
    struct tm* timeinfo = localtime(&time_val);
    
    current_time.year = timeinfo->tm_year + 1900;
    current_time.month = timeinfo->tm_mon + 1;
    current_time.day = timeinfo->tm_mday;
    current_time.hour = timeinfo->tm_hour;
    current_time.minute = timeinfo->tm_min;
    current_time.second = timeinfo->tm_sec;
    current_time.weekday = timeinfo->tm_wday;
    
    time_set = true;
    last_update = millis();
    
    return true;
}

DateTime TimeManager::getCurrentTime() {
    updateInternalTime();
    return current_time;
}

uint32_t TimeManager::getUnixTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

uint64_t TimeManager::getUnixTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

void TimeManager::formatTime24(char* buffer, bool include_seconds) {
    updateInternalTime();
    if (include_seconds) {
        snprintf(buffer, 9, "%02d:%02d:%02d", 
            current_time.hour, current_time.minute, current_time.second);
    } else {
        snprintf(buffer, 6, "%02d:%02d", 
            current_time.hour, current_time.minute);
    }
}

void TimeManager::formatTime12(char* buffer, bool include_seconds) {
    updateInternalTime();
    int display_hour = current_time.hour;
    const char* am_pm = "AM";
    
    if (display_hour == 0) {
        display_hour = 12;
    } else if (display_hour > 12) {
        display_hour -= 12;
        am_pm = "PM";
    } else if (display_hour == 12) {
        am_pm = "PM";
    }
    
    if (include_seconds) {
        snprintf(buffer, 12, "%02d:%02d:%02d %s", 
            display_hour, current_time.minute, current_time.second, am_pm);
    } else {
        snprintf(buffer, 9, "%02d:%02d %s", 
            display_hour, current_time.minute, am_pm);
    }
}

void TimeManager::formatDate(char* buffer, bool use_turkish) {
    updateInternalTime();
    if (use_turkish) {
        snprintf(buffer, 20, "%d %s %d", 
            current_time.day, 
            getTurkishMonthName(current_time.month), 
            current_time.year);
    } else {
        snprintf(buffer, 12, "%02d/%02d/%04d", 
            current_time.day, current_time.month, current_time.year);
    }
}

void TimeManager::formatDateTime(char* buffer, bool use_turkish) {
    char date_str[20];
    char time_str[9];
    
    formatDate(date_str, use_turkish);
    formatTime24(time_str, false);
    
    snprintf(buffer, 30, "%s %s", date_str, time_str);
}

void TimeManager::formatISO8601(char* buffer) {
    updateInternalTime();
    snprintf(buffer, 21, "%04d-%02d-%02dT%02d:%02d:%02d", 
        current_time.year, current_time.month, current_time.day,
        current_time.hour, current_time.minute, current_time.second);
}

void TimeManager::formatFilename(char* buffer) {
    updateInternalTime();
    snprintf(buffer, 11, "%04d-%02d-%02d", 
        current_time.year, current_time.month, current_time.day);
}

void TimeManager::formatEventFilename(char* buffer) {
    updateInternalTime();
    snprintf(buffer, 30, "event_%04d-%02d-%02dT%02d-%02d-%02d", 
        current_time.year, current_time.month, current_time.day,
        current_time.hour, current_time.minute, current_time.second);
}

const char* TimeManager::getTurkishMonthName(int month) {
    const char* turkish_months[] = {
        "Ocak", "Subat", "Mart", "Nisan", "Mayis", "Haziran",
        "Temmuz", "Agustos", "Eylul", "Ekim", "Kasim", "Aralik"
    };
    
    if (month >= 1 && month <= 12) {
        return turkish_months[month - 1];
    }
    return "???";
}

bool TimeManager::validateTime(int hour, int minute) {
    return (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59);
}

bool TimeManager::validateDate(int year, int month, int day) {
    if (year < 2020 || year > 2050) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > getDaysInMonth(year, month)) return false;
    return true;
}

void TimeManager::update() {
    // Update internal time from system time periodically
    uint32_t now = millis();
    if (now - last_update > 1000) { // Update every second
        updateInternalTime();
        last_update = now;
    }
}

void TimeManager::updateInternalTime() {
    time_t now;
    struct tm* timeinfo;
    
    time(&now);
    timeinfo = localtime(&now);
    
    current_time.year = timeinfo->tm_year + 1900;
    current_time.month = timeinfo->tm_mon + 1;
    current_time.day = timeinfo->tm_mday;
    current_time.hour = timeinfo->tm_hour;
    current_time.minute = timeinfo->tm_min;
    current_time.second = timeinfo->tm_sec;
    current_time.weekday = timeinfo->tm_wday;
}

bool TimeManager::isValidDate(int year, int month, int day) {
    return validateDate(year, month, day);
}

bool TimeManager::isValidTime(int hour, int minute, int second) {
    return (hour >= 0 && hour <= 23 && 
            minute >= 0 && minute <= 59 && 
            second >= 0 && second <= 59);
}

int TimeManager::getDaysInMonth(int year, int month) {
    const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    if (month < 1 || month > 12) return 0;
    
    int days = days_in_month[month - 1];
    if (month == 2 && isLeapYear(year)) {
        days = 29;
    }
    
    return days;
}

bool TimeManager::isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int TimeManager::calculateWeekday(int year, int month, int day) {
    // Zeller's congruence algorithm
    if (month < 3) {
        month += 12;
        year--;
    }
    
    int k = year % 100;
    int j = year / 100;
    
    int weekday = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    
    // Convert to Sunday = 0 format
    weekday = (weekday + 6) % 7;
    
    return weekday;
}

uint32_t TimeManager::getUptime() {
    return millis();
}

uint32_t TimeManager::timeDifference(uint32_t time1, uint32_t time2) {
    return (time1 > time2) ? (time1 - time2) : (time2 - time1);
}

bool TimeManager::isToday(uint32_t timestamp) {
    time_t now;
    time(&now);
    
    struct tm* today = localtime(&now);
    struct tm* check_time = localtime((time_t*)&timestamp);
    
    return (today->tm_year == check_time->tm_year &&
            today->tm_mon == check_time->tm_mon &&
            today->tm_mday == check_time->tm_mday);
}

bool TimeManager::isSameDay(uint32_t timestamp1, uint32_t timestamp2) {
    struct tm* time1 = localtime((time_t*)&timestamp1);
    struct tm* time2 = localtime((time_t*)&timestamp2);
    
    return (time1->tm_year == time2->tm_year &&
            time1->tm_mon == time2->tm_mon &&
            time1->tm_mday == time2->tm_mday);
}

void TimeManager::printCurrentTime() {
#ifdef DEBUG_MODE
    char datetime_str[30];
    formatDateTime(datetime_str, false);
    Serial.print(F("Current time: "));
    Serial.println(datetime_str);
#endif
}

void TimeManager::printDebugInfo() {
#ifdef DEBUG_MODE
    updateInternalTime();
    Serial.printf("Time: %02d:%02d:%02d Date: %02d/%02d/%04d Weekday: %d\n",
        current_time.hour, current_time.minute, current_time.second,
        current_time.day, current_time.month, current_time.year,
        current_time.weekday);
    Serial.printf("Unix timestamp: %d, Set: %s\n", 
        getUnixTime(), time_set ? "YES" : "NO");
#endif
}