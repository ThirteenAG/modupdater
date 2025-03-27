#include "foo.hpp"
#include <cassert>
#include <iostream>

int main()
{
    int res = foo(1, 1);
    assert(res == 2);

    std::cout << "all tests good !" << std::endl;

    return 0;
}