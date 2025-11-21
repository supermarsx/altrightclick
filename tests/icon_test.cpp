/**
 * @file icon_test.cpp
 * @brief Smoke-test that the generated ICO contains the expected sizes.
 */

#include <fstream>
#include <vector>
#include <cstdint>
#include <iostream>

/**
 * @brief Validate that an ICO file includes required size entries.
 *
 * @param argc Argument count.
 * @param argv Argument vector (optional target ICO path).
 * @return 0 on success, non-zero if a required size is missing or IO fails.
 */
int main(int argc, char **argv) {
    const char* path = "build/x64/altrightclick.ico";
    if(argc>1) path = argv[1];
    std::ifstream f(path, std::ios::binary);
    if(!f){ std::cerr<<"failed to open "<<path<<"\n"; return 2; }
    auto read8=[&](){ return (uint8_t)f.get(); };
    auto read16=[&](){ uint16_t a=read8(); uint16_t b=read8(); return (uint16_t)(a | (b<<8)); };
    auto read32=[&](){ uint32_t a=read16(); uint32_t b=read16(); return (uint32_t)(a | (b<<16)); };
    // ICONDIR
    uint16_t reserved = read16();
    uint16_t type = read16();
    uint16_t count = read16();
    if(type!=1){ std::cerr<<"not an icon file\n"; return 3; }
    std::vector<int> widths;
    for(int i=0;i<count;i++){
        uint8_t w = read8(); uint8_t h = read8(); uint8_t color = read8(); uint8_t r = read8();
        uint16_t planes = read16(); uint16_t bpp = read16(); uint32_t bytes = read32(); uint32_t offset = read32();
        widths.push_back(w==0?256:w);
    }
    // expected sizes include 256 for high-DPI
    std::vector<int> want = {256,64,48,32,16};
    for(int s : want){ bool ok=false; for(auto w:widths) if(w==s) ok=true; if(!ok){ std::cerr<<"missing size "<<s<<"\n"; return 4; } }
    std::cout<<"OK: found sizes"<<std::endl;
    return 0;
}
