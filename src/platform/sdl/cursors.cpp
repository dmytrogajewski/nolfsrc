#include "platform/cursors.h"
#include <SDL3/SDL.h>
#include <string>
#include <thread>

namespace nolf::platform {
namespace {
void check(bool value,const char* operation) { if (!value) throw std::runtime_error(std::string(operation)+": "+SDL_GetError()); }
struct Video {
    const std::thread::id owner=std::this_thread::get_id();
    Video() { check(SDL_InitSubSystem(SDL_INIT_VIDEO),"initialize native cursor video lease"); }
    ~Video() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("cursor used from another thread"); }
};
struct CursorDelete { void operator()(SDL_Cursor* cursor) const { SDL_DestroyCursor(cursor); } };
class SdlShape final : public CursorShape {
public:
    std::shared_ptr<Video> video;
    std::unique_ptr<SDL_Cursor,CursorDelete> cursor;
    SdlShape(std::shared_ptr<Video> lease,std::unique_ptr<SDL_Cursor,CursorDelete> value) : video(std::move(lease)),cursor(std::move(value)) {}
};
class SdlCursors final : public CursorProvider {
    std::shared_ptr<Video> video_=std::make_shared<Video>();
    std::shared_ptr<const SdlShape> selected_;
public:
    ~SdlCursors() override { if (selected_) SDL_SetCursor(SDL_GetDefaultCursor()); }
    std::shared_ptr<const CursorShape> create(const CursorBitmap& bitmap) override {
        video_->thread();
        if (!bitmap.width || !bitmap.height || bitmap.width>512 || bitmap.height>512 || bitmap.hot_x>=bitmap.width || bitmap.hot_y>=bitmap.height || bitmap.rgba.size()!=std::size_t(bitmap.width)*bitmap.height) throw std::invalid_argument("invalid native cursor bitmap");
        std::unique_ptr<SDL_Cursor,CursorDelete> cursor;
        if (!bitmap.mono_data.empty()) {
            const auto width=(bitmap.width+7)&~7u;
            const auto bytes=std::size_t(width/8)*bitmap.height;
            if (bitmap.mono_data.size()!=bytes || bitmap.mono_mask.size()!=bytes) throw std::invalid_argument("invalid native mono cursor planes");
            cursor.reset(SDL_CreateCursor(bitmap.mono_data.data(),bitmap.mono_mask.data(),static_cast<int>(width),static_cast<int>(bitmap.height),static_cast<int>(bitmap.hot_x),static_cast<int>(bitmap.hot_y)));
        } else {
            if (bitmap.color_xor) throw UnsupportedCursor("native color cursor requires destination XOR; try another authored group image");
            struct SurfaceDelete { void operator()(SDL_Surface* value) const { SDL_DestroySurface(value); } };
            std::unique_ptr<SDL_Surface,SurfaceDelete> surface(SDL_CreateSurfaceFrom(static_cast<int>(bitmap.width),static_cast<int>(bitmap.height),SDL_PIXELFORMAT_ARGB8888,const_cast<std::uint32_t*>(bitmap.rgba.data()),static_cast<int>(bitmap.width*4)));
            check(bool(surface),"create native cursor surface"); cursor.reset(SDL_CreateColorCursor(surface.get(),static_cast<int>(bitmap.hot_x),static_cast<int>(bitmap.hot_y)));
        }
        check(bool(cursor),"create native cursor"); return std::make_shared<SdlShape>(video_,std::move(cursor));
    }
    void select(std::shared_ptr<const CursorShape> cursor) override {
        video_->thread(); const auto shape=std::dynamic_pointer_cast<const SdlShape>(cursor);
        if (cursor && (!shape || shape->video!=video_)) throw std::invalid_argument("foreign native cursor shape");
        check(SDL_SetCursor(shape ? shape->cursor.get() : SDL_GetDefaultCursor()),"select native cursor"); selected_=shape;
    }
    void visible(bool enabled) override { video_->thread(); check(enabled ? SDL_ShowCursor() : SDL_HideCursor(),"set native cursor visibility"); }
    bool available() const override { video_->thread(); return SDL_GetDefaultCursor()!=nullptr; }
};
}
std::shared_ptr<CursorProvider> make_sdl_cursors() { return std::make_shared<SdlCursors>(); }
}
