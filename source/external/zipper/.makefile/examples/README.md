# Some examples that can be achieved with MyMakefile

## [Example 00](00)

Basic hello word project.

To compile and run the main application:
- Optionally: `make clean`
- `make` or if you have multicore `make -j8` (adapt the `8` to your CPU number of cores)
- `make run` or `./build/HelloWorld00`

The Makefile generate a file `./build/project_info.hpp` and the example shows how to use
it.

## [Example 01](01)

Basic hello word project with unit tests and Doxygen code.

To compile and run the main application:
- Optionally: `make clean`
- `make` or if you have multicore `make -j8` (adapt the `8` to your CPU number of cores)
- `make run` or `./build/HelloWorld01`

To compile and run unit tests of the main application:
- Optionally: `make veryclean`
- `make check` or if you have multicore `make check -j8` (adapt the `8` to your CPU number of cores)

If a gcov have been installed you will see an coverage report.

## [Example 02](02)

Basic hello word project with two internal libraries.

To compile and run the main application:
- Optionally: `make clean`
- `make` or if you have multicore `make -j8` (adapt the `8` to your CPU number of cores)
- `make run` or `./build/HelloWorld00`
