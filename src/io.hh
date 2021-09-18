#ifndef _IO_HH_
#define _IO_HH_

#include <iostream>

#include "common.hh"

template<typename T>
concept printable = requires (T t, std::ostream& os) {
    { os << t } -> std::same_as<std::ostream&>;
};

constexpr bool LOGGING_ON = true;

template<printable... Ts, std::ostream& os = std::cerr, bool log = LOGGING_ON>
[[noreturn]] void fail(Ts&&... args)
{
    if constexpr (!log) exit(1);
    os << "\x1b[31merror:\x1b[0m ";
    ((os << args), ...);
    os << ".\n";
    exit(1);
}

template<printable... Ts, std::ostream& os = std::cerr, bool log = LOGGING_ON>
void warn(Ts&&... args)
{
    if constexpr (!log) return;

    os << "\x1b[33mwarning:\x1b[0m ";
    ((os << args), ...);
    os << ".\n";
}

#endif /* _IO_HH_ */
