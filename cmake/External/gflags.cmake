if (NOT __GFLAGS_INCLUDED) # guard against multiple includes
  set(__GFLAGS_INCLUDED TRUE)

  # use the system-wide gflags if present
  find_package(GFlags)
  if (GFLAGS_FOUND)
    set(GFLAGS_EXTERNAL FALSE)
  else()
    # gflags will use pthreads if it's available in the system, so we must link with it
    find_package(Threads)

    # build directory
    set(gflags_PREFIX ${CMAKE_BINARY_DIR}/external/gflags-prefix)
    # install directory
    set(gflags_INSTALL ${CMAKE_BINARY_DIR}/external/gflags-install)

    # we build gflags statically, but want to link it into the caffe shared library
    # this requires position-independent code
    if (UNIX)
        set(GFLAGS_EXTRA_COMPILER_FLAGS "-fPIC")
    endif()

    set(GFLAGS_CXX_FLAGS ${CMAKE_CXX_FLAGS} ${GFLAGS_EXTRA_COMPILER_FLAGS})
    set(GFLAGS_C_FLAGS ${CMAKE_C_FLAGS} ${GFLAGS_EXTRA_COMPILER_FLAGS})

    ExternalProject_Add(gflags
      PREFIX ${gflags_PREFIX}
      GIT_REPOSITORY "https://gitee.com/ltecho/gflags.git"
      GIT_TAG "v2.2.2"
      UPDATE_COMMAND ""
      INSTALL_DIR ${gflags_INSTALL}
      CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                 -DCMAKE_INSTALL_PREFIX=${gflags_INSTALL}
                 -DBUILD_SHARED_LIBS=ON
                 -DBUILD_gflags_LIB=ON
                 -DBUILD_STATIC_LIBS=ON
                 -DBUILD_PACKAGING=OFF
                 -DBUILD_TESTING=OFF
                 #-DBUILD_NC_TESTS=OFF
                 #-DBUILD_CONFIG_TESTS=OFF
                 -DINSTALL_HEADERS=ON
                 #-DCMAKE_C_FLAGS=${GFLAGS_C_FLAGS}
                 -DCMAKE_CXX_FLAGS=${GFLAGS_CXX_FLAGS}
      LOG_DOWNLOAD 1
      LOG_INSTALL 1
      )

    set(GFLAGS_FOUND TRUE)
    set(GFLAGS_INCLUDE_DIRS ${gflags_INSTALL}/include)
    set(GFLAGS_LIBRARY ${gflags_INSTALL}/lib/libgflags.so)
    set(GFLAGS_LIBRARIES ${gflags_INSTALL}/lib/libgflags.so ${CMAKE_THREAD_LIBS_INIT})
    set(GFLAGS_LIBRARY_DIRS ${gflags_INSTALL}/lib)
    set(GFLAGS_EXTERNAL TRUE)

    list(APPEND external_project_dependencies gflags)
  endif()

endif()


if(GFLAGS_FOUND AND NOT TARGET gflags::gflags)
  add_library(gflags::gflags SHARED IMPORTED GLOBAL)
  set_target_properties(gflags::gflags PROPERTIES IMPORTED_LOCATION ${GFLAGS_LIBRARY})
  #set_property(TARGET gflags::gflags PROPERTY INTERFACE_LINK_LIBRARIES ${GFLAGS_LIBRARIES})
  set_property(TARGET gflags::gflags PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${GFLAGS_INCLUDE_DIRS})
endif()
