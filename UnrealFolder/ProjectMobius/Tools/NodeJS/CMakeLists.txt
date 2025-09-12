cmake_minimum_required(VERSION 3.20)
project(MobiusNodeJS NONE)

# ----- Pin Node for pkg runtime (node18) -----
set(NODE_VERSION "18.20.0")

# Where the superbuild stages intermediary artifacts
set(NODE_STAGE_DIR "${CMAKE_BINARY_DIR}/nodejs-build")
set(BUNDLE_DIR     "${CMAKE_BINARY_DIR}/bundle")
file(MAKE_DIRECTORY "${NODE_STAGE_DIR}" "${BUNDLE_DIR}")

# This CMakeLists lives in Tools/NodeJS
set(NODEJS_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ----- Platform specifics -----
if(WIN32)
  set(NODE_OS "win")
  set(NODE_ARCHIVE "${NODE_STAGE_DIR}/node-v${NODE_VERSION}-${NODE_OS}-x64.zip")
  set(NODE_URL     "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-${NODE_OS}-x64.zip")
  set(NODE_BIN_DIR "${NODE_STAGE_DIR}/node-v${NODE_VERSION}-${NODE_OS}-x64")
  set(NODE_EXE     "${NODE_BIN_DIR}/node.exe")
  # npm CLI path inside the portable node tree
  set(NPM_CLI_JS   "${NODE_BIN_DIR}/node_modules/npm/bin/npm-cli.js")
  # local pkg wrapper installed into node_modules/.bin
  set(PKG_BIN      "${NODEJS_SRC_DIR}/node_modules/.bin/pkg.cmd")
  set(OUTPUT_NAME  "MobiusServer.exe")
  set(ARCHIVE_EXTRACT_CMD ${CMAKE_COMMAND} -E tar xvf "${NODE_ARCHIVE}")
elseif(APPLE)
  set(NODE_OS "darwin")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(NODE_CPU "arm64")
  else()
    set(NODE_CPU "x64")
  endif()
  set(NODE_ARCHIVE "${NODE_STAGE_DIR}/node-v${NODE_VERSION}-${NODE_OS}-${NODE_CPU}.tar.xz")
  set(NODE_URL     "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-${NODE_OS}-${NODE_CPU}.tar.xz")
  set(NODE_BIN_DIR "${NODE_STAGE_DIR}/node-v${NODE_VERSION}-${NODE_OS}-${NODE_CPU}")
  set(NODE_EXE     "${NODE_BIN_DIR}/bin/node")
  set(NPM_CLI_JS   "${NODE_BIN_DIR}/lib/node_modules/npm/bin/npm-cli.js")
  set(PKG_BIN      "${NODEJS_SRC_DIR}/node_modules/.bin/pkg")
  set(OUTPUT_NAME  "MobiusServer")
  set(ARCHIVE_EXTRACT_CMD ${CMAKE_COMMAND} -E tar xJf "${NODE_ARCHIVE}")
else()
  message(FATAL_ERROR "Windows & macOS are supported for this build.")
endif()

# ----- Prefer system Node only if BOTH node and npm exist -----
find_program(SYSTEM_NODE node)
find_program(SYSTEM_NPM  npm)

if(SYSTEM_NODE AND SYSTEM_NPM)
  set(USE_SYSTEM_NODE TRUE)
  set(NODE "${SYSTEM_NODE}")
  set(NPM  "${SYSTEM_NPM}")
else()
  set(USE_SYSTEM_NODE FALSE)
  set(NODE "${NODE_EXE}")                 # executable token 1
  set(NPM_NODE "${NODE_EXE}")             # we'll run npm via: node <npm-cli.js> <args...>
  set(NPM_CLI  "${NPM_CLI_JS}")           # tokenized separately
endif()

# ===========================
# Download & extract portable Node (when needed)
# ===========================
add_custom_command(
  OUTPUT "${NODE_ARCHIVE}"
  COMMAND ${CMAKE_COMMAND} -E echo "[mobius-node] download start: ${NODE_URL}"
  COMMAND ${CMAKE_COMMAND} -E rm -f "${NODE_ARCHIVE}"
  COMMAND ${CMAKE_COMMAND} -E download "${NODE_URL}" "${NODE_ARCHIVE}" TLS_VERIFY ON
  COMMAND ${CMAKE_COMMAND} -E echo "[mobius-node] download ok: ${NODE_ARCHIVE}"
  WORKING_DIRECTORY "${NODE_STAGE_DIR}"
  COMMENT "Downloading Node.js v${NODE_VERSION}"
  VERBATIM
)

add_custom_command(
  OUTPUT "${NODE_EXE}"
  COMMAND ${CMAKE_COMMAND} -E echo "[mobius-node] extract start: ${NODE_ARCHIVE}"
  COMMAND ${ARCHIVE_EXTRACT_CMD}
  COMMAND ${CMAKE_COMMAND} -E echo "[mobius-node] extract ok: ${NODE_BIN_DIR}"
  WORKING_DIRECTORY "${NODE_STAGE_DIR}"
  DEPENDS "${NODE_ARCHIVE}"
  COMMENT "Extracting Node.js v${NODE_VERSION}"
  VERBATIM
)

add_custom_target(node_runtime ALL
  COMMENT "Ensuring Node.js runtime available"
)
if(NOT USE_SYSTEM_NODE)
  add_dependencies(node_runtime "${NODE_EXE}")
endif()

# ===========================
# npm install (ci → fallback), tokenized correctly
# ===========================
# Marker: creation of node_modules/.bin indicates install done
set(NPM_BIN_MARKER "${NODEJS_SRC_DIR}/node_modules/.bin")

if(USE_SYSTEM_NODE)
  # system npm is a real exe; pass subcommands as separate args
  set(NPM_CI_CMD   "${NPM}" "ci")
  set(NPM_INST_CMD "${NPM}" "install" "--no-audit" "--no-fund")
else()
  # drive npm via node + npm-cli.js; each token is separate
  set(NPM_CI_CMD   "${NPM_NODE}" "${NPM_CLI}" "ci")
  set(NPM_INST_CMD "${NPM_NODE}" "${NPM_CLI}" "install" "--no-audit" "--no-fund")
endif()

add_custom_command(
  OUTPUT "${NPM_BIN_MARKER}"
  COMMAND ${CMAKE_COMMAND} -E echo "[mobius-node] npm ci (lockfile install)"
  # try ci
  COMMAND ${NPM_CI_CMD}
  # if ci fails, fallback to install (Windows: use cmd for '||' chaining)
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/_npm_fallback.cmake"
  WORKING_DIRECTORY "${NODEJS_SRC_DIR}"
  DEPENDS node_runtime
  COMMENT "Installing Node dependencies"
  VERBATIM
)

# tiny script to do the fallback in a cross-platform way
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/_npm_fallback.cmake" "
execute_process(COMMAND ${NPM_CI_CMD}
                WORKING_DIRECTORY \"${NODEJS_SRC_DIR}\"
                RESULT_VARIABLE _r1
                OUTPUT_QUIET ERROR_QUIET)
if(NOT _r1 EQUAL 0)
  message(STATUS \"[mobius-node] npm ci failed; falling back to npm install...\")
  execute_process(COMMAND ${NPM_INST_CMD}
                  WORKING_DIRECTORY \"${NODEJS_SRC_DIR}\"
                  RESULT_VARIABLE _r2)
  if(NOT _r2 EQUAL 0)
    message(FATAL_ERROR \"[mobius-node] npm install failed\")
  endif()
endif()
")

add_custom_target(mobius_node_deps ALL
  DEPENDS "${NPM_BIN_MARKER}"
)

# ===========================
# Package with pkg (node18 targets)
# ===========================
file(MAKE_DIRECTORY "${BUNDLE_DIR}")

# On Windows, execute pkg.cmd via cmd /C; on macOS call pkg directly
if(WIN32)
  set(PKG_CMD "cmd" "/C" "${PKG_BIN}")
else()
  set(PKG_CMD "${PKG_BIN}")
endif()

add_custom_command(
  OUTPUT "${BUNDLE_DIR}/${OUTPUT_NAME}"
  COMMAND ${CMAKE_COMMAND} -E echo "[mobius-node] packaging ${OUTPUT_NAME} (targets: node18-${NODE_OS}-x64)"
  COMMAND ${PKG_CMD} "${NODEJS_SRC_DIR}/MobiusServer.js"
          --targets "node18-${NODE_OS}-x64"
          --output "${BUNDLE_DIR}/${OUTPUT_NAME}"
  COMMAND ${CMAKE_COMMAND} -E echo "[mobius-node] package ok: ${BUNDLE_DIR}/${OUTPUT_NAME}"
  WORKING_DIRECTORY "${NODEJS_SRC_DIR}"
  DEPENDS mobius_node_deps "${NODEJS_SRC_DIR}/MobiusServer.js" "${NODEJS_SRC_DIR}/package.json"
  COMMENT "Building MobiusServer native binary with pkg"
  VERBATIM
)

add_custom_target(mobius_node_bundle ALL
  DEPENDS "${BUNDLE_DIR}/${OUTPUT_NAME}"
)

# (Optional) make `cmake --install` succeed if you still want it
install(PROGRAMS "${BUNDLE_DIR}/${OUTPUT_NAME}" DESTINATION bin)
