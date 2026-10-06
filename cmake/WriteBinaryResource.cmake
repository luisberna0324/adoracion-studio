# Invoked by the build, once per changed asset. No Python or host JUCE needed.
if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "INPUT, OUTPUT and SYMBOL are required")
endif()
file(READ "${INPUT}" bytes HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
# Bound source line lengths even for the multi-megabyte bundled demo.
string(REGEX REPLACE "([^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "// Generated from a bundled asset. Do not edit.\nnamespace BinaryData {\nstatic const unsigned char data[] = {\n${bytes}0,0};\nconst char* ${SYMBOL} = reinterpret_cast<const char*>(data);\n}\n")
