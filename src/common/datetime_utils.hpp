#pragma once

#include <cmath>
#include <string>
#include <cstdio>
#include "defs.h"
#include "errors.h"

namespace datetime {

inline std::string to_string(datetime_t datetime) {
    auto from_bcd = [&] (size_t offset, size_t len) {
        uint64_t res = 0, field = ((datetime >> offset) & ((1 << (4 * len)) - 1));
        for (size_t i = 0; i < len; i += 1, field >>= 4)
            res += std::pow(10, i) * (field & 0b1111);
        return res;
    };

    char buffer[20] = "XXXX-XX-XX XX:XX:XX";
    std::sprintf(buffer +  0, "%04lu", from_bcd(40, 4));
    buffer[4] = '-';
    std::sprintf(buffer +  5, "%02lu", from_bcd(32, 2));
    buffer[7] = '-';
    std::sprintf(buffer +  8, "%02lu", from_bcd(24, 2));
    buffer[10] = ' ';
    std::sprintf(buffer + 11, "%02lu", from_bcd(16, 2));
    buffer[13] = ':';
    std::sprintf(buffer + 14, "%02lu", from_bcd( 8, 2));
    buffer[16] = ':';
    std::sprintf(buffer + 17, "%02lu", from_bcd( 0, 2));
    return std::string(buffer);
}

// 输入样例: 2023-05-30 12:34:32
inline uint64_t to_bcd(std::string datetime) {
    uint64_t year  = std::stoull(datetime.substr(0,  4));
    uint64_t month = std::stoull(datetime.substr(5,  2));
    uint64_t day   = std::stoull(datetime.substr(8,  2));
    uint64_t hour  = std::stoull(datetime.substr(11, 2));
    uint64_t min   = std::stoull(datetime.substr(14, 2));
    uint64_t sec   = std::stoull(datetime.substr(17, 2));

    if (
        year  < 1000 || year  > 9999 ||
        month < 1    || month > 12 ||
        day   < 1    || day   > 31 ||
        hour  < 0    || hour  > 23 ||
        min   < 0    || min   > 59 ||
        sec   < 0    || sec   > 59
    ) {
        throw InternalError("Illegal datatime.");
    }

    bool is_valid_day = [&] () {
        if (month == 4 || month == 6 || month == 9 || month == 11) {
            if (day > 30) return false;
        } else if (month == 2) {
            if (((year % 4) == 0 && (year % 100) == 0) || (year % 400 == 0)) {
                if (day > 29) return false;
            } else {
                if (day > 28) return false;
            }
        }
        return true;
        // 已经判断过所有天数都不大于31 不再需要判断正常月份
    } ();

    if (!is_valid_day)
        throw InternalError("Illegal datatime.");

    auto to_bcd = [] (uint64_t num, size_t len) {
        uint64_t res = 0;
        for (size_t i = 0; len; len -= 1, num /= 10, i += 4)
            res |= ((num % 10) << i);
        return res;
    };

    return ((to_bcd(sec,  2) <<  0) |
            (to_bcd(min,  2) <<  8) |
            (to_bcd(hour, 2) << 16) |
            (to_bcd(day,  2) << 24) |
            (to_bcd(month,2) << 32) |
            (to_bcd(year, 4) << 40));
}

}