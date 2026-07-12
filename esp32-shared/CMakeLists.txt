idf_component_register(
    SRCS
        "src/telemetry_protocol.c"
        "third_party/mpack/mpack.c"
    INCLUDE_DIRS
        "include"
        "third_party/mpack")

# The telemetry protocol uses only MPack's fixed-buffer reader, expect API,
# and fixed-buffer writer. Exclude the dynamic tree and builder APIs.
target_compile_definitions(${COMPONENT_LIB} PRIVATE MPACK_NODE=0 MPACK_BUILDER=0)
