#ifndef _IO_HH_
#define _IO_HH_

#include <iostream>

#include "common.hh"

template<typename T>
concept printable = requires (T t, std::ostream& os) {
    { os << t } -> std::same_as<std::ostream&>;
};

template<printable... Ts>
[[noreturn]] void fail(Ts&&... args)
{
    ((std::cerr << args), ...);
    std::cerr << '\n';
    exit(1);
}

#endif /* _IO_HH_ */
