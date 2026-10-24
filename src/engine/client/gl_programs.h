#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nolf {
// A GLSL 1.20 program on the render device's programmable extension, built
// for whichever device is current and again after a device restart. One
// that does not compile reports why once and stays unusable on that device.
class GlProgram {
public:
    GlProgram(std::string name,std::string vertex,std::string fragment);
    ~GlProgram();
    GlProgram(const GlProgram&)=delete;
    GlProgram& operator=(const GlProgram&)=delete;
    // Puts the program in use; false when the device has none to give.
    bool use();
    // The uniform's location in the program in use, -1 when it has none.
    int uniform(const char* name);
private:
    bool ready();
    std::string name_,vertex_,fragment_;
    std::uint32_t program_=0;
    std::uint64_t generation_=0;
    bool failed_=false;
    std::unordered_map<std::string,int> uniforms_;
};
// Back to the fixed pipeline at the end of a scope.
struct FixedPipeline { ~FixedPipeline(); };
}
