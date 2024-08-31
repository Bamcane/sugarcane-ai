#ifndef LIB_BASE_H
#define LIB_BASE_H

#include <ctime>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>

using size_t = std::size_t;

#include "string.h"

template<>
struct std::formatter<string>
{
    auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

    auto format(const string& str, format_context& ctx)
    {
        return std::format_to(ctx.out(), "{:s}", str.c_str());
    }
};

#define log_msg(from, msg) \
    std::cout<< std::format("[{:x}][{}] ", time(0), from) << msg << std::endl

#define log_msgf(from, format_str, ...) \
    std::cout<< std::format("[{:x}][{}] ", time(0), from) << std::format(format_str, __VA_ARGS__) << std::endl

#endif // LIB_BASE_H