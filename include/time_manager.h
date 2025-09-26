#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>

// Time structure for easier handling
struct DateTime {
    int year;
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-59
    int weekday; // 0-6 (Sunday = 0)
};

class TimeManager {
private:
    bool time_set;
    DateTime current_time;
    uint32_t last_update;
    
    // Time zone offset (hours from UTC)
    int timezone_offset;
    
    // Private methods
    void updateInternalTime();
    bool isValidDate(int year, int month, int day);
    bool isValidTime(int hour, int minute, int second);
    int getDaysInMonth(int year, int month);
    bool isLeapYear(int year);
    int calculateWeekday(int year, int month, int day);
    
public:
    TimeManager();
    
    // Initialization
    void initialize();
    
    // Time setting
    bool setTime(int hour, int minute, int second = 0);
    bool setDate(int year, int month, int day);
    bool setDateTime(const DateTime& dt);
    bool setUnixTime(uint32_t unix_time);
    
    // Time getting
    DateTime getCurrentTime();
    uint32_t getUnixTime();
    uint64_t getUnixTimeMs();
    uint32_t getTimestamp() { return getUnixTime(); }
    
    // Formatting
    void formatTime24(char* buffer, bool include_seconds = false);
    void formatTime12(char* buffer, bool include_seconds = false);
    void formatDate(char* buffer, bool use_turkish = false);
    void formatDateTime(char* buffer, bool use_turkish = false);
    void formatISO8601(char* buffer);
    void formatFilename(char* buffer); // YYYY-MM-DD format
    void formatEventFilename(char* buffer); // event_YYYY-MM-DDTHH-MM-SS format
    
    // Turkish month names
    const char* getTurkishMonthName(int month);
    
    // Validation
    bool isTimeSet() { return time_set; }
    bool validateTime(int hour, int minute);
    bool validateDate(int year, int month, int day);
    
    // Timezone
    void setTimezone(int offset_hours) { timezone_offset = offset_hours; }
    int getTimezone() { return timezone_offset; }
    
    // Utility
    void update(); // Call regularly to keep internal time current
    uint32_t getUptime(); // Milliseconds since boot
    
    // Time calculations
    uint32_t timeDifference(uint32_t time1, uint32_t time2);
    bool isToday(uint32_t timestamp);
    bool isSameDay(uint32_t timestamp1, uint32_t timestamp2);
    
    // Debug
    void printCurrentTime();
    void printDebugInfo();
};

// Global instance declaration
extern TimeManager timeManager;

#endif // TIME_MANAGER_H