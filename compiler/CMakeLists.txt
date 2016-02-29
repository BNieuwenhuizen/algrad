


add_library(algrad-compiler STATIC src/types.cpp
                                   src/ir.cpp
                                   src/spirv_loader.cpp src/promote_variables.cpp src/split_composites.cpp src/dead_code_elimination.cpp src/lower_io.cpp src/lir.hpp src/lir.cpp src/instruction_selection.cpp src/register_allocation.cpp)

set_property(TARGET algrad-compiler PROPERTY CXX_STANDARD 14)
set_property(TARGET algrad-compiler PROPERTY CXX_STANDARD_REQUIRED ON)


add_executable(algrad-cc src/standalone.cpp)
target_link_libraries(algrad-cc algrad-compiler)

set_property(TARGET algrad-cc PROPERTY CXX_STANDARD 14)
set_property(TARGET algrad-cc PROPERTY CXX_STANDARD_REQUIRED ON)
