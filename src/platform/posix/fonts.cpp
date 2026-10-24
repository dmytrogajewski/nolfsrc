#include "platform/fonts.h"
#include <fontconfig/fontconfig.h>
#include <stdexcept>
#include <thread>

namespace nolf::platform {
namespace {
class FontconfigLocator final : public FontLocator {
    struct ConfigDelete { void operator()(FcConfig* config) const { FcConfigDestroy(config); } };
    struct PatternDelete { void operator()(FcPattern* pattern) const { FcPatternDestroy(pattern); } };
    std::unique_ptr<FcConfig,ConfigDelete> config_{FcInitLoadConfigAndFonts()};
    const std::thread::id owner_=std::this_thread::get_id();
public:
    FontconfigLocator() { if (!config_) throw std::runtime_error("cannot load native font configuration"); }
    FontLocation find(const FontRequest& request) override {
        if (owner_!=std::this_thread::get_id()) throw std::logic_error("font discovery used from another thread");
        if (request.family.empty() || request.family.size()>256 || request.family.find('\0')!=std::string::npos) throw std::invalid_argument("invalid font family");
        std::unique_ptr<FcPattern,PatternDelete> pattern(FcPatternCreate()); if (!pattern) throw std::bad_alloc();
        if (!FcPatternAddString(pattern.get(),FC_FAMILY,reinterpret_cast<const FcChar8*>(request.family.c_str())) ||
            !FcPatternAddInteger(pattern.get(),FC_WEIGHT,request.bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR) ||
            !FcPatternAddInteger(pattern.get(),FC_SLANT,request.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN) ||
            !FcPatternAddBool(pattern.get(),FC_SCALABLE,FcTrue)) throw std::bad_alloc();
        if (!FcConfigSubstitute(config_.get(),pattern.get(),FcMatchPattern)) throw std::runtime_error("font configuration substitution failed");
        FcDefaultSubstitute(pattern.get()); FcResult result;
        std::unique_ptr<FcPattern,PatternDelete> match(FcFontMatch(config_.get(),pattern.get(),&result));
        FcChar8 *filename=nullptr,*family=nullptr; int index=0;
        if (!match || result!=FcResultMatch || FcPatternGetString(match.get(),FC_FILE,0,&filename)!=FcResultMatch || !filename) throw std::runtime_error("no usable native font found");
        FcPatternGetString(match.get(),FC_FAMILY,0,&family); FcPatternGetInteger(match.get(),FC_INDEX,0,&index);
        if (index<0) throw std::runtime_error("invalid native font face index");
        return {std::filesystem::path(reinterpret_cast<const char*>(filename)),family ? reinterpret_cast<const char*>(family) : request.family,index};
    }
};
}
std::shared_ptr<FontLocator> system_font_locator() { return std::make_shared<FontconfigLocator>(); }
}
