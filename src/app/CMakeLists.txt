add_library (interpolation STATIC)
target_sources (
       interpolation
       PUBLIC [[interpolation.hpp]]
       PRIVATE [[interpolation.cpp]]
)
target_link_libraries (interpolation PRIVATE CG_Labs_options glm)

add_library (parametric_shapes STATIC)
target_sources (
       parametric_shapes
       PUBLIC [[parametric_shapes.hpp]]
       PRIVATE [[parametric_shapes.cpp]]
)
target_link_libraries (parametric_shapes PRIVATE bonobo CG_Labs_options)

add_executable (NPRR)

target_sources (
	NPRR
	PRIVATE
		[[nprr.hpp]]
		[[nprr.cpp]]
)

target_link_libraries (NPRR PRIVATE assignment_setup parametric_shapes)

install (TARGETS NPRR DESTINATION bin)

copy_dlls (NPRR "${CMAKE_CURRENT_BINARY_DIR}")
