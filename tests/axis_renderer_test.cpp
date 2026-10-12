#include "../src/gfx.cpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
int comparisons = 0, sourceArt = 0;
size_t comparedBytes = 0;
void require(bool ok,const std::string& detail) { if (!ok) throw std::runtime_error(detail); }
void fixture(const std::filesystem::path& path,bool alpha) {
    const int offset = alpha ? 122 : 54, bpp = alpha ? 4 : 3;
    std::vector<uint8_t> bytes(offset+8*8*bpp,0);
    auto put = [&](int at,uint32_t value,int count) { for (int i=0;i<count;++i) bytes[at+i]=uint8_t(value>>(i*8)); };
    bytes[0]='B'; bytes[1]='M'; put(2,uint32_t(bytes.size()),4); put(10,offset,4);
    put(14,alpha?108:40,4); put(18,8,4); put(22,uint32_t(-8),4); put(26,1,2); put(28,bpp*8,2); put(30,alpha?3:0,4);
    if (alpha) { put(54,0xff0000,4); put(58,0xff00,4); put(62,0xff,4); put(66,0xff000000,4); }
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) {
        const int i=offset+(y*8+x)*bpp;
        bytes[i]=uint8_t(x*31); bytes[i+1]=uint8_t(y*29); bytes[i+2]=uint8_t(255-x*17-y*13);
        if (alpha) bytes[i+3]=std::array<uint8_t,8>{0,1,32,64,128,192,254,255}[(x+y)%8];
        else if ((x+y)%5==0) { bytes[i]=255; bytes[i+1]=0; bytes[i+2]=255; }
    }
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    require(bool(out),"fixture write");
}
void scene(lt::Gfx& g,int tex,int variant) {
    g.beginFrame(); g.clear(0.08f,0.13f,0.21f);
    const float d=variant*0.125f;
    g.rect(-3.75f+d,-2.125f,47.5f,21.75f,0.7f,0.5f,0.3f,0.45f);
    g.spriteUV(tex,-10.25f+d,17.125f,100.5f,65.75f,0.125f,0.25f,0.875f,0.75f);
    g.spriteUV(tex,178.375f,23.125f,-79.25f,52.5f,0.875f,0.75f,0.125f,0.25f);
    g.spriteUV(tex,28.125f,138.25f,76.5f,-47.75f,0,1,1,0);
    g.spriteUVTinted(tex,112.375f,100.25f,81.5f,72.375f,0,0,1,1,0.63f,1.2f,0.15f);
    g.spriteUV(tex,384.5f,223.75f,51.25f,33.5f,0,0,1,1);
    g.spriteUV(tex,240.5f,30.5f,0,35,0,0,1,1);
    g.spriteUV(tex,300,80,60,0,0,0,1,1);
    g.spriteEx(tex,230.125f,105.75f,0.08f,-0.09f,0.37f,0.9f,0.7f,1,0.65f);
    g.print("AXIS 2D",206.125f,197.375f,0.92f,0.81f,0.67f,0.8f);
    g.endFrame();
}
void compare(const std::filesystem::path& path,int density,int variant,bool fractional=false) {
    lt::Gfx g;
    require(fractional?g.initNative(850,510):g.init(density),"renderer init");
    const int tex=g.loadTexture(path.string()); require(tex>=0,"load "+path.string());
    g.setAxisFastPath(false); scene(g,tex,variant);
    const std::vector<uint8_t> reference(g.framebuffer(),g.framebuffer()+g.framebufferSize());
    g.setAxisFastPath(true); scene(g,tex,variant);
    for (size_t i=0;i<reference.size();++i)
        if (reference[i]!=g.framebuffer()[i])
            throw std::runtime_error("axis/reference RGBA mismatch "+path.filename().string()+" density="+std::to_string(density)+" variant="+std::to_string(variant)+" byte="+std::to_string(i)+" expected="+std::to_string(reference[i])+" actual="+std::to_string(g.framebuffer()[i]));
    ++comparisons; comparedBytes+=reference.size();
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2||argc==3,"usage: axis-test OWNED_FIXTURE_DIR [READ_ONLY_ART_DIR]");
        const std::filesystem::path dir(argv[1]); require(std::filesystem::is_directory(dir),"fixture directory exists");
        for (bool alpha : {false,true}) {
            const auto path=dir/(alpha?"axis-alpha.bmp":"axis-keyed.bmp");
            require(!std::filesystem::exists(path),"fresh fixture path"); fixture(path,alpha);
            for (int density : {1,2,4}) for (int variant=0;variant<8;++variant) compare(path,density,variant);
            compare(path,0,3,true);
        }
        if (argc==3) {
            std::vector<std::filesystem::path> paths;
            for (const auto& item:std::filesystem::directory_iterator(argv[2]))
                if (item.is_regular_file()&&item.path().extension()==".bmp") paths.push_back(item.path());
            std::sort(paths.begin(),paths.end());
            require(!paths.empty()&&paths.size()<=128,"bounded nonempty actual-art inventory");
            for (const auto& path:paths) {
                for (int density:{1,2,4}) compare(path,density,2);
                compare(path,0,5,true); ++sourceArt;
            }
        }
        std::printf("{\"status\":\"passed\",\"frame_comparisons\":%d,\"compared_rgba_bytes\":%zu,\"actual_art_files\":%d}\n",comparisons,comparedBytes,sourceArt);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"axis renderer: %s\n",e.what()); return 1; }
}
