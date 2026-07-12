if(NOT DEFINED INPUT_DIR)
  message(FATAL_ERROR "INPUT_DIR is required")
endif()

if(NOT DEFINED OUTPUT_CPP)
  message(FATAL_ERROR "OUTPUT_CPP is required")
endif()

find_program(XXD_EXECUTABLE xxd)
if(NOT XXD_EXECUTABLE)
  message(FATAL_ERROR "xxd executable not found; required for embedding admin UI assets")
endif()

if(NOT IS_DIRECTORY "${INPUT_DIR}")
  message(FATAL_ERROR "INPUT_DIR does not exist: ${INPUT_DIR}")
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT_CPP}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

file(GLOB_RECURSE UI_FILES RELATIVE "${INPUT_DIR}" "${INPUT_DIR}/*")
list(SORT UI_FILES)

set(CPP "")
string(APPEND CPP "#include \"kernel/admin/AdminUiResources.h\"\n\n")
string(APPEND CPP "#include <string_view>\n\n")
string(APPEND CPP "namespace animus::kernel {\n")
string(APPEND CPP "namespace {\n\n")

set(CASES "")
set(EMBED_COUNT 0)

foreach(REL_PATH IN LISTS UI_FILES)
  set(ABS_PATH "${INPUT_DIR}/${REL_PATH}")
  if(IS_DIRECTORY "${ABS_PATH}")
    continue()
  endif()

  math(EXPR EMBED_COUNT "${EMBED_COUNT} + 1")
  string(MD5 IDENT "${REL_PATH}")
  set(VAR_NAME "kAsset_${IDENT}")

  execute_process(
    COMMAND "${XXD_EXECUTABLE}" -i -n "${VAR_NAME}" "${ABS_PATH}"
    RESULT_VARIABLE XXD_RESULT
    OUTPUT_VARIABLE XXD_OUTPUT
    ERROR_VARIABLE XXD_ERROR
  )
  if(NOT XXD_RESULT EQUAL 0)
    message(FATAL_ERROR "xxd failed for ${REL_PATH}: ${XXD_ERROR}")
  endif()

  string(APPEND CPP "${XXD_OUTPUT}\n")

  string(REPLACE "\\" "\\\\" ESCAPED_PATH "${REL_PATH}")
  string(REPLACE "\"" "\\\"" ESCAPED_PATH "${ESCAPED_PATH}")
  string(APPEND CASES "    if (path == \"${ESCAPED_PATH}\") {\n")
  string(APPEND CASES "        asset->data = ${VAR_NAME};\n")
  string(APPEND CASES "        asset->size = static_cast<std::size_t>(${VAR_NAME}_len);\n")
  string(APPEND CASES "        return true;\n")
  string(APPEND CASES "    }\n")
endforeach()

string(APPEND CPP "} // namespace\n\n")
if(EMBED_COUNT GREATER 0)
  string(APPEND CPP "bool HasEmbeddedAdminUiAssets() {\n")
  string(APPEND CPP "    return true;\n")
  string(APPEND CPP "}\n\n")
  string(APPEND CPP "bool GetEmbeddedAdminUiAsset(std::string_view path, EmbeddedAdminUiAsset* asset) {\n")
  string(APPEND CPP "    if (!asset) {\n")
  string(APPEND CPP "        return false;\n")
  string(APPEND CPP "    }\n")
  string(APPEND CPP "    asset->data = nullptr;\n")
  string(APPEND CPP "    asset->size = 0;\n")
  string(APPEND CPP "${CASES}")
  string(APPEND CPP "    return false;\n")
  string(APPEND CPP "}\n")
else()
  string(APPEND CPP "bool HasEmbeddedAdminUiAssets() {\n")
  string(APPEND CPP "    return false;\n")
  string(APPEND CPP "}\n\n")
  string(APPEND CPP "bool GetEmbeddedAdminUiAsset(std::string_view, EmbeddedAdminUiAsset*) {\n")
  string(APPEND CPP "    return false;\n")
  string(APPEND CPP "}\n")
endif()

string(APPEND CPP "\n} // namespace animus::kernel\n")

file(WRITE "${OUTPUT_CPP}" "${CPP}")
message(STATUS "Generated embedded admin UI source: ${OUTPUT_CPP} (${EMBED_COUNT} files)")
