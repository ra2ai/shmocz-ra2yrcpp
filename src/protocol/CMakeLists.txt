set(BDIR "${CMAKE_CURRENT_BINARY_DIR}")
set(RA2YRPROTO_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/ra2yrproto"
    CACHE PATH "Checkout containing ra2yrproto/*.proto")
file(
  GLOB PROTO_FILES
  RELATIVE "${RA2YRPROTO_SOURCE_DIR}"
  "${RA2YRPROTO_SOURCE_DIR}/ra2yrproto/*.proto")
if(NOT PROTO_FILES)
  message(FATAL_ERROR "No ra2yrproto/*.proto in ${RA2YRPROTO_SOURCE_DIR}")
endif()
message(STATUS "ra2yrproto schema: ${RA2YRPROTO_SOURCE_DIR}")

# set protobuf file paths
set(PROTO_HDRS "")
set(PROTO_SRCS "")
set(PROTO_DEPS "")
foreach(X IN LISTS PROTO_FILES)
  string(REPLACE ".proto" ".pb.h" FN_H ${X})
  string(REPLACE ".proto" ".pb.cc" FN_S ${X})
  list(APPEND PROTO_HDRS ${FN_H})
  list(APPEND PROTO_SRCS ${FN_S})
  list(APPEND PROTO_DEPS "${RA2YRPROTO_SOURCE_DIR}/${X}")
  unset(FN_H)
  unset(FN_S)
endforeach()

# A replacement .proto file can be older than the sources generated from its
# predecessor. Record checksums on configure so a content change still triggers
# protoc on the next build.
set(SCHEMA_CHECKSUMS_FILE "${BDIR}/schema-checksums.txt")
set(schema_checksums "${RA2YRPROTO_SOURCE_DIR}\n")
foreach(proto IN LISTS PROTO_DEPS)
  file(SHA256 "${proto}" digest)
  string(APPEND schema_checksums "${digest} ${proto}\n")
endforeach()
file(
  GENERATE
  OUTPUT "${SCHEMA_CHECKSUMS_FILE}"
  CONTENT "${schema_checksums}")
list(APPEND PROTO_DEPS "${SCHEMA_CHECKSUMS_FILE}")

if(NOT protobuf_SOURCE_DIR)
  set(protobuf_SOURCE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/protobuf")
endif()
set(PROTOBUF_SOURCES "${protobuf_SOURCE_DIR}/src")

add_custom_target(compile-protobuf ALL DEPENDS ${PROTO_SRCS} ${PROTO_HDRS})
add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
  WORKING_DIRECTORY "${RA2YRPROTO_SOURCE_DIR}"
  COMMAND "${PROTOC_PATH}" -I=. "-I${PROTOBUF_SOURCES}" --pyi_out=${BDIR}
          --python_out=${BDIR} --cpp_out=${BDIR} ${PROTO_FILES}
  DEPENDS ${PROTO_DEPS}
  VERBATIM)

add_library(protocol_o OBJECT helpers.cpp protocol.cpp ${PROTO_SRCS}
                              ../util_string.cpp ../errors.cpp)
add_library(protocol INTERFACE protocol.cpp)
if(NOT protobuf_PROTOC_EXE)
  add_dependencies(protocol_o compile-protobuf)
endif()
target_link_libraries(protocol INTERFACE protocol_o)

# TODO: should make generated headers as install targets
