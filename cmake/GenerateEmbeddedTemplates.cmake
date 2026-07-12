if(NOT DEFINED INPUT_DIR)
  message(FATAL_ERROR "INPUT_DIR is required")
endif()

if(NOT DEFINED OUTPUT_CPP)
  message(FATAL_ERROR "OUTPUT_CPP is required")
endif()

find_program(XXD_EXECUTABLE xxd)
if(NOT XXD_EXECUTABLE)
  message(FATAL_ERROR "xxd executable not found; required for embedding templates")
endif()

if(NOT IS_DIRECTORY "${INPUT_DIR}")
  message(FATAL_ERROR "INPUT_DIR does not exist: ${INPUT_DIR}")
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT_CPP}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

file(GLOB_RECURSE TEMPLATE_FILES RELATIVE "${INPUT_DIR}" "${INPUT_DIR}/*.md")
list(SORT TEMPLATE_FILES)

set(CPP "")
string(APPEND CPP "#include \"kernel/admin/TemplateResources.h\"\n\n")
string(APPEND CPP "#include <string_view>\n\n")
string(APPEND CPP "namespace animus::kernel {\n")
string(APPEND CPP "namespace {\n\n")

set(CASES "")
set(EMBED_COUNT 0)

foreach(REL_PATH IN LISTS TEMPLATE_FILES)
  set(ABS_PATH "${INPUT_DIR}/${REL_PATH}")
  if(IS_DIRECTORY "${ABS_PATH}")
    continue()
  endif()

  math(EXPR EMBED_COUNT "${EMBED_COUNT} + 1")
  string(MD5 IDENT "${REL_PATH}")
  set(VAR_NAME "kTemplate_${IDENT}")

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
  string(APPEND CASES "        out->data = ${VAR_NAME};\n")
  string(APPEND CASES "        out->size = static_cast<std::size_t>(${VAR_NAME}_len);\n")
  string(APPEND CASES "        return true;\n")
  string(APPEND CASES "    }\n")
endforeach()

string(APPEND CPP "} // namespace\n\n")
if(EMBED_COUNT GREATER 0)
  string(APPEND CPP "bool HasEmbeddedTemplates() {\n")
  string(APPEND CPP "    return true;\n")
  string(APPEND CPP "}\n\n")
  string(APPEND CPP "bool GetEmbeddedTemplate(std::string_view path, EmbeddedTemplate* out) {\n")
  string(APPEND CPP "    if (!out) {\n")
  string(APPEND CPP "        return false;\n")
  string(APPEND CPP "    }\n")
  string(APPEND CPP "    out->data = nullptr;\n")
  string(APPEND CPP "    out->size = 0;\n")
  string(APPEND CPP "${CASES}")
  string(APPEND CPP "    return false;\n")
  string(APPEND CPP "}\n")
else()
  string(APPEND CPP "bool HasEmbeddedTemplates() {\n")
  string(APPEND CPP "    return false;\n")
  string(APPEND CPP "}\n\n")
  string(APPEND CPP "bool GetEmbeddedTemplate(std::string_view, EmbeddedTemplate*) {\n")
  string(APPEND CPP "    return false;\n")
  string(APPEND CPP "}\n")
endif()

string(APPEND CPP "\n} // namespace animus::kernel\n")

file(WRITE "${OUTPUT_CPP}" "${CPP}")
message(STATUS "Generated embedded templates source: ${OUTPUT_CPP} (${EMBED_COUNT} files)")
