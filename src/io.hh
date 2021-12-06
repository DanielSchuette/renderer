/* A header-only library providing common I/O routines and a basic logger
 * class.
 *
 * renderer Copyright (C) 2021 Daniel Schuette
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
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

/* @NOTE: Needs some refactoring. It doesn't really make sense to have a
 * templated method in a class that might at some point be overridden. For now,
 * we're probably good though.
 */
class Logger {
public:
    Logger(void) {}
    Logger(const Logger&) = delete;
    Logger(Logger&&)      = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&)      = delete;
    virtual ~Logger(void) {}

    template<printable... Ts>
    [[noreturn]] void fail(Ts&&... args) { fail(args...); exit(1); }

    template<printable... Ts>
    void log(Ts&&... args) { warn(args...); }
};

#endif /* _IO_HH_ */
