#include "platform/fonts.h"
#include "platform/filesystem.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace nolf::platform {
namespace {
constexpr std::size_t byte_budget=64*1024*1024;
void check(FT_Error error,const char* message) { if (error) throw std::runtime_error(message); }
struct Budget { std::atomic<std::size_t> used{0}; };
struct FontBytes {
    std::shared_ptr<Budget> budget;
    std::size_t charge=0;
    std::vector<std::byte> data;
    ~FontBytes() { if (budget) budget->used.fetch_sub(charge,std::memory_order_relaxed); }
};
struct Library {
    FT_Library value=nullptr;
    std::mutex mutex;
    std::shared_ptr<Budget> budget=std::make_shared<Budget>();
    Library() { check(FT_Init_FreeType(&value),"cannot initialize native font rasterizer"); }
    ~Library() { FT_Done_FreeType(value); }
};
class FreeTypeFace final : public FontFace {
    std::shared_ptr<Library> library_;
    std::shared_ptr<const FontBytes> bytes_;
    struct Face {
        Library& library;
        FT_Face value=nullptr;
        ~Face() { if (value) { std::lock_guard lock(library.mutex); FT_Done_Face(value); } }
    } face_{*library_};
    FontMetrics metrics_;
    double x_scale_=1;
    std::string family_;
    const std::thread::id owner_=std::this_thread::get_id();
    void owner() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("font face used from another thread"); }
public:
    FreeTypeFace(std::shared_ptr<Library> library,std::shared_ptr<const FontBytes> bytes,const FontLocation& location,const FontRequest& request)
        : library_(std::move(library)),bytes_(std::move(bytes)),family_(location.family) {
        std::lock_guard lock(library_->mutex);
        check(FT_New_Memory_Face(library_->value,reinterpret_cast<const FT_Byte*>(bytes_->data.data()),static_cast<FT_Long>(bytes_->data.size()),location.index,&face_.value),"cannot load native font face");
        check(FT_Select_Charmap(face_.value,FT_ENCODING_UNICODE),"native font has no Unicode character map");
        const int height=std::abs(request.height);
        FT_Size_RequestRec size{}; size.type=request.height>0 ? FT_SIZE_REQUEST_TYPE_REAL_DIM : FT_SIZE_REQUEST_TYPE_NOMINAL; size.height=height*64;
        check(FT_Request_Size(face_.value,&size),"cannot size native font face");
        const auto* face=face_.value;
        const double full=double(face->ascender)-face->descender;
        metrics_.height=height;
        if (request.height<0) metrics_.height=std::max(1,int(std::lround(full*face->size->metrics.y_scale/65536/64)));
        metrics_.ascent=std::clamp(full>0 ? int(std::lround(metrics_.height*double(face->ascender)/full)) : metrics_.height,0,metrics_.height);
        metrics_.descent=metrics_.height-metrics_.ascent;
        metrics_.underline_y=std::min(metrics_.height-1,metrics_.ascent+std::max(1,metrics_.descent/2));
        metrics_.underline_thickness=std::max(1,metrics_.height/16);
        if (request.width) {
            std::int64_t sum=0; unsigned count=0;
            for (unsigned code='A';code<='z';++code) if (code<='Z' || code>='a') {
                if (!FT_Load_Char(face_.value,code,FT_LOAD_DEFAULT|FT_LOAD_TARGET_MONO)) {
                    const auto advance=face_.value->glyph->advance.x; if (advance<0 || advance>4096*64) throw std::length_error("native font advance exceeds glyph budget");
                    sum+=advance; ++count;
                }
            }
            if (sum>0) {
                const double scale=double(request.width)*count*64/sum*65536;
                if (!std::isfinite(scale) || scale>std::numeric_limits<std::int32_t>::max()) throw std::invalid_argument("native font width exceeds rasterizer limits");
                FT_Matrix transform{static_cast<FT_Fixed>(std::lround(scale)),0,0,65536}; FT_Set_Transform(face_.value,&transform,nullptr);
                x_scale_=double(transform.xx)/65536;
            }
        }
    }
    const std::string& family() const override { owner(); return family_; }
    FontMetrics metrics() const override { owner(); return metrics_; }
    FontGlyph glyph(std::uint32_t codepoint,bool rasterize) override {
        owner(); if (codepoint>0x10ffff || (codepoint>=0xd800 && codepoint<=0xdfff)) throw std::invalid_argument("invalid font codepoint");
        std::lock_guard lock(library_->mutex); const auto index=FT_Get_Char_Index(face_.value,codepoint);
        check(FT_Load_Glyph(face_.value,index,FT_LOAD_DEFAULT|FT_LOAD_TARGET_MONO),"cannot load native font glyph");
        if (face_.value->glyph->advance.x < -4096*64 || face_.value->glyph->advance.x>4096*64) throw std::length_error("native glyph advance exceeds layout budget");
        if (!rasterize) { FontGlyph result; result.index=index; result.advance=static_cast<int>((face_.value->glyph->advance.x+32)/64); return result; }
        check(FT_Render_Glyph(face_.value->glyph,FT_RENDER_MODE_MONO),"cannot rasterize native font glyph");
        const auto& glyph=*face_.value->glyph; const auto& bitmap=glyph.bitmap;
        if (bitmap.width>4096 || bitmap.rows>4096 || std::abs(std::int64_t(bitmap.pitch))>1024*1024 || (bitmap.width && bitmap.rows && !bitmap.buffer)) throw std::length_error("native font glyph exceeds bitmap budget");
        if (glyph.advance.x < -4096*64 || glyph.advance.x>4096*64) throw std::length_error("native glyph advance exceeds layout budget");
        FontGlyph result; result.index=index; result.width=static_cast<int>(bitmap.width); result.height=static_cast<int>(bitmap.rows); result.left=glyph.bitmap_left; result.top=glyph.bitmap_top; result.advance=static_cast<int>((glyph.advance.x+32)/64);
        result.coverage.resize(std::size_t(bitmap.width)*bitmap.rows);
        const bool mono=bitmap.pixel_mode==FT_PIXEL_MODE_MONO,gray=bitmap.pixel_mode==FT_PIXEL_MODE_GRAY;
        const auto minimum_pitch=mono ? (bitmap.width+7)/8 : bitmap.width;
        if ((!mono && !gray && !result.coverage.empty()) || (bitmap.rows && std::abs(std::int64_t(bitmap.pitch))<minimum_pitch) || (gray && bitmap.num_grays<2)) throw std::runtime_error("unsupported native glyph bitmap format");
        for (unsigned y=0;y<bitmap.rows;++y) for (unsigned x=0;x<bitmap.width;++x) {
            const auto* row=bitmap.buffer+std::ptrdiff_t(y)*bitmap.pitch;
            result.coverage[std::size_t(y)*bitmap.width+x]=mono ? ((row[x/8]&(0x80>>(x%8))) ? 255 : 0) : static_cast<std::uint8_t>(unsigned(row[x])*255/(bitmap.num_grays-1));
        }
        return result;
    }
    int kerning(std::uint32_t left,std::uint32_t right) override {
        owner(); std::lock_guard lock(library_->mutex); FT_Vector vector{};
        if (left>=static_cast<std::uint64_t>(face_.value->num_glyphs) || right>=static_cast<std::uint64_t>(face_.value->num_glyphs)) throw std::invalid_argument("invalid native font glyph index");
        check(FT_Get_Kerning(face_.value,left,right,FT_KERNING_DEFAULT,&vector),"cannot measure native font kerning"); return static_cast<int>(std::lround(double(vector.x)*x_scale_/64));
    }
};
class FreeTypeFonts final : public FontProvider {
    using Key=std::tuple<std::string,int,int,bool,bool>;
    std::shared_ptr<Library> library_=std::make_shared<Library>();
    std::shared_ptr<FontLocator> locator_;
    std::map<std::filesystem::path,std::weak_ptr<const FontBytes>> files_;
    std::map<Key,std::weak_ptr<FontFace>> faces_;
    const std::thread::id owner_=std::this_thread::get_id();
public:
    explicit FreeTypeFonts(std::shared_ptr<FontLocator> locator) : locator_(std::move(locator)) { if (!locator_) throw std::invalid_argument("font discovery is required"); }
    std::shared_ptr<FontFace> load(const FontRequest& request) override {
        if (owner_!=std::this_thread::get_id()) throw std::logic_error("font provider used from another thread");
        if (request.family.empty() || request.family.size()>256 || request.family.find('\0')!=std::string::npos || request.width<0 || request.width>512 || !request.height || request.height < -512 || request.height>512) throw std::invalid_argument("invalid native font request");
        const Key key{request.family,request.width,request.height,request.italic,request.bold};
        if (const auto found=faces_.find(key);found!=faces_.end()) if (auto face=found->second.lock()) return face;
        const auto location=locator_->find(request); std::shared_ptr<const FontBytes> bytes;
        if (const auto found=files_.find(location.file);found!=files_.end()) bytes=found->second.lock();
        if (!bytes) {
            InputFile file(location.file); const auto size=file.size(); const auto used=library_->budget->used.load(std::memory_order_relaxed);
            if (!size || size>byte_budget || used>byte_budget-size) throw std::length_error("native font files exceed memory budget");
            auto buffer=std::make_shared<FontBytes>(); buffer->budget=library_->budget; buffer->charge=static_cast<std::size_t>(size); buffer->budget->used.fetch_add(buffer->charge,std::memory_order_relaxed);
            buffer->data.resize(buffer->charge); file.read_at(0,buffer->data); bytes=std::move(buffer); files_[location.file]=bytes;
        }
        auto face=std::make_shared<FreeTypeFace>(library_,std::move(bytes),location,request); faces_[key]=face;
        std::erase_if(faces_,[](const auto& item) { return item.second.expired(); }); std::erase_if(files_,[](const auto& item) { return item.second.expired(); }); return face;
    }
};
}
std::shared_ptr<FontProvider> make_freetype_fonts(std::shared_ptr<FontLocator> locator) { return std::make_shared<FreeTypeFonts>(std::move(locator)); }
}
