#include <iostream>
#include "project_info.hpp"

int main()
{
  std::cout << "Welcome to your project " << project::info::project_name << " version "
            << project::info::major_version
            << '.'
            << project::info::minor_version
            << " compiled in "
            << (project::info::mode == project::info::debug ? "debug" : "release")
            << " mode "
            << std::endl
            << "Your code was git cloned on branch " << project::info::git_branch
            << " SHA1 " << project::info::git_sha1
            << std::endl;
  return 0;
}
