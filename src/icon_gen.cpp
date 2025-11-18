/**
 * @file icon_gen.cpp
 * @brief Programmatic pixel-art mouse icon generator.
 *
 * This small standalone tool generates an ICO file containing two sizes
 * (32x32 and 16x16). The generated icon depicts a simple pixel-art mouse
 * silhouette. The generator includes a "falling" effect: the mouse is
 * drawn slightly lower in the image with a soft shadow below, and a small
 * motion blur trail to convey downward movement. The output is suitable
 * for embedding as the application's resource icon.
 *
 * Usage:
 *   icon_gen [output.ico]
 * If `output.ico` is omitted, the default path `res/altrightclick.ico` is used.
 *
 * The implementation writes ICO structures directly (ICONDIR + ICONDIRENTRY)
 * and embeds BITMAPINFOHEADER + BGRA pixel data with an AND mask as required
 * by the Windows ICO format. No external libraries are needed.
 */

#include <cstdint>
#include <vector>
#include <fstream>
#include <array>
#include <cmath>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
using namespace Gdiplus;
#endif

/// Small helper structure describing an ICO directory entry (not used directly)
struct IconDirEntry {
    uint8_t width;
    uint8_t height;
    uint8_t colorCount;
    uint8_t reserved;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t bytesInRes;
    uint32_t imageOffset;
};

static void write_le(std::ofstream &f, uint32_t v) { f.put(v & 0xFF); f.put((v>>8)&0xFF); f.put((v>>16)&0xFF); f.put((v>>24)&0xFF); }
static void write_le16(std::ofstream &f, uint16_t v) { f.put(v & 0xFF); f.put((v>>8)&0xFF); }

/**
 * Generate a pixel-art mouse silhouette with a falling effect.
 *
 * The returned buffer is RGBA (4 bytes per pixel), row-major, top-to-bottom.
 * The falling effect is produced by shifting the mouse downward by a small
 * offset and drawing a shadow and a faint vertical blur trail behind the mouse.
 *
 * @param w Width of the image in pixels.
 * @param h Height of the image in pixels.
 * @return RGBA byte buffer of size w*h*4.
 */
static std::vector<uint8_t> generate_mouse(int w, int h) {

    // Parameters controlling the falling appearance
    const int fall_offset = std::max(1, h / 10); // vertical shift downwards
    const int trail_length = std::max(2, h / 8);

    // Base center (before fall)
    int cx = w/2;
    int cy = h/2 - fall_offset/2;
    int rx = w/2 - 2;
    int ry = h/2 - 1;

    // Supersample scale (3x)
    const int SS = 3;
    int sw = w * SS;
    int sh = h * SS;
    std::vector<uint8_t> sbuf(sw*sh*4, 0);
    auto sset = [&](int x,int y,uint8_t r,uint8_t g,uint8_t b,uint8_t a){
        if(x<0||x>=sw||y<0||y>=sh) return;
        int i = (y*sw + x)*4;
        sbuf[i+0]=b; sbuf[i+1]=g; sbuf[i+2]=r; sbuf[i+3]=a;
    };
    // adjust parameters for supersampled canvas
    int scx = cx * SS;
    int scy = cy * SS;
    int srx = rx * SS;
    int sry = ry * SS;
    int sfall_offset = fall_offset * SS;
    int strail_length = trail_length; // keep same steps

    // Draw trail on supersampled buffer
    for(int t=0;t<strail_length;t++){
        float a = 0.10f * (1.0f - float(t) / strail_length); // slightly stronger trail
        int yshift = t * SS;
        for(int y=0;y<sh;y++){
            for(int x=0;x<sw;x++){
                int dx = x - scx;
                int dy = y - (scy - yshift) + SS;
                if((dx*dx)*sry*sry + (dy*dy)*srx*srx <= srx*srx*sry*sry){
                    int i=(y*sw+x)*4;
                    uint8_t trail_a = uint8_t(255 * a);
                    if(trail_a){
                        // blend towards pale green trail (200,238,200)
                        sbuf[i+0] = uint8_t((sbuf[i+0] * (255 - trail_a) + (uint8_t)200 * trail_a) / 255);
                        sbuf[i+1] = uint8_t((sbuf[i+1] * (255 - trail_a) + (uint8_t)238 * trail_a) / 255);
                        sbuf[i+2] = uint8_t((sbuf[i+2] * (255 - trail_a) + (uint8_t)200 * trail_a) / 255);
                        sbuf[i+3] = uint8_t(std::min(255, sbuf[i+3] + trail_a));
                    }
                }
            }
        }
    }
    // Draw main silhouette on supersampled buffer
    scy += sfall_offset;
    std::vector<uint8_t> smask(sw*sh,0);
    for(int y=0;y<sh;y++){
        for(int x=0;x<sw;x++){
            int dx = x - scx;
            int dy = y - scy + SS;
            if((dx*dx)*sry*sry + (dy*dy)*srx*srx <= srx*srx*sry*sry){
                smask[y*sw+x]=1;
                sset(x,y,200,238,200,255);
            }
        }
    }
    // White outline in supersampled buffer
    for(int y=0;y<sh;y++){
        for(int x=0;x<sw;x++){
            if(!smask[y*sw+x]) continue;
            bool edge=false;
            // Use 8-neighborhood to make a thicker outline at supersampled scale
            for(int oy=-1;oy<=1;oy++) for(int ox=-1;ox<=1;ox++){
                if(ox==0 && oy==0) continue;
                int xx=x+ox; int yy=y+oy;
                if(xx<0||xx>=sw||yy<0||yy>=sh || !smask[yy*sw+xx]){ edge=true; break; }
            }
            if(edge) sset(x,y,255,255,255,255);
        }
    }
    // details on supersampled
    sset(scx+4*SS, scy-3*SS, 10,20,10,255);
    sset(scx+5*SS, scy-3*SS, 10,20,10,255);
    for(int i=0;i<8*SS;i++) sset(scx+srx-2*SS+i, scy+3*SS + (i/2) + (i/6), 170,210,170,255);
    // shadow
    for(int y=0;y<sh;y++){
        for(int x=0;x<sw;x++){
            int dx = x - scx;
            int dy = y - (scy + sry + 2*SS);
            float dist = std::sqrt(float(dx*dx + dy*dy));
            float spread = sry * 1.2f;
            if(dist < spread){
                float alpha = (1.0f - dist / spread) * 0.45f;
                int i=(y*sw+x)*4; uint8_t shadow_a = uint8_t(255*alpha);
                sbuf[i+0] = uint8_t((sbuf[i+0] * (255 - shadow_a) + 0 * shadow_a) / 255);
                sbuf[i+1] = uint8_t((sbuf[i+1] * (255 - shadow_a) + 0 * shadow_a) / 255);
                sbuf[i+2] = uint8_t((sbuf[i+2] * (255 - shadow_a) + 0 * shadow_a) / 255);
                sbuf[i+3] = uint8_t(std::min(255, sbuf[i+3] + shadow_a));
            }
        }
    }
    // Downsample by averaging 2x2 blocks into final buffer
    std::vector<uint8_t> buf(w*h*4,0);
    for(int y=0;y<h;y++){
        for(int x=0;x<w;x++){
            int r=0,g=0,b=0,a=0; int cnt=0;
            for(int sy=0;sy<SS;sy++) for(int sx=0;sx<SS;sx++){
                int sxp = x*SS + sx; int syp = y*SS + sy; int i=(syp*sw + sxp)*4;
                b += sbuf[i+0]; g += sbuf[i+1]; r += sbuf[i+2]; a += sbuf[i+3]; cnt++; }
            int i=(y*w + x)*4; buf[i+0]=uint8_t(b/cnt); buf[i+1]=uint8_t(g/cnt); buf[i+2]=uint8_t(r/cnt); buf[i+3]=uint8_t(a/cnt);
        }
    }

    return buf;
}

/**
 * Create BMP-like image data expected inside an ICO entry. The function writes
 * a BITMAPINFOHEADER followed by bottom-up BGRA pixel rows and an AND mask.
 *
 * @param w Width in pixels.
 * @param h Height in pixels.
 * @param rgba RGBA buffer, top-to-bottom row-major.
 * @return Byte buffer containing the BMP+mask image as used in ICO resources.
 */
static std::vector<uint8_t> make_bmp_masked(int w,int h,const std::vector<uint8_t>&rgba){
    // We'll write BITMAPINFOHEADER + raw BGRA pixels (BI_RGB) and PNG-like alpha mask appended as AND mask
    int rowBytes = ((w * 32 + 31) / 32) * 4; // for AND mask rows (1bpp) align to 32 bits
    (void)rowBytes; // rowBytes variable retained for clarity; some compilers warn if unused
    std::vector<uint8_t> out;
    auto push_u32 = [&](uint32_t v){ out.push_back(v & 0xFF); out.push_back((v>>8)&0xFF); out.push_back((v>>16)&0xFF); out.push_back((v>>24)&0xFF); };
    auto push_u16 = [&](uint16_t v){ out.push_back(v & 0xFF); out.push_back((v>>8)&0xFF); };
    // BITMAPINFOHEADER
    push_u32(40); // header size
    push_u32(w);
    push_u32(h*2); // height = color height + mask height (we supply mask after color)
    push_u16(1); // planes
    push_u16(32); // bitcount
    push_u32(0); // compression BI_RGB
    push_u32(w*h*4); // image size
    push_u32(0); push_u32(0); // xpels, ypels
    push_u32(0); // colors used
    push_u32(0); // important colors
    // pixel data (bottom-up)
    for(int y=h-1;y>=0;y--){
        for(int x=0;x<w;x++){
            int i = (y*w + x)*4;
            out.push_back(rgba[i+0]); // B
            out.push_back(rgba[i+1]); // G
            out.push_back(rgba[i+2]); // R
            out.push_back(rgba[i+3]); // A
        }
    }
    // AND mask 1bpp (0 = draw, 1 = transparent) - rows bottom-up, each row padded to 32 bits
    for(int y=h-1;y>=0;y--){
        uint8_t cur=0; int bit=7;
        for(int x=0;x<w;x++){
            int i = (y*w + x)*4;
            uint8_t alpha = rgba[i+3];
            uint8_t bitv = (alpha < 128) ? 1 : 0;
            cur |= (bitv<<bit);
            bit--;
            if(bit<0){ out.push_back(cur); cur=0; bit=7; }
        }
        if(bit!=7) out.push_back(cur);
        // pad to 4-byte boundary
        while((out.size() % 4) != 0) out.push_back(0);
    }
    return out;
}

/**
 * Program entrypoint. Writes an ICO file with 32x32 and 16x16 images.
 *
 * The first CLI argument can be an output path for the ICO.
 */
#ifdef _WIN32
// Helper: get encoder CLSID for given MIME type
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    UINT num = 0;          // number of image encoders
    UINT size = 0;         // size of the image encoder array in bytes

    GetImageEncodersSize(&num, &size);
    if(size == 0) return -1;  // Failure

    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if(pImageCodecInfo == NULL) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);

    for(UINT j = 0; j < num; ++j)
    {
        if(wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
        {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;  // Success
        }
    }
    free(pImageCodecInfo);
    return -1;  // Failure
}

// Convert RGBA buffer to PNG bytes using GDI+; returns empty vector on failure
static std::vector<uint8_t> png_from_rgba(int w, int h, const std::vector<uint8_t>& rgba){
    std::vector<uint8_t> out;
    IStream* istream = NULL;
    if(CreateStreamOnHGlobal(NULL, TRUE, &istream) != S_OK) return out;
    {
        Bitmap bmp(w, h, PixelFormat32bppARGB);
        // set pixels
        for(int y=0;y<h;y++){
            for(int x=0;x<w;x++){
                int i = (y*w + x)*4;
                Color c(rgba[i+3], rgba[i+2], rgba[i+1], rgba[i+0]); // A,R,G,B
                bmp.SetPixel(x, y, c);
            }
        }
        CLSID pngClsid;
        if(GetEncoderClsid(L"image/png", &pngClsid) < 0){ istream->Release(); return out; }
        if(bmp.Save(istream, &pngClsid, NULL) != Ok){ istream->Release(); return out; }

        // read IStream to HGLOBAL
        HGLOBAL hg = NULL;
        if(GetHGlobalFromStream(istream, &hg) != S_OK){ istream->Release(); return out; }
        SIZE_T sz = GlobalSize(hg);
        void* data = GlobalLock(hg);
        if(data){
            out.resize(sz);
            memcpy(out.data(), data, sz);
            GlobalUnlock(hg);
        }
    }
    istream->Release();
    return out;
}
#endif

static bool write_ico_file(const char* outpath, const std::vector<int>& sizes){
    std::ofstream f(outpath, std::ios::binary);
    if(!f) return false;
    // ICONDIR
    f.put(0); f.put(0); // reserved
    f.put(1); f.put(0); // type 1 = icon
    uint16_t count = (uint16_t)sizes.size();
    f.put((uint8_t)(count & 0xFF)); f.put((uint8_t)((count>>8)&0xFF));
    // Reserve space for entries
    std::streampos entries_pos = f.tellp();
    for(size_t i=0;i<sizes.size();i++){
        for(int j=0;j<16;j++) f.put(0);
    }
    struct ImgInfo { uint32_t sizeBytes; uint32_t offset; uint8_t width; uint8_t height; std::vector<uint8_t> data; };
    std::vector<ImgInfo> imgs;
    for(int s : sizes){
#ifdef _WIN32
        if(s==256){
            auto rgba = generate_mouse(s,s);
            auto png = png_from_rgba(s,s,rgba);
            ImgInfo info;
            info.sizeBytes = (uint32_t)png.size();
            info.offset = (uint32_t)f.tellp();
            info.width = 0; // 0 means 256
            info.height = 0;
            info.data = std::move(png);
            imgs.push_back(std::move(info));
            f.write((char*)imgs.back().data.data(), imgs.back().data.size());
            continue;
        }
#endif
        auto bmp = generate_mouse(s,s);
        auto img = make_bmp_masked(s,s,bmp);
        ImgInfo info;
        info.sizeBytes = (uint32_t)img.size();
        info.offset = (uint32_t)f.tellp();
        info.width = (uint8_t)s;
        info.height = (uint8_t)s;
        info.data = std::move(img);
        imgs.push_back(std::move(info));
        f.write((char*)imgs.back().data.data(), imgs.back().data.size());
    }
    // go back and write directory entries
    f.seekp(entries_pos);
    for(const auto &info : imgs){
        f.put(info.width); // width
        f.put(info.height); // height
        f.put(0); // color count
        f.put(0); // reserved
        write_le16(f, 1); // planes
        write_le16(f, 32); // bitcount
        write_le(f, info.sizeBytes);
        write_le(f, info.offset);
    }
    f.close();
    return true;
}

int main(int argc, char** argv){
    const char* outpath = "res/altrightclick.ico";
    if(argc > 1) outpath = argv[1];
#ifdef _WIN32
    // Initialize GDI+ for PNG encoding
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
#endif
    // Write a minimal compatible ICO (32,16) to the requested path so RC is happy
    write_ico_file(outpath, std::vector<int>{32,16});
    // Also write a multi-size ICO for richer assets and testing (include 256 for HD)
    std::string outstr = outpath;
    size_t pos = outstr.find_last_of("/\\");
    std::string dir = (pos==std::string::npos) ? std::string() : outstr.substr(0,pos+1);
    std::string multi = dir + "altrightclick_multi.ico";
    std::vector<int> multi_sizes = {512,256,64,48,32,16};
    write_ico_file(multi.c_str(), multi_sizes);

    // Export per-size BMPs for quick review (write 32-bit BMP files)
    auto write_bmp_file = [&](const std::string &path, int w, int h, const std::vector<uint8_t>& rgba)->bool{
        std::ofstream of(path, std::ios::binary);
        if(!of) return false;
        // BITMAPFILEHEADER (14 bytes)
        uint32_t bfSize = 14 + 40 + w*h*4; // file header + info header + pixel data
        of.put('B'); of.put('M');
        of.put((char)(bfSize & 0xFF)); of.put((char)((bfSize>>8)&0xFF)); of.put((char)((bfSize>>16)&0xFF)); of.put((char)((bfSize>>24)&0xFF));
        // bfReserved1/2
        of.put(0); of.put(0); of.put(0); of.put(0);
        // bfOffBits (14 + 40)
        uint32_t offBits = 14 + 40;
        of.put((char)(offBits & 0xFF)); of.put((char)((offBits>>8)&0xFF)); of.put((char)((offBits>>16)&0xFF)); of.put((char)((offBits>>24)&0xFF));
        // BITMAPINFOHEADER (40 bytes)
        auto push_u32 = [&](uint32_t v){ of.put((char)(v & 0xFF)); of.put((char)((v>>8)&0xFF)); of.put((char)((v>>16)&0xFF)); of.put((char)((v>>24)&0xFF)); };
        auto push_u16 = [&](uint16_t v){ of.put((char)(v & 0xFF)); of.put((char)((v>>8)&0xFF)); };
        push_u32(40); // biSize
        push_u32(w);
        push_u32(h); // height for BMP (color only)
        push_u16(1); // planes
        push_u16(32); // bitcount
        push_u32(0); // compression BI_RGB
        push_u32(w*h*4); // biSizeImage
        push_u32(0); push_u32(0); // biXPelsPerMeter biYPelsPerMeter
        push_u32(0); // biClrUsed
        push_u32(0); // biClrImportant
        // Pixel data (bottom-up)
        for(int y=h-1;y>=0;y--){
            for(int x=0;x<w;x++){
                int i = (y*w + x)*4;
                of.put((char)rgba[i+0]); // B
                of.put((char)rgba[i+1]); // G
                of.put((char)rgba[i+2]); // R
                of.put((char)rgba[i+3]); // A
            }
        }
        return true;
    };
    for(int s : multi_sizes){
        auto bmp = generate_mouse(s,s);
        std::string bmp_path = dir + "altrightclick_" + std::to_string(s) + ".bmp";
        write_bmp_file(bmp_path, s, s, bmp);
    }
#ifdef _WIN32
    GdiplusShutdown(gdiplusToken);
#endif
    return 0;
}
