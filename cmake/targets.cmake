set(INSTALL_LIBS "")

SET(LTIO_LIBRARY_SOURCE_FILES
  $<TARGET_OBJECTS:ltnet_objs>
  $<TARGET_OBJECTS:ltbase_objs>
  $<TARGET_OBJECTS:ltcomponent_objs>
  )

add_library(ltio STATIC ${LTIO_LIBRARY_SOURCE_FILES})
ltio_default_properties(ltio)
target_compile_definitions(ltio
  PUBLIC ${EXPORT_CORO_COMPLILE_DEFINE}
  )
target_link_libraries(ltio
  PUBLIC lt3rd
  PUBLIC fmt::fmt
  PUBLIC ${CORO_LIBRARY}
  PUBLIC ${LtIO_LINKER_LIBS}
  )
target_include_directories(ltio PUBLIC
  ${LtIO_INCLUDE_DIRS}
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/thirdparty
  ${PROJECT_SOURCE_DIR}/integration
  )

list(APPEND INSTALL_LIBS ltio)

if (LTIO_BUILD_SHARED_LIBS)
  add_library(ltio_shared SHARED ${LTIO_LIBRARY_SOURCE_FILES})
  ltio_default_properties(ltio_shared)
  target_compile_definitions(ltio_shared
    PUBLIC ${EXPORT_CORO_COMPLILE_DEFINE}
    )
  target_link_libraries(ltio_shared
    PUBLIC lt3rd
    PUBLIC fmt::fmt
    PUBLIC ${CORO_LIBRARY}
    PUBLIC ${LtIO_LINKER_LIBS}
    )
  target_include_directories(ltio_shared PUBLIC
    ${LtIO_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/thirdparty
    ${PROJECT_SOURCE_DIR}/integration
    )
  set_target_properties(ltio PROPERTIES CLEAN_DIRECT_OUTPUT 1)
  set_target_properties(ltio_shared PROPERTIES OUTPUT_NAME "ltio")
  set_target_properties(ltio_shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)

  list(APPEND INSTALL_LIBS ltio_shared)
endif(LTIO_BUILD_SHARED_LIBS)

include(GNUInstallDirs)
install(TARGETS ${INSTALL_LIBS}
  EXPORT ltio-export
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
