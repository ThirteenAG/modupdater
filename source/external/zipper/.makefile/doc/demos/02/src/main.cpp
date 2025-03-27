#include "foo.hpp"
#include "bar.hpp"

#include <iostream>

int main(int argc, char* argv[])
{
  for (int i = 0; i < argc; ++i)
  {
    std::cout << argv[i] << std::endl;
  }

  foo(bar());
  return 0;
}
