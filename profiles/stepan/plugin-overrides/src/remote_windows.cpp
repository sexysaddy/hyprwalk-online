#include "remote_windows.hpp"
#ifdef HYPRWALK_MULTIPLAYER
#include <cairo.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <stdexcept>
namespace hyprwalk {
namespace {
class Canvas {
  public:
    cairo_surface_t* surface=nullptr; cairo_t* cr=nullptr; int width,height;
    Canvas(int w,int h):width(w),height(h) {
        surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
        if(cairo_surface_status(surface)!=CAIRO_STATUS_SUCCESS){cairo_surface_destroy(surface);surface=nullptr;throw std::runtime_error("remote placeholder: Cairo surface allocation failed");}
        cr=cairo_create(surface);
        if(cairo_status(cr)!=CAIRO_STATUS_SUCCESS){cairo_destroy(cr);cairo_surface_destroy(surface);cr=nullptr;surface=nullptr;throw std::runtime_error("remote placeholder: Cairo context allocation failed");}
    }
    ~Canvas(){if(cr)cairo_destroy(cr);if(surface)cairo_surface_destroy(surface);}
    Canvas(const Canvas&)=delete;
    void box(float x,float y,float w,float h,float r,float g,float b){cairo_set_source_rgb(cr,r,g,b);cairo_rectangle(cr,x,y,w,h);cairo_fill(cr);}
    void text(std::string_view text,float x,float y,int size,float r,float g,float b){
        PangoLayout* layout=pango_cairo_create_layout(cr); auto* font=pango_font_description_from_string("Sans");
        pango_font_description_set_absolute_size(font,size*PANGO_SCALE);pango_layout_set_font_description(layout,font);
        pango_layout_set_width(layout,(width-static_cast<int>(x)-22)*PANGO_SCALE);pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);pango_layout_set_single_paragraph_mode(layout,true);
        pango_layout_set_text(layout,text.data(),static_cast<int>(text.size()));cairo_set_source_rgb(cr,r,g,b);cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,layout);
        pango_font_description_free(font);g_object_unref(layout);
    }
    std::shared_ptr<SRemoteWindowImage> finish(std::uint64_t serial){
        cairo_surface_flush(surface);if(cairo_status(cr)!=CAIRO_STATUS_SUCCESS||cairo_surface_status(surface)!=CAIRO_STATUS_SUCCESS)throw std::runtime_error("remote placeholder: Cairo drawing failed");
        auto image=std::make_shared<SRemoteWindowImage>();image->width=width;image->height=height;image->serial=serial;image->rgba.resize(static_cast<std::size_t>(width)*height*4);
        const auto* bytes=cairo_image_surface_get_data(surface);const auto stride=cairo_image_surface_get_stride(surface);
        for(int y=0;y<height;++y){const auto* row=reinterpret_cast<const std::uint32_t*>(bytes+y*stride);for(int x=0;x<width;++x){const auto argb=row[x];const auto i=(static_cast<std::size_t>(y)*width+x)*4;image->rgba[i]=static_cast<std::uint8_t>(argb>>16);image->rgba[i+1]=static_cast<std::uint8_t>(argb>>8);image->rgba[i+2]=static_cast<std::uint8_t>(argb);image->rgba[i+3]=static_cast<std::uint8_t>(argb>>24);}}
        return image;
    }
};
std::shared_ptr<SRemoteWindowImage> panelImage(const mp::WindowMetadata& w,std::string_view owner,std::string_view video,std::uint64_t serial){
    Canvas c(512,256);c.box(0,0,512,256,.075f,.095f,.125f);c.box(0,0,512,6,.35f,.66f,.83f);
    c.text("REMOTE WINDOW / VIDEO",24,20,17,.50f,.75f,.88f);c.text(w.title.empty()?"Untitled window":w.title,24,60,26,.95f,.96f,.98f);
    c.text(std::string("Owner: ")+std::string(owner),24,102,18,.70f,.77f,.84f);c.text(w.application.empty()?"Application: unknown":w.application,24,133,16,.65f,.71f,.77f);
    c.box(24,171,464,2,.19f,.24f,.29f);c.text(std::string("PLACEHOLDER / ")+std::string(video),24,187,17,.97f,.78f,.40f);
    return c.finish(serial);
}
}
void CRemoteWindowView::update(const mp::WindowSync& sync,std::string_view owner,std::uint64_t){
    std::erase_if(m_cache,[&](const auto& entry){const auto* w=sync.findRemote(entry.first.window);return !w||w->key!=entry.first||w->receive.hidden;});m_frames.clear();
    for(const auto& w:sync.remote()){
        if(w.receive.hidden)continue;auto& c=m_cache[w.key];
        if(!c.panel||c.title!=w.metadata.title||c.app!=w.metadata.application||c.owner!=owner||c.video!=w.video){c.panel=panelImage(w.metadata,owner,w.video,++m_serial);c.title=w.metadata.title;c.app=w.metadata.application;c.owner=owner;c.video=w.video;}
        SRemoteWindowFrame frame{w.key,w.metadata.geometry,c.panel};
        m_frames.push_back(std::move(frame));
    }
}
} // namespace hyprwalk
#endif
