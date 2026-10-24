#include "engine/client/gl_programs.h"
#include "rdev.h"
#include <array>
#include <cstdio>

namespace nolf {
GlProgram::GlProgram(std::string name,std::string vertex,std::string fragment)
    : name_(std::move(name)),vertex_(std::move(vertex)),fragment_(std::move(fragment)) {}
GlProgram::~GlProgram() { if (program_ && generation_==rd_DeviceGeneration()) rd_DeleteProgram(program_); }
bool GlProgram::ready() {
    const auto generation=rd_DeviceGeneration();
    if (!generation || !g_RDCaps.bShaders) return false;
    if (generation!=generation_) { program_=0; failed_=false; uniforms_.clear(); generation_=generation; }
    if (program_) return true;
    if (failed_) return false;
    std::array<char,2048> log{};
    program_=rd_CreateProgram(vertex_.c_str(),fragment_.c_str(),log.data(),int(log.size()));
    if (!program_) { failed_=true; std::fprintf(stderr,"[render] %s program unavailable: %s\n",name_.c_str(),log.data()); }
    return program_!=0;
}
bool GlProgram::use() { if (!ready()) return false; rd_UseProgram(program_); return true; }
int GlProgram::uniform(const char* name) {
    if (!ready()) return -1;
    const auto found=uniforms_.find(name); if (found!=uniforms_.end()) return found->second;
    const auto location=rd_Uniform(program_,name); uniforms_.emplace(name,location); return location;
}
FixedPipeline::~FixedPipeline() { if (g_RDCaps.bShaders && rd_DeviceGeneration()) rd_UseProgram(0); }
}
