set(FLOWER_CORE_SOURCES
    BuildIndex.cpp
    ExclusiveLock.cpp
    Index.cpp
    IndexFile.cpp
    IndexNode.cpp
    KVContent.cpp
    MemoryPool.cpp
    Myfile.cpp
    ReadWriteLock.cpp
    ResultMapWithLock.cpp
    SearchContext.cpp
    SearchIndex.cpp
    SetWithLock.cpp
    UniqueGenerator.cpp
    common.cpp
    interface.cpp
)

set(FLOWER_PUBLIC_HEADERS
    BuildIndex.h
    ExclusiveLock.h
    Index.h
    IndexFile.h
    IndexNode.h
    KVContent.h
    MemoryPool.h
    Myfile.h
    ReadWriteLock.h
    ResultMapWithLock.h
    SearchContext.h
    SearchIndex.h
    SetWithLock.h
    UniqueGenerator.h
    common.h
    interface.h
)

find_package(Threads REQUIRED)

add_library(flower_lib STATIC
    ${FLOWER_CORE_SOURCES}
    ${FLOWER_PUBLIC_HEADERS}
)

target_include_directories(flower_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(flower_lib PUBLIC cxx_std_17)

target_link_libraries(flower_lib
    PUBLIC
        Threads::Threads
)

if(FLOWER_USE_TCMALLOC)
    find_library(FLOWER_TCMALLOC_LIBRARY
        NAMES tcmalloc_minimal tcmalloc
    )

    if(FLOWER_TCMALLOC_LIBRARY)
        target_link_libraries(flower_lib PUBLIC ${FLOWER_TCMALLOC_LIBRARY})
        message(STATUS "Using tcmalloc: ${FLOWER_TCMALLOC_LIBRARY}")
    else()
        message(WARNING "FLOWER_USE_TCMALLOC is ON, but no tcmalloc library was found. Building without tcmalloc.")
    endif()
endif()

add_executable(flower main.cpp)

target_link_libraries(flower PRIVATE flower_lib)
