#include "src/foo.cpp"
#include <cassert>

void test_foo()
{
    int res = hello(1, 1);
    assert(res == 2);
}

int main()
{
    test_foo();
    return 0;
}
