#include "foo.hpp"
#include <iostream>

int foo(int a, int b)
{
    std::cout
        << "foo "
        << a << " + " << b
        << " = " << a + b
        << std::endl;
    return a + b;
}
