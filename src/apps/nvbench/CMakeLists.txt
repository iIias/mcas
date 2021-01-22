cmake_minimum_required (VERSION 3.5.1 FATAL_ERROR)

include(../../../mk/common.cmake)

project(nvbench CXX)

add_compile_options(-g -Wall -Werror -Wextra -Wcast-align -Wcast-qual -Wconversion -Weffc++ -Wold-style-cast -Wredundant-decls -Wshadow -Wtype-limits -Wunused-parameter -Wwrite-strings)
add_compile_options("$<$<CONFIG:Debug>:-O0>")

include_directories(${CMAKE_SOURCE_DIR}/src/lib/common/include)
link_directories(${CMAKE_BINARY_DIR}/src/lib/common)

# begin PMEM
find_path(PMEM_INCLUDE_DIR libpmem.h NO_DEFAULT_PATH PATHS
  ${HT_DEPENDENCY_INCLUDE_DIR}
  /opt/local/include
  /usr/local/include
  /usr/include
  )

set(PMEM_NAMES ${PMEM_NAMES} pmem pmemobj)
find_library(PMEM_LIBRARY NAMES ${PMEM_NAMES} NO_DEFAULT_PATH PATHS
  ${HT_DEPENDENCY_LIB_DIR}
  /usr/local/lib
  /usr/local/lib64
  /opt/local/lib
  /opt/local/lib64
  /usr/lib
  /usr/lib64
  )

if (PMEM_INCLUDE_DIR AND PMEM_LIBRARY)
  set(PMEM_FOUND TRUE)
  set(PMEM_LIBRARIES ${PMEM_LIBRARY} )
else ()
  set(PMEM_FOUND FALSE)
  set(PMEM_LIBRARIES )
endif ()

if (PMEM_FOUND)
  message(STATUS "Found pmem: ${PMEM_LIBRARY}")
else ()
  message(STATUS "Not Found pmem: ${PMEM_LIBRARY}")
  if (PMEM_FIND_REQUIRED)
    message(STATUS "Looked for pmem libraries named ${PMEM_NAMES}.")
    MESSAGE(SEND_ERROR "Could NOT find pmem library")
  endif ()
endif ()
# end PMEM

# begin PAPI
find_path(PAPI_INCLUDE_DIR papi.h NO_DEFAULT_PATH PATHS
  /opt/local/include
  /usr/local/include
  /usr/include
  )

find_library(PAPI_LIBRARY NAMES papi NO_DEFAULT_PATH PATHS
  ${HT_DEPENDENCY_LIB_DIR}
  /usr/local/lib
  /usr/local/lib64
  /opt/local/lib
  /opt/local/lib64
  /usr/lib
  /usr/lib64
  )

if (PAPI_INCLUDE_DIR AND PAPI_LIBRARY)
  set(PAPI_FOUND TRUE)
  set( PAPI_LIBRARIES ${PAPI_LIBRARY} )
else ()
  set(PAPI_FOUND FALSE)
  set( PAPI_LIBRARIES )
endif ()

if (PAPI_FOUND)
  message(STATUS "Found papi: ${PAPI_LIBRARY}")
else ()
  message(STATUS "Not Found papi: ${PAPI_LIBRARY}")
  if (PAPI_FIND_REQUIRED)
    message(STATUS "Looked for api libraries named ${PAPI_NAMES}.")
    message(SEND_ERROR "Could NOT find papi library")
  endif ()
endif ()
# end PAPI

add_definitions(${GCC_COVERAGE_COMPILE_FLAGS} -DCONFIG_DEBUG)

add_executable(nvbench src/cpu_mask_iterator.cpp src/cpu_measured.cpp src/external_count.cpp src/geometry.cpp src/load_generator.cpp src/memcpy_init.cpp src/mem_run.cpp src/mem_test.cpp src/ops.cpp src/pattern.cpp src/persist.cpp src/reporter_papi.cpp src/reporter_rusage.cpp src/rw_pos.cpp src/rw_prepared.cpp src/write_cycle.cpp src/nvbench.cpp)
add_executable(chronobench src/chronobench.cpp)

target_link_libraries(nvbench common numa boost_program_options boost_iostreams boost_system pthread dl pthread)
if (PMEM_FOUND)
  target_link_libraries(nvbench pmem pmemobj)
  # target_link_libraries(nvbench pmem pmemobj benchmark)
endif()
if (PAPI_FOUND)
  target_link_libraries(nvbench papi)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
