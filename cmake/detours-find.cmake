find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" REQUIRED)
find_library(DETOURS_LIBRARY detours REQUIRED)

add_library(detours_target STATIC IMPORTED)
set_target_properties(detours_target PROPERTIES
    IMPORTED_LOCATION "${DETOURS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${DETOURS_INCLUDE_DIRS}"
)
