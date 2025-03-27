# MyMakefile

[MyMakefile](https://github.com/Lecrapouille/MyMakefile) is a central build
system based on GNU Makefile for compiling my GitHub C++ projects for Linux, Mac
OS X, [Emscripten](https://emscripten.org/) and
[ExaequOS](https://www.exaequos.com/) (and certainly for Windows)
architectures. To be used for upset people against CMake like me. The aim of
`MyMakefile` is to reduce the number of lines to type for my Makefiles by
avoiding duplicating the same boring code over all my projects and therefore to
make all my Makefiles consistent.

Write in few lines of Makefile by including in your project two files containing
all the complex cookery rules for compiling files, generating shared/static lib,
generating pkg config, creating MacOX bundle application, generating
documentation, runing code coverage, compilation flags, installing on your
system ... is already made for you. You can still extend rules!

Here, the list of my personal projects using the branch version-2 as git
submodule:
- https://github.com/Lecrapouille/TimedPetriNetEditor
- https://github.com/Lecrapouille/OpenGlassBox
- https://github.com/Lecrapouille/zipper

Here, the list of my personal projects using the branch version-1 as git
submodule:
- https://github.com/Lecrapouille/Highway
- https://github.com/Lecrapouille/OpenGLCppWrapper
- https://github.com/Lecrapouille/SimTaDyn
- https://github.com/Lecrapouille/SimForth
- https://github.com/Lecrapouille/ChessNeuNeu
- https://github.com/Lecrapouille/LinkAgainstMyLibs

## Why do I care using this project?

Or maybe: "why not simply using CMake instead of this project?" CMake is, after
all, a Makefile generator and architecture agnostic! The answer would be "maybe
yes for big projects", but I personally never liked CMake for its syntax,
obscure function names and specially for generating Makefiles containing more
lines than an hand-made equivalent, especially for small projects such as mines.

In the case, for your personal projects, you are not bored by CMake syntax or
not interested with deadling with Makefile rules, you may be interested in this
project. Two choices:
- simply copy/paste this repo inside your project.
- or better, use it as a git submodule to track my evolutions.

## MyMakefile API

WIP: See [this document](doc/API.md).

## MyMakefile Features

MyMakefile allows you to:
- Each Makefile allows you to define a binary target and/or a static/shared
  library target for Linux, OS X, Windows.
- In the case of your target is a library, a
  [pkg-config](https://en.wikipedia.org/wiki/Pkg-config) file is automatically
  created.
- Define macros for installing your program, its resources, docs, libraries,
  include files, pkg-config inside your OS in the case you want a `make install`
  rule. By default a minimal installation rules is offered installing
  applications, libraries, pkg files, header files, doc and data.
- Define by default plenty of compilation flags for GCC and clang compilers to
  have plenty of warnings. Some are made for hardening your binary or striping
  your release binaries.
- Enable/disable the good optimization flags (-Ox) as well as enabling/disabling
  asserts (NDEBUG) depending on if your project is in debug or release mode. If
  you do not like my default compilation/link flags, you can replace them by
  yours. If you do not like the default compiler, you can tell your own.
- In debug mode, add automatically a stack trace for helping you fixing segfaults.
- In debug mode, offer debug printf macros.
- Offer rules such as gcov (code coverage report), Coverity Scan (static
  analyzer of code), doxgen (documentation generator), asan (AddressSanitizer),
  check for a hardened target.
- Generate a C++ header file with your project information (version, SHA1 ...)
  that can be used in a logger.
- Generate a Doxygen file with project parameters (such as project name,
  version ...). The generated HTML follows the theme used by the library SFML
  which is more proper than the default Doxygen theme.
- Have a rule for compressing your project (without .git or generated
  documentation) in a tar.gz tarball with the date and the version. Names
  collision of tarballs is managed.
- Auto-generate the Makefile help by parsing your comments placed before rules.
- Work with parallel builds (-jX option, where X is the number of your CPU cores).
- Hide by default all these awful lines made of GCC compilation flags. Colorful
information messages are displayed instead with the percentage of compiled files
(in the way of the CMake progress bar).
- Create a build directory where compiled files are placed, instead of being
  created within source files.
- Generate .d files inside a build directory holding dependencies files (when
  one header file is modified, dependent source files are also compiled).
- For MacOS, you have option for obtaining bundle applications.

**Current constraint:**

- Currently, you have to define a single target by Makefile file (for example a
  library and its demo application). This can be easily bypassed as shown in
  examples given in this document by adding a makefile in a separate folder
  (source, lib, demo, tests, ...). A WIP solution which both fix and reduce the
  code is in gestation in the git branch dev-multitargets.

**Prerequisites:**

You have to install:
- a bash interpretor since some Makefile code cannot be directly called.
- the basic calculator `bc` tool: `apt-get install bc` needed for the progress
  bar (if not present the compilation does not fail but the percent of progress
  is crapped).
- if needed, install tools that can be called by MyMakefile: gcov, doxygen,
  hardening-check: `apt-get install gcovr doxygen devscripts`.

## How to compile for ExaequOS ?

ExaequOS is a new project: a fork of Emscripten for managing posix features such
as forks, signals, tty, Wayland and more ...

- Install the ExaequOS Docker image. Follow instructions
  https://github.com/Lecrapouille/docker-exa
- Go to your project using MyMakefile.
- Run the ExaequOS Docker image against the folder holding your project.
- Simply call `make`! Your project will compile like if you were compiling a
  Linux application. Do not worry about Emscripten compiler. All is hidden for
  you!
- Once compiled, call `make install`.
- Open you favorite browser to the URL https://www.exaequos.com/
- Run Havoc, the ExaequOS console and type `/media/localhost/<my-application>`
  where `<my-application>` has to be changed for your application name.
