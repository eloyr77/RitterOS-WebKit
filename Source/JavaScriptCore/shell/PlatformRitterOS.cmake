# (RitterOS) abort() prints a backtrace: WTF/wtf/ritteros/AbortBacktraceRitterOS.cpp,
# the program's own abort, exported so that ld.so binds the libraries' calls to it.
list(APPEND jsc_SOURCES ${WTF_DIR}/wtf/ritteros/AbortBacktraceRitterOS.cpp)
cmake_language(DEFER CALL target_link_options jsc PRIVATE "--export-dynamic-symbol=abort")
