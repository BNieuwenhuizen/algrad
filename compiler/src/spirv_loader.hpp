#ifndef ALGRAD_COMPILER_SPIRV_LOADER_HPP
#define ALGRAD_COMPILER_SPIRV_LOADER_HPP

#include <memory>
#include <string>

namespace algrad {
namespace compiler {
class Program;

std::unique_ptr<Program> loadSPIRV(std::uint32_t const* b, std::uint32_t const* e, std::string const& entryName);
}
}

#endif
