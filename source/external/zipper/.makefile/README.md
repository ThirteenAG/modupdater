# MyMakefile

[MyMakefile](https://github.com/Lecrapouille/MyMakefile) is a central build system based on GNU Makefile for compiling my GitHub C++ projects for Linux, Mac OS X (and certainly for Windows) architectures. `MyMakefile` reduces the number of lines to type for my Makefiles by avoiding duplicating the same boring code over all my projects and therefore to make consistent all my Makefiles. In the case, for your personal projects, you are not bored by CMake syntax or not interested with deadling with Makefile rules, you may be interested in this project. Two choices:
* simply copy/paste this repo inside your project.
* or better, use it as a git submodule to track my evolutions.

Here, the list of my personal projects using this repo as git submodule:

* https://github.com/Lecrapouille/TimedPetriNetEditor
* https://github.com/Lecrapouille/Highway
* https://github.com/Lecrapouille/OpenGLCppWrapper
* https://github.com/Lecrapouille/OpenGlassBox
* https://github.com/Lecrapouille/SimTaDyn
* https://github.com/Lecrapouille/SimForth
* https://github.com/Lecrapouille/ChessNeuNeu
* https://github.com/Lecrapouille/zipper
* https://github.com/Lecrapouille/LinkAgainstMyLibs

## Why do I care using this project?

Or maybe: "why not simply using CMake instead of this project?" CMake is, after all, a Makefile generator and is architecture agnostic! The answer would be "maybe yes for big projects", but I personally never liked CMake for its syntax and for generating makefiles containing more lines than an equivalent Makefile, especially for small projects such as mines.

**Similar project:**

- MyMakefile is inspired by this build system https://github.com/Parrot-Developers/alchemy that I used in my previous firm. This project is a portage of Android Makefiles `Android.mk` and can manage several targets in a single file but this project is not fully 100% Makefile. MyMakefile is 90% Makefile and 10% bash scripts and one of its biggest drawback is that it cannot manage two targets with the same Makefile file.

## MyMakefile Features

MyMakefile allows you to:

* Define a target as a binary and/or a static/shared library for Linux, OS X, Windows.
* Write few lines Makefile by hidding the necessity to write rules for compiling files. You can add your own personal Makefile rules anyway.
* In the case of your target is a library, a [pkg-config](https://en.wikipedia.org/wiki/Pkg-config) file is automatically created.
* Define macros for installing your program, its resources, docs, libraries, include files, pkg-config inside your OS in the case you want a `make install` rule.
* Define by default plenty of compilation flags for GCC and clang compilers. Some are made for hardening your binary or striping your release binaries.
* Enable/disable the good optimization flags (-Ox) as well as enabling/disabling asserts (NDEBUG) depending on if your project is in debug or release mode. If you do not like my default compilation/link flags, you can replace them by yours. If you do not like the default compiler, you can tell your own.
* Offer rules such as gcov (code coverage report), Coverity Scan (static analyzer of code), doxgen (documentation generator), asan (AddressSanitizer), check for a hardened target.
* Generate a Doxygen file with project parameters (such as project name, version ...). The generated HTML follows the theme used by the library SFML which is more proper than the default Doxygen theme.
* Have a rule for compressing your project (without .git or generated documentation) in a tar.gz tarball with the date and the version. Names collision of tarballs is managed.
* Auto-generate the Makefile help by parsing your comments placed before rules.
* Work with parallel builds (-jX option, where X is the number of your CPU cores).
* Hide by default all these awful lines made of GCC compilation flags. Colorful information messages are displayed instead
with the percentage of compiled files (in the way of the CMake progress bar).
* Create a build directory where compiled files are placed, instead of being created within source files.
* Generate .d files inside a build directory holding dependencies files (when one header file is modified, dependent source files are also compiled).
* For MacOS, you have option for obtaining bundle applications.

**Current constraint:**
* Currently, you have to define a single target by Makefile file (for example a library and its demo application). This can be easily bypassed as shown in examples given in this document by adding a makefile in a separate folder (source, lib, demo, tests, ...). A WIP solution which both fix and reduce the code is in gestation in the git branch dev-multitargets.

**Prerequisites:**

You have to install:
- a bash interpretor since some commands cannot be directly called as pure Makefile commands.
- the basic calculator `bc` tool: `apt-get install bc` needed for the progress bar (if not present the compilation does not fail).
- if needed, install tools that can be called by MyMakefile: gcov, doxygen, hardening-check: `apt-get install gcovr doxygen devscripts`.

## Builtin utility rules

- `make help` show your makefile rules (the display is an auto-generated).
- `make clean` remove `$(BUILD)` `$(GENDOC)/coverage`, `$(GENDOC)/html` folders.
- `make doc` generate Doxyfile and call doxygen. The report is generated inside `$(GENDOC)/html`.
- `make check-harden` check if you code is hardening.
- `make asan` use Address Sanitizer. `USE_ASAN` shall be set to 1.
- `make gprof` use GNU profiler. `USE_GPROF` shall be set to 1.
- `make coverage` call gcov against your code and generate a code coverage report inside `$(GENDOC)/coverage`. `USE_COVERAGE` shall be set to 1.
- `make coverity-scan` static analyzer of code (only if you have install coverity-scan): a tarball is created that you have to manually upload on their server
for obtaining the report.
- `make tarball` compress your project in tar.gz tarball (without `.git`, `$(BUILD)`, `$(GENDOC)` folders). Name conflicts of tarball are managed.
- `make which-compiler` show which is the default compiler.

The following commands are mine and will not work "as it" for you (see my personal projects to adapt to your project):
- `make download-external-libs` call the bash script located in `$(P)/$(THIRDPART)/download-external-libs.sh`. I use this script as alternative to git submodules. I do not like git submodules and I prefer `repo` but it is too complex for such simple projects as mine. Bash script is more flexbile for my case.
I download github projects inside `$(P)/$(THIRDPART)`, rename them, refactorize them, etc. This command is also used for my continuous integration tests.
- `make compile-external-libs` call the bash script `$(P)//$(THIRDPART)/download-external-libs.sh`. I use this script to compile my downloaded GitHub projects.
You have to call `make download-external-libs` before. This command is also used for my continuous integration tests.
- `make obs` call the bash script `$(P)/.integration/opensuse-build-service.sh`. I use this script for building my projects on the compilation farms OBS (OpenSuse Build Service).

## MyMakefile examples

Examples are given in the `examples/` folder. Else, see my personal projects for more concrete examples.

### Hello-World MyMakefile example

Here is a minimal C++ project using MyMakefile:
```
helloworld/
├── .makefile/
├── VERSION.txt
├── src/
│   ├── main.cpp
│   └── main.hpp
└── Makefile
```

- `.makefile/` is simply this MyMakefile repo cloned with the following command `git clone git@github.com:Lecrapouille/MyMakefile.git --depth=1 .makefile`.
A better solution would to use MyMakefile.git as sub-module for your pincipal project `git submodule add https://github.com/Lecrapouille/MyMakefile.git .makefile`.
I personally add the `.` to hide it in my workspace but this is not mandatory.

- `VERSION.txt` is an ASCII file containing a version number such as `0.1` or `1.0.3`. It seems useless but it has a great role when installing
your project in your operating system: you can install different versions of your project without interfering each others. If not present a default file is created.
*Note: since 2019, our VERSION file shall have a .txt extension, because of possible conflict with C++ VERSION file when using `-I.` with Mac OS X.*

- `src/` is the folder containing your code source (you can use your own name) containing, for this example, a simple hello word named `main.cpp`. The name is not important as well: you can several sub-folders and source files.

- `Makefile` contains, for this example, the following code (explanations come just after) compile c++ files inside the src folder:

```
PROJECT = CheckMyMakefile
TARGET = Test
DESCRIPTION = Project template testing MyMakefile
STANDARD = --std=c++14
BUILD_TYPE = release

P := .
M := $(P)/.makefile
include $(M)/Makefile.header

OBJS += main.o
DEFINES += -DFOO -UBAR
VPATH += src
INCLUDES += -Isrc

all: $(TARGET)

include $(M)/Makefile.footer
```

#### Explanations

* `PROJECT` is the main project name. A project can have several targets (ie: main binary, unit tests, libraries, ...).
* `TARGET` is the name for your compiled binary (or library) by this Makefile.
* `DESCRIPTION` explain your target in few words (optional but used for generated pkg-config files when you compile for libraries).
* `BUILD_TYPE = release` to compile your project without debug elements (else replace it by `̀BUILD_TYPE = debug`).
* `STANDARD = --std=c++14` tell which C++ standard you want (C++14 by default if not set).
* `P` and `M` are mandatory and their placement **SHALL BE AFTER** previously lines. `P` (for Project) indicates the relative path of the folder holding the root project. `M` (for MyMakefile) indicates the location of the folder holding this `MyMakefile` project.
* `OBJS` contains the list of all .o files (separated by spaces) for compiling `TARGET`. Please just give their base names and not their source path. The `+=` is mandatory since some objects are added by `include $(M)/Makefile.header`.
* Use `VPATH` (separated by spaces) to define folders for finding your source files. In our example with have a single folder holding source code: `src`.  The `+=` is mandatory since some paths are added by `include $(M)/Makefile.header`.
* Use `INCLUDES` (prepend by `-I` and separated by spaces) to define folders for finding your header files. In our example with have a single folder holding source code: `src`. The `+=` is mandatory since some includes are added by `include $(M)/Makefile.header`.
* Use `DEFINES` for defining your personal C/C++ macros (if needed).
* `all: $(TARGET)` for building your project. Notice it has empty rule. Since an internal rule manages it already.
* You do not have to write compilation rules or rules such as `clean:` or `doc:` ... rules they are already defined in Makefile.footer. The generated documentation is placed on `$(P)`.
* If you want to add new rules add them before `include $(M)/Makefile.footer`.

#### Compilation for Linux

- To compile it just type `make` or `make -j8` change 8 to the number of cores of your CPU.
- A `./build/Test` binary has been created (by default `BUILD = build/`).
- To display compilation flags, simply compile with `VERBOSE=1 make -j8` or simply `V=1 make -j8`.
- To change the default compiler by yours (for ie clang++-6.0 instead of g++) do: `make CXX=clang++-6.0 -j8`
- If compiled with success, you can test it: `./build/Test` or `make run`.
- To force compiling in release mode: `make BUILD_TYPE=release CXX=clang++-6.0 -j8`
- To force compiling in debug mode: `make BUILD_TYPE=debug CXX=clang++-6.0 -j8`
- To follow GNU directives https://www.gnu.org/prep/standards/html_node/Directory-Variables.html you can override variables `PREFIX`, `DESTDIR`, `INCLUDEDIR`, `LIBDIR`, `PKGLIBDIR`, `BINDIR`, `DATADIR`.
- You can activate the verbose mode with `VERBOSE=1` (or shorter `V=1`) : `V=1 make BUILD_TYPE=debug CXX=clang++-6.0 -j8`.

#### Compilation for Mac OS X

Same principe than for Linux: a binary is created inside `$(BUILD)` folder. If you want to create a bundle application. You can add the following code:

```
ifeq ($(ARCHI),Darwin)
BUILD_MACOS_APP_BUNDLE = 1
APPLE_IDENTIFIER = lecrapouille
MACOS_BUNDLE_ICON = data/icon.icns
endif
```

* The `ifeq ($(ARCHI),Darwin) endif` if optional and allow to generated MacOS application if and only if your Makefile is called from a Mac device and therefore provide creating non-crossed compiled application from ie Linux.
* `BUILD_MACOS_APP_BUNDLE` will add a `all: $(TARGET).app` for you.
* `APPLE_IDENTIFIER` allows to create inside the `Info.plist` a field `com.$(APPLE_IDENTIFIER).$(TARGET)`.
* `MACOS_BUNDLE_ICON` define the path of your bundle application icon. If not set a default icon will be used.

#### Compilation for Windows

TODO.

#### Compilation with Emscripten or Emscripten-exa

To compile your project with [Emscripten](https://emscripten.org) or with [Emscripten-exa](https://github.com/exaequos),
add `emmake` before the `make` command. Where `emmake` is a script offered by Emscripten that calls your makefile but set
before all environement variables for you.

- For Emscripten:

```
make download-external-libs
V=1 emmake make compile-external-libs
V=1 emmake make -j8
emmake make run
```

The `emmake make run` will call `emrun` calling `python -m http.server 8080` and `localhost:8080/project_name.html`.
**Note:** small issue the whole project is compiled back everytime.

- For Emscripten-exa (experimental):

Type the same command than for Emscripten but inside this [Docker](https://github.com/baudaux/docker-exa).

#### Compilation Clang vs. GCC

By default gcc is called (because of `$CXX`). To compile with clang++.

```
make CXX=clang++-11 -j8
```

### Hello-World MyMakefile with unit-tests example

Let suppose you want to add a unit test folder for your project. Just do this:

```
foo/
├── .makefile/
├── Makefile
├── Makefile.common
├── src/
│   ├── main.cpp
│   ├── foo.cpp
│   └── main.hpp
├── test/
│   ├── Makefile
│   ├── tests.cpp
│   ├── tests.hpp
│   └── VERSION.txt
└── VERSION.txt
```

- An optional `Makefile.common` can be added for factorizing information between `Makefile` and `test/Makefile` (such as `PROJECT`, `VPATH`, `INCLUDES`, `DEFINES`, `THIRDPART_LIBS`, `LINKER_FLAGS`) and including `$(M)/Makefile.header`. This `Makefile.common` file is not mandatory.

- Each `Makefile` defines `P` the path to the root of the project (`.` for `./Makefile` and `..` for `test/Makefile`) and include the `Makefile.common`.

Example `Makefile.common`:

```
PROJECT = CheckMyMakefile
include $(M)/Makefile.header
DEFINES += -DFOOBAR
VPATH += $(P)/src
INCLUDES += $(P)/src
```

And `test/Makefile`:

```
TARGET = $(PROJECT)-UnitTest
DESCRIPTION = Unit tests for $(PROJECT)
BUILD_TYPE = release

P := ..
M := $(P)/.makefile
include $(M)/Makefile.common

OBJS = tests.o foo.o

all: $(TARGET)

include $(M)/Makefile.footer
```

### A more complex example

This Makefile show lot of MyMakefile variables. Explainations are given in the next section.

```
P := .
M := $(P)/.makefile

PROJECT = CheckMyMakefile
TARGET = Test
DESCRIPTION = Project template testing MyMakefile
BUILD_TYPE = debug

SUFFIX = .cpp
STANDARD = --std=c++14
CXXFLAGS = -W -Wall
COMPIL_FLAGS = -Wextra

USE_COVERAGE = 1

include $(M)/Makefile.header

OBJS += main.o

DEFINES += -DFOOBAR -UFOO
VPATH += src src/foo
INCLUDES += -Isrc -Iinclude

THIRDPART_LIBS += $(THIRDPART)/foo/libfoo.a
LINKER_FLAGS += -L/not/official/path -lesoteric
PKG_LIBS += gtk+-3.0

all: $(TARGET)

install: $(STATIC_LIB_TARGET) $(SHARED_LIB_TARGET)
  @$(call RULE_INSTALL_DOC)
  @$(call RULE_INSTALL_LIBRARIES)
  @$(call RULE_INSTALL_HEADER_FILES)
  @$(call RULE_INSTALL_PKG_CONFIG)

include $(M)/Makefile.footer
```

## Inside MyMakefile

### Description of useful macros

Explanations of MyMakefile variables:

* **[Mandatory]** `P` shall indicate the relative path to the root folder (`.`, `..` etc).
* **[Mandatory]** `M` shall indicate the relative path of the folder containing the `MyMakefile` repo (usually `M := $(P)/.makefile`).
* **[Mandatory]** `PROJECT` is the main project name.
* **[Mandatory]** `TARGET` is the name of your binary or library that your Makefile file is compiling for. `TARGET` can be equal to `PROJECT` but
for the same project, you may want several binaries and therefore several Makefile targets (for example MyGame-lib, MyGame-exec, and MyGame-unit-tests).
When typing `make install` resources files will be install in their sub-folders (such as PROJECT/PROJECT_VERSION.txt/TARGET/TARGET_VERSION.txt/). The current constraint is
to have one Makefile file for each target (this is not a major problem if your create one folder by sub-project).
* **[Mandatory]** `DESCRIPTION` explain your target in few words. This information is used for pkg-config file when `TARGET` is a library (or soon for [Open Build Service](https://openbuildservice.org/)).
* **[Optional]** `LOGO` the path of your logo for your Doxygen documentation.
* **[Optional]** `DOXY_INPUT` extra paths to search for Doxygen.
* **[Mandatory]** `BUILD_TYPE` allow compile the project either in `release` mode (compiled with -O2 and no gdb symbols) or `debug` mode (compiled with -O0 -g3 and extra compilation flags and use [backward-cpp](https://github.com/bombela/backward-cpp)) or `normal` mode (compiled -O2 -g). This mode is very slow and generates bigger executable size but nice for developement.
* **[Optional]** when `BUILD_TYPE=release` [backward-cpp](https://github.com/bombela/backward-cpp) is disabled.
* **[Optional]** You can activate gprof with `USE_GPROF=1` and `make gprof` rule.
* **[Optional]** You can activate address sanitizer with `USE_ASAN=1` and `make asan` rule.
* **[Optional]** You can activate code covergae with `USE_COVERAGE=1` and `make coverage` rule.
* **[Default=.cpp]** `SUFFIX` allows choosing between a c++ or a c project (Note I've hardly tested C projects).
* **[Default=--std=c++11]** `STANDARD` is only used for c++ projects. It defines the c++ standard (gnu++11, std++14, etc).
* **[Default=external/]** `$(P)/THIRDPART` refers to the folder containing thirdpart libraries
(git cloned from GitHub for example). In my case, in `debug` mode, I compile my project against the https://github.com/bombela/backward-cpp (placed in `$(THIRDPART)` directory) for displaying the stack trace when a segfault occurred.
* **[Default=build/]** `BUILD` refers to the folder holding compiled files.
* **[Default=doc/]** `DOC` refers to the folder holding generated documentation (doxygen ...)
* **[Mandatory]** `OBJS` is the list of object files you want to compile. Do not include their path just their name with the extension `.o` (ie foo.o bar.o).
* **[Optional]** `VPATH` and `INCLUDES` allow to find .c or .cpp and .hpp or .h files. Use the macro `P` in paths (ie `$(P)/src`).
* **[Optional]** `COMPIL_FLAGS` if set it will had extra compilation flags to `CXXFLAGS`. By default `CXXFLAGS` is set with plenty of good compilation flags for clang and gcc.
* **[Optional]** `LINKER_FLAGS` if set it will had extra linkage flags to `LDFLAGS`. By default `LDFLAGS` is set with plenty of good linker flags for clang and gcc.
* **[Optional]** `CXXFLAGS` and `LDFLAGS` allow to replace default values by your own flags (in the case you do not desire to use default compilation/linker flags).
* **[Mandatory]** `Makefile.header` is mandatory else MyMakefile will not be called. Beware of placing it correctly (some variables are nevertheless checked by MyMakefile).
* **[Optional]** `DEFINES` hold your macro definitions.
* **[Optional]** `PKG_LIBS` defines system libraries known by the command `pkg-config` which will add extra flags to `CXXFLAGS` and `LDFLAGS`. In the case you want to force using static library you can add `--static` before the library name. For example: `PKG_LIBS += glew --static glfw3`.
* **[Optional]** `NOT_PKG_CONFIG` defines system libraries unknown from the command `pkg-config`. This will add  extra flags to `CXXFLAGS` and `LDFLAGS`.
* **[Optional]** `THIRDPART_OBJS` and `THIRDPART_LIBS` define .o, .a or .so (.dll or .dylib) files once your third part have been compiled. I personally add in `THIRDPART` shell scripts such as
 `download-external-libs.sh` (called by `make download-external-libs`) and `compile-external-libs.sh` (called by `make compile-external-libs`) to allow to git clone github projects and compile them.
This avoids having git submodules (which I dislike).
* **[Mandatory]** `all:` tell Makefile what to compile. In this case the binary `$(TARGET)` its libraries `$(STATIC_LIB_TARGET)`, `$(SHARED_LIB_TARGET)` and the pkg-config file `$(PKG_FILE)` (when typing `make install`).
* Including `Makefile.footer` is mandatory.

### How are made CXXFLAGS and LDFLAGS?

Here how internally (privately) `CXXFLAGS` and `LDFLAGS` are made:
```
CXXFLAGS := $(CXXFLAGS) $(PKGCFG_CFLAGS) $(OPTIM_FLAGS) $(DEFINES) $(INCLUDES) $(COMPIL_FLAGS)
LDFLAGS := $(LDFLAGS) $(THIRDPART_LIBS) $(NOT_PKG_LIBS) $(PKGCFG_LIBS) $(LINKER_FLAGS)
```

* `:= $(CXXFLAGS)` and `:= $(LDFLAGS)` are predefined flags the `:=` allow you replace values by ypur values.
* `$(PKGCFG_CFLAGS)` is made by `PKG_LIBS` when calling the option `--cflags`.
* `$(PKGCFG_LIBS)` is made by `PKG_LIBS` when calling the option `--libs`.
* `$(OPTIM_FLAGS)` is changed by `BUILD_TYPE=release` or `BUILD_TYPE=debug`.
* `$(DEFINES)` is defined by you.
* `$(INCLUDES)` is defined by you.
* `$(COMPIL_FLAGS)` and `$(LINKER_FLAGS)` are defined by you.
* `$(THIRDPART_LIBS)` are defined by after having compiled your external libraries.
* `$(NOT_PKG_LIBS)` are system libs when not define by a `pkg-config` file.

### Description of installation macros

- You have to write your own install rule (usually named `install:`). Place it after the `all:`.
- Call `sudo make install` to install your project in your system but you can also modify `DESTDIR` and `PREFIX` to tell to the `make install` rule where to install your software:
  - `PREFIX`: determines where the package will go when it is installed, and where it will look for its associated files when it is run. It's what you should use if you're just compiling something for use on a single host. Default value: `/usr/local`
  - `DESTDIR`: is for installing to a temporary directory which is not where the package will be run from.
  - Example: `sudo make DESTDIR=/foo PREFIX=/usr/local/bar install` will install binaries in `/foo/usr/local/bar/bin`.
  - Other macros are `INCLUDEDIR ?= $(PREFIX)/include`, `LIBDIR ?= $(PREFIX)/lib/x86_64-linux-gnu`, `PKGLIBDIR ?= $(LIBDIR)/pkgconfig`, `BINDIR ?= $(PREFIX)/bin`, `DATADIR ?= $(PREFIX)/share`.
  
Some macros are here to help you:
  - `$(call INSTALL_BINARY)` to install your binary into `$(DESTDIR)$(PREFIX)/bin`.
  - `$(call INSTALL_DOCUMENTATION)` to install your documentation into `$(DESTDIR)$(PREFIX)/share/$(PROJECT)/$(TARGET_VERSION.txt)`. This will copy the followinf files and folders: `$(GENDOC) data/, examples/, AUTHORS, LICENSE, README.md, ChangeLog`.
  - `$(call INSTALL_PROJECT_LIBRARIES)` to install shared and static libraries into `$(DESTDIR)$(PREFIX)/lib` and pkg-confile file into `/usr/lib/pkgconfig`.
  - `$(call INSTALL_PROJECT_HEADERS)` to install header files (.hpp and .h) from include/ and src/ folders into `$(DESTDIR)$(PREFIX)/include/$(PROJECT)-$(TARGET_VERSION.txt)`.
  - `$(call INSTALL_PROJECT_INCLUDES)` to install header files (from include/ folders into `$(DESTDIR)$(PREFIX)/include/$(PROJECT)-$(TARGET_VERSION.txt)`.
  - `$(call INSTALL_PROJECT_FOLDER,folder)` to install the folder into `$(DESTDIR)$(PREFIX)/share/$(PROJECT)/$(TARGET_VERSION.txt)`.
  - `$(call INSTALL_THIRDPART_FOLDER,ThirdPartLibrary/src,LibraryName,-name "*.h")` to copy recursively all `h` files from the folder `$(THIRDPART)/ThirdPartLibrary/src` into `$(DESTDIR)$(PREFIX)/include/$(PROJECT)-$(TARGET_VERSION.txt)/LibraryName`. The `-name "*.h"` is a parameter to the command `find`.
  - `$(call INSTALL_THIRDPART_FILES,ThirdPartLibrary,LibraryName,-name "*.h")` to copy all `h` files from the folder `$(THIRDPART)/ThirdPartLibrary/src` into `$(DESTDIR)$(PREFIX)/include/$(PROJECT)-$(TARGET_VERSION.txt)/LibraryName`. The `-name "*.h"` is a parameter to the command `find`.

### Description of internal files

* `Makefile.header`: is the part of your Makefile to be included as header part. It contains the code for knowing your architecture, your compiler, destination folder for installation.
It defines your project folder name (build, doc, external). It also checks against uninitialized variables.
* `Makefile.macros`: contains code for defining paths, libraries/project names, installation ...
* `Makefile.color`: define colorful displays and progress bar for hiding the misery of compilation.
* `Makefile.flags`: add all GCC/clang compilation flags findable in the world and more :)
* `Makefile.help`: Allow Makefile to auto parse and display its own rules.
* `Makefile.footer`: is the part of your Makefile to be included as footer part: it defines a set of Makefile rules (like compiling c++ files or linking the project, ...).
* Some Bash scripts exist and are called by Makefile rules:
 - targz.sh: for creating a backup of the code source project. The code source is compressed. git files, compiled and generated files (like doc) are not taken into account.
 - config.sh: for creating a C++ project_info.hpp file needed when compiling the project.

Order of inclusion:
* `Makefile.header` will include `Makefile.macros` that will include `Makefile.color`.
* `Makefile.footer` will include `Makefile.flags` and `Makefile.help`.
