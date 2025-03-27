# API

**Work in progress**

## Show Makefile help

To see which variables can be overriden and which are rules you can call. Type in your console:

```
make help
```

In the case, you want to extend rules of MyMakefile, you can complete the help by:
- documenting default variable using `#? my single-line comment` before the variable.
- documenting a rule using `#? my single-line comment` before the rule.
- constrain: single line comment is only managed!

### Example (Makefile)

```
#? Here is my variable comment
FOO ?= bar

#? Here is my rule comment
foobar:
    ...
```

### Example (console)

```
> make help

Usage:
  [VERBOSE=1] make [flags...] <target>

You can override the following flags:
  FOO               Here is my variable comment
    Default value: bar

Targets:
  foobar:           Here is my rule comment
```

## Debug and verbose your Makefile executions

The following variables can be combined:
- `VERBOSE` (or shorter `V`) when set, will show Makefile rules content when executed.
- `DEBUG` (or shorter `D`) when set, will show shell commands when executed.

The best is to change them when calling the make command, you can combine with
other makefile debug options (`--warn-undefined-variables`, `--just-print`,
`--print-data-base`).

### Example (console)

```
> V=1 D=1 make all --warn-undefined-variables
```

## Change the default C/C++ compiler

You can change default C/C++ compiler. The best is to change them when calling make. i.e.
- `CC` define your C compiler.
- `CXX` define your C++ compiler.
- `AR` define your archiver (static libraries).

**Note:** you can override more variables. Type `make help` to see other.

### Example (console)

```
> make CXX=clang++ all
```

## By-project information

The following variables are mandatory to compile your project and shall be included before including `project/Makefile` and at the top of your Makefile.

- `P` for setting the local path to the project root. Usually `P := .` since top-level Makefile are placed in the project root folder.
- `M` setting the local path to MyMakefile root folder. I suggest you to add `M := $(P)/.makefile` where `.makefile` is this current repo renamed with a `.` to make it hidden. Suggestion git clone as submodule to follow evolutions.
- `PROJECT_NAME` for giving a name to your project. When installing the project on your operating system a top-level folders holding libraries, includes, docs, data ... is created.
- `PROJECT_VERSION` for giving a version for your binaries and libraries. This will avoid you to have conflicts if you have to install several versions of the same project on your operating system. Versioning is `major.minor.patch`.
- `TARGET_NAME` for giving a name to your executable or library target.
- `TARGET_DESCRIPTION` for giving a short description of your target. Some generated files need this information.
- `COMPILATION_MODE` for compiling in `release` (no debug symbols, binary is stripped) or `debug` (gdb symbols plus display stack trace in case of segfault) mode or `normal` (gdb without stack trace) or `test` mode (a shortcut for debug mode with code coverag activated; usefull for unit-test).

The following variables are optional:
- `FORCE_LOWER_CASE` if set then `PROJECT_NAME` and `TARGET_NAME` will be forced to use lower case names.

### Example (Makefile file)

```
P := .
M := $(P)/.makefile

PROJECT_NAME := MySuperProject
PROJECT_VERSION := 0.2.0
TARGET_NAME := $(PROJECT_NAME)
TARGET_DESCRIPTION := This is a real super project
COMPILATION_MODE := release
FORCE_LOWER_CASE := 1

include $(M)/project/Makefile
```

## By-project folders configuration

The following variables are optional to compile your project and complete mandatory variables see in previous section. They shall be included before including `project/Makefile`.

- `BUILD` set to `build` by default. Define the directory name holding the compilation artifacts.
- `THIRDPART` set to `external` by default. Define the directory holding third-party libraries.
- `PROJECT_DATA` set to `data` by default. Define the directory holding project data.
- `PROJECT_DOC` set to `doc` by default. Name of the directory holding documentation.
- `PROJECT_GENERATED_DOC` set to `doc` by default. Name of the directory holding generated documentation.
- `PROJECT_TESTS` set to `tests` by default. Name of the directory holding unit tests.
- `PROJECT_TEMP_DIR` set to `$(TMPDIR)/$(PROJECT_NAME)/$(PROJECT_VERSION)` by default. Define the path where to store temporary project files.

### Example (Makefile file)

```
P := .
M := $(P)/.makefile

PROJECT_NAME := MySuperProject
PROJECT_VERSION := 0.2.0
TARGET_NAME := $(PROJECT_NAME)
TARGET_DESCRIPTION := This is a real super project
COMPILATION_MODE := release

BUILD := out
THIRDPART := thirdparties
PROJECT_DATA := demo/data
PROJECT_DOC := docs
PROJECT_GENERATED_DOC := docs/gen
PROJECT_TESTS := check

include $(M)/project/Makefile
```

## C/C++/Linker

The following variables are optional to compile your project and complete mandatory variables see in previous section. They shall be included before including `project/Makefile`.

- `CXX_STANDARD` define your C++ standard. By default: `--std=c++14`.
- `REGEXP_CXX_HEADER_FILES` set to `*.hpp *.ipp *.tpp *.hh *.h *.hxx *.incl` by default. Define file extensions matching for C++ header files.
- `USER_CXXFLAGS` is empty by default. Allows to overrides MyMakefile `CXX_FLAGS` (C++ compilation flags).
- `USER_CCFLAGS` is empty by default. Allows to overrides MyMakefile `CC_FLAGS` (C compilation flags).
- `USER_LDFLAGS` is empty by default Allows to overrides MyMakefile `CC_FLAGS` (C compilation flags).

### Example (Makefile file)

```
P := .
M := $(P)/.makefile

PROJECT_NAME := MySuperProject
PROJECT_VERSION := 0.2.0
TARGET_NAME := $(PROJECT_NAME)
TARGET_DESCRIPTION := This is a real super project
COMPILATION_MODE := release

CXX_STANDARD := --std=c++11
USER_CXX_FLAGS := -Wno-old-style-cast

include $(M)/project/Makefile
```












- `DOXYGEN_INPUTS` set to `` by default. Define the
- `GENERATED_DOXYGEN_DIR`  set to `$(PROJECT_GENERATED_DOC_DIR)/doxygen` by default. Define the directory where Doxygen is generated.
-
- `GPROF_ANALYSIS` set to `$(PROJECT_GENERATED_DOC_DIR)/profiling/analysis.txt` by default. Define the path of the generated profiling analysis.
- `COVERAGE_RAPPORT`  set to `` by default. Define the
- `MACOS_BUNDLE_ICON` set to `` by default. Define the
- `PATH_PROJECT_LOGO` set to `` by default. Define the






## By-system informations

The following variables are deduced by MyMakefile and can be used after including `project/Makefile`.

- `OS` defines the operating system you are using and building for. Currently we do not cross compile.
Values are:
  - `Linux` for operating system running i.e. on Ubuntu, Debian ...
  - `Windows` for Windows. `WIN_ENV` will define environement such as MinGW ...
  - `Darwin` for MacOS.
  - `Emscripten` or Emscripten and ExaquOS. While they are not real operating system, this allows us to simplify logic.
  To distinguish them use `ifdef EXAEQUOS`.
- `ARCHI`: Windows: 32, 64, Linu: x86_64 ...

Depending on your operating system, the following variables are created:
- `EXT_BINARY` to define application extension (.exe for Windows, .app for MacOS X ...).
- `EXT_DYNAMIC_LIB` to define shared library extension (.so for Linux, .dll for Windows, .dylib for MacOS).
- `EXT_STATIC_LIB` to define shared library extension (.a for Linux, .lib for Windows).



## Makefile rules

### Gprof

- `make USE_GPROF=1 gprof`


## GNU standard paths

See https://www.gnu.org/prep/standards/html_node/Directory-Variables.html
the following variables are supported:

- `DESTDIR` is empty by default. Define where the project will be installed.
- `PREFIX` set to `/usr/local` by default. Define where the project will be installed.
- `INCLUDEDIR` set to `$(PREFIX)/include` by default. Define where to install includes.
- `LIBDIR` set to `$(PREFIX)/lib` by default. Define where to install libraries.
- `PKGLIBDIR` set to `$(LIBDIR)/pkgconfig` by default. Define where to install pkgconfig files.
- `DATADIR` set to `$(PREFIX)/share` by default. Define where to install data and documentation.
- `BINDIR` set to `$(PREFIX)/bin` by default. Define where to install standalone applications.
- `TMPDIR` set to `/tmp` by default. Define where to you can store temporary files.

Note for ExaequOS, the binary will be installed into `/media/localhost/$(TARGET_NAME)/exa` whatever values of `$(DESTDIR)` `$(BINDIR)`.

## Print helpers

- `print-simple($1,$2)` print simple colorfull text.
- `print-from($1,$2,$3)` print simple colorfull text for in-coming information.
- `print-to($1,$2,$3)` print simple colorfull text for out-coming information.

## Commands

- `make run` will call your compiled standalone application (if your project is not a pure library project) or `make run -- your list -of --arguments` to pass `your list -of --arguments` as command line to your application. Note: for Emscripten this will call your web browser.
