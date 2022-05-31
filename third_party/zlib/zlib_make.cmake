execute_process(
    COMMAND ./configure  --prefix=${CMAKE_CURRENT_BINARY_DIR}/..
    ERROR_QUIET
)

execute_process(
    COMMAND cmake -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
    ERROR_QUIET
)

execute_process(
    COMMAND make
    ERROR_QUIET
)
execute_process(
    COMMAND make install
    ERROR_QUIET
)

