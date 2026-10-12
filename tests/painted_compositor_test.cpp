// Uses the real implementation, including the established sampler as an
// independent per-pixel oracle. No platform, SDL or generated-media substitutes.
#include "../src/gfx.cpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
std::atomic<bool> countAllocations{false};
std::atomic<size_t> allocations{0};
std::atomic<int> failAfter{-1};
int checks=0, frameComparisons=0, legacyMaxDelta=0, actualArt=0;
size_t oraclePixels=0;
void require(bool ok,const char* detail) { ++checks; if (!ok) throw std::runtime_error(detail); }
using Pixels=std::vector<uint8_t>;
Pixels pixels(const lt::Gfx& g) { return {g.framebuffer(),g.framebuffer()+g.framebufferSize()}; }
void fixture(const std::filesystem::path& path,int kind,bool hidden=false) {
    std::vector<uint8_t> bytes(122+8*8*4,0);
    auto put=[&](int at,uint32_t value,int n) { for(int i=0;i<n;++i) bytes[at+i]=uint8_t(value>>(i*8)); };
    bytes[0]='B';bytes[1]='M';put(2,uint32_t(bytes.size()),4);put(10,122,4);put(14,108,4);
    put(18,8,4);put(22,uint32_t(-8),4);put(26,1,2);put(28,32,2);put(30,3,4);
    put(54,0xff0000,4);put(58,0xff00,4);put(62,0xff,4);put(66,0xff000000,4);
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
        const int i=122+(y*8+x)*4;
        if(kind==2) { // hostile adjacent atlas cells, including hidden RGB
            bytes[i+2]=x<4?255:0;bytes[i+1]=x<4?0:255;bytes[i]=x<4?0:255;
            bytes[i+3]=(x+y)%3==0?0:255;
        } else {
            bytes[i]=uint8_t(x*31);bytes[i+1]=uint8_t(y*29);bytes[i+2]=uint8_t(255-x*17-y*13);
            bytes[i+3]=kind==0?255:std::array<uint8_t,8>{0,1,32,64,128,192,254,255}[(x+y)%8];
        }
        if(hidden&&bytes[i+3]==0) { bytes[i]=123;bytes[i+1]=222;bytes[i+2]=17; }
    }
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    require(bool(out),"fixture write");
}
void scene(lt::Gfx& g,int tex,int variant,bool rotation=true) {
    const float d=variant*0.125f;
    g.beginFrame();g.clear(0.08f,0.13f,0.21f);
    g.rect(-3.75f+d,-2.125f,47.5f,21.75f,0.7f,0.5f,0.3f,0.45f);
    g.spriteUV(tex,-10.25f+d,17.125f,100.5f,65.75f,0.125f,0.25f,0.875f,0.75f);
    g.spriteUV(tex,178.375f,23.125f,-79.25f,52.5f,0.875f,0.75f,0.125f,0.25f);
    g.spriteUV(tex,28.125f,138.25f,76.5f,-47.75f,0,1,1,0);
    g.spriteUVTinted(tex,112.375f,100.25f,81.5f,72.375f,0,0,1,1,0.63f,1.2f,0.15f);
    g.spriteUV(tex,384.5f,223.75f,51.25f,33.5f,0,0,1,1);
    g.spriteUV(tex,240.5f,30.5f,0,35,0,0,1,1);
    g.spriteUV(tex,300,80,60,0,0,0,1,1);
    g.rect(30.5f,30.5f,23,23,0.4f,0.9f,1.3f,0.5f);
    if(rotation) g.spriteEx(tex,60,60,4,-4,0.37f,0.9f,0.7f,1,0.65f);
    g.rect(55,55,16,16,1,0,0,0.3f);
    g.print("PAINTED2D",206.125f,197.375f,0.92f,0.81f,0.67f,0.8f);
    g.endFrame();
}
void parity(const std::filesystem::path& path,int width,int height,int variant) {
    lt::Gfx g;
    require(g.initNative(width,height),"parity init");
    const int tex=g.loadTexture(path.string());require(tex>=0,"parity texture");
    require(g.setPainted2D(true,1),"enable serial");scene(g,tex,variant);
    const Pixels serial=pixels(g);
    for(int workers:{2,8}) {
        require(g.setPainted2D(true,workers),"enable parallel");scene(g,tex,variant);
        require(serial==pixels(g),"single/multiple workers RGBA byte parity");++frameComparisons;
    }
    require(g.setPainted2D(false),"disable painted");scene(g,tex,variant);
    int delta=0;
    for(size_t i=0;i<serial.size();++i) delta=std::max(delta,std::abs(int(serial[i])-int(g.framebuffer()[i])));
    legacyMaxDelta=std::max(legacyMaxDelta,delta);
    // Affine UV/constant tint remove legacy barycentric rounding. Under this
    // deliberately overlapping fixture, each output channel stays within4.
    require(delta<=4,"bounded legacy/direct rounding difference");
}
void oracle(const std::filesystem::path& path,int kind,bool mirror) {
    lt::Gfx g;require(g.initNative(850,510),"oracle init");g.setPainted2D(true,8);
    const int tex=g.loadTexture(path.string());require(tex>=0,"oracle texture");
    lt::Gfx::Tex source;bool alpha=false;
    require(lt::loadBmpFile(path.string(),source.w,source.h,source.px,alpha),"oracle source");
    const float u0=mirror?0.5f:0,u1=mirror?0:0.5f,v0=mirror?1:0,v1=mirror?0:1;
    const auto clamp=lt::texClampFor(&source,u0,v0,u1,v1);
    const float x=30.5f,y=20.5f,w=100.25f,h=80.75f;
    const float tint[3]={0.65f,1.3f,0.2f};
    g.beginFrame();g.clear(0.1f,0.2f,0.3f);
    g.spriteUVTinted(tex,x,y,w,h,u0,v0,u1,v1,tint[0],tint[1],tint[2]);g.endFrame();
    const float scale=g.renderScale();
    for(int py=0;py<g.framebufferHeight();++py) for(int px=0;px<g.framebufferWidth();++px) {
        const auto* dst=g.framebuffer()+(size_t(py)*g.framebufferWidth()+px)*4;
        const bool covered=px+0.5f>x*scale&&px+0.5f<=(x+w)*scale&&py+0.5f>y*scale&&py+0.5f<=(y+h)*scale;
        if(!covered) { require(dst[0]==25&&dst[1]==51&&dst[2]==76&&dst[3]==255,"clip/edge untouched");continue; }
        const float u=u0+((px+0.5f)-x*scale)*((u1-u0)/(w*scale));
        const float v=v0+((py+0.5f)-y*scale)*((v1-v0)/(h*scale));
        float sample[4];lt::sampleTex(&source,u,v,clamp,sample);
        const int background[3]={25,51,76};
        for(int k=0;k<3;++k) {
            const int expected=sample[3]<=0.004f?background[k]:uint8_t(lt::to8(sample[k]*tint[k])*sample[3]+background[k]*(1-sample[3]));
            require(std::abs(int(dst[k])-expected)<=1,"independent alpha-weighted four-tap sampler oracle");
        }
        require(dst[3]==255,"2D preserves framebuffer alpha");++oraclePixels;
        if(kind==2) require(dst[1]<=51&&dst[2]<=76,"all four taps stay inside selected red atlas cell");
    }
}
void lifecycle(const std::filesystem::path& path) {
    {
        lt::Gfx empty;
        for(int workers:{1,8}) {
            require(empty.setPainted2D(true,workers),"empty target accepts bounded compositor configuration");
            empty.beginFrame();empty.clear(0.2f,0.3f,0.4f);empty.endFrame();
            require(empty.framebufferSize()==0,"configured uninitialized frame operations are no-ops");
        }
        failAfter=0;
        require(!empty.initNative(3600,2160),"failed init returns empty target");failAfter=-1;
        empty.beginFrame();empty.clear(0.2f,0.3f,0.4f);empty.endFrame();
        require(empty.framebufferSize()==0,"failed-init frame operations are no-ops");
    }
    lt::Gfx g;require(g.init(1),"lifecycle init");
    require(!g.painted2DEnabled()&&g.painted2DWorkers()==1,"legacy default");
    require(!g.setPainted2D(true,9)&&!g.painted2DEnabled(),"invalid worker count unchanged");
    for(int fail=0;fail<8;++fail) {
        failAfter=fail;require(g.setPainted2D(true,8),"allocation failure enables bounded serial fallback");failAfter=-1;
        require(g.painted2DWorkers()==1,"pool construction failure joins partial workers and falls back serial");
        g.beginFrame();g.clear(0,0,0);g.rect(0,0,400,240,1,0,0,1);g.endFrame();
        require(g.framebuffer()[0]==255,"fallback remains functional");
    }
    g.setPainted2D(true,8);
    int tex=g.loadTexture(path.string());require(tex>=0,"lifecycle texture");scene(g,tex,0);
    const auto first=pixels(g);g.reset();require(g.painted2DWorkers()==8,"reset retains warm bounded pool");
    tex=g.loadTexture(path.string());scene(g,tex,0);require(first==pixels(g),"reset and reload identical");
    require(g.init(2),"reinitialize existing renderer");tex=g.loadTexture(path.string());
    scene(g,tex,0);const auto again=pixels(g);
    lt::Gfx moved=std::move(g);scene(moved,tex,0);require(again==pixels(moved),"move construction keeps pool owner valid");
    g.beginFrame();g.clear(1,0,0);g.endFrame();require(g.framebufferSize()==0,"moved-from frame operations are safe");
    lt::Gfx assigned;assigned=std::move(moved);scene(assigned,tex,0);require(again==pixels(assigned),"move assignment keeps pool owner valid");
    require(assigned.initNative(850,510),"reinit native");tex=assigned.loadTexture(path.string());scene(assigned,tex,0);
    const auto capacity=assigned.frameCapacityBytes();
    allocations=0;countAllocations=true;
    bool good=true;
    for(auto size:{std::pair<int,int>{400,240},{850,510},{1800,1080},{5,3}}) {
        good=good&&assigned.resizeNative(size.first,size.second);scene(assigned,tex,1);
    }
    countAllocations=false;
    require(good&&allocations==0&&capacity==assigned.frameCapacityBytes(),"warmed frames/resizes allocate nothing");
    assigned=lt::Gfx{};require(assigned.framebufferSize()==0,"engine-style teardown joins workers");
    assigned.beginFrame();assigned.clear(1,0,0);assigned.endFrame();
    require(assigned.framebufferSize()==0,"torn-down frame operations are safe");
}
void transformedAnd3D(const std::filesystem::path& path) {
    lt::Gfx g;require(g.init(2),"3D init");const int tex=g.loadTexture(path.string()),mesh=g.makeCube();
    auto draw=[&] {
        g.beginFrame();g.clear(0.1f,0.2f,0.3f);
        g.drawMesh(mesh,tex,lt::translate(0,0,0),1,1,1);
        g.clear(0.2f,0.1f,0.3f); // reset depth between immediate 3D calls
        g.drawMesh(mesh,tex,lt::translate(0.3f,0,0),1,1,1);
        g.spriteEx(tex,200,100,4,-3,0.4f,0.7f,0.9f,1,0.8f);
        g.endFrame();
    };
    draw();const auto reference=pixels(g);g.setPainted2D(true,8);draw();
    require(reference==pixels(g),"3D/depth reset and transformed fallback preserve legacy bytes");
    g.beginFrame();g.clear(0,0,0);g.rect(0,0,400,240,1,0,0,1);
    g.setPainted2D(false);g.rect(30,30,10,10,0,1,0,1);g.endFrame();
    require(g.framebuffer()[0]==255&&g.framebuffer()[(size_t(70)*800+70)*4+1]==255,"mode change flushes queued work in order");
    g.setPainted2D(true,8);g.beginFrame();g.clear(0,0,0);
    g.rect(std::numeric_limits<float>::quiet_NaN(),0,5,5,1,0,0,1);
    g.rect(0,0,10,10,0,1,0,1);g.endFrame();
    require(g.framebuffer()[1]==255,"nonfinite queued geometry is discarded without poisoning later quads");
}
void concurrentInstances(const std::filesystem::path& path) {
    lt::Gfx one,two;require(one.init(1)&&two.init(2),"separate instance init");
    one.setPainted2D(true,2);two.setPainted2D(true,8);
    const int a=one.loadTexture(path.string()),b=two.loadTexture(path.string());
    scene(one,a,1);scene(two,b,2);const auto expectedOne=pixels(one),expectedTwo=pixels(two);
    std::thread t([&]{for(int i=0;i<12;++i)scene(two,b,2);});
    for(int i=0;i<12;++i)scene(one,a,1);t.join();
    require(expectedOne==pixels(one)&&expectedTwo==pixels(two),"concurrent instances retain independent workers and targets");
}
void benchmark(const std::filesystem::path& root,bool legacy) {
    lt::Gfx g;require(g.initNative(3600,2160),"benchmark init");
    const int world=g.loadTexture((root/"world-0.bmp").string());
    const int hero=g.loadTexture((root/"hero-atlas.bmp").string());
    const int font=g.loadTexture((root/"font.bmp").string());require(world>=0&&hero>=0&&font>=0,"real benchmark art");
    auto draw=[&](bool painted) {
        g.beginFrame();g.clear(0,0,0);
        if(!painted)g.rect(0,0,400,240,0.7f,0.5f,0.3f,1);
        else {
            g.spriteUV(world,-200,0,800,240,0,0,1,1);
            for(int i=0;i<6;++i)g.spriteUV(hero,25.0f+i*60,148,64,64,i/8.0f,0,(i+1)/8.0f,0.25f);
            for(int i=0;i<160;++i) {
                const float u=float(i%16)/16,v=float(i/16)/11;
                g.spriteUVTinted(font,float(i%60)*6,float(i/60)*11,6,11,u,v,u+1.0f/16,v+1.0f/11,0.94f,0.88f,0.72f);
            }
        }
        g.endFrame();
    };
    std::printf("{\"kind\":\"painted2d_cpu_benchmark\",\"width\":3600,\"height\":2160,\"samples\":[");
    bool first=true;
    for(int workers: {0,1,8}) {
        if(workers==0&&!legacy)continue;
        g.setPainted2D(workers!=0,workers);
        for(bool painted:{false,true}) {
            for(int i=0;i<8;++i)draw(painted);
            std::array<double,120> ms;allocations=0;countAllocations=true;
            for(auto& value:ms) {
                const auto start=std::chrono::steady_clock::now();draw(painted);
                value=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            }
            countAllocations=false;std::sort(ms.begin(),ms.end());
            std::printf("%s{\"mode\":\"%s\",\"workers\":%d,\"scene\":\"%s\",\"warmup_frames\":8,\"measured_frames\":120,\"p50_ms\":%.3f,\"p95_ms\":%.3f,\"max_ms\":%.3f,\"frame_allocations\":%zu}",first?"":",",workers==0?"legacy":"painted2d",workers==0?1:g.painted2DWorkers(),painted?"current_panorama_six_sprites_160_glyphs":"opaque_full_frame_rect",ms[59],ms[113],ms[119],allocations.load());
            std::fflush(stdout);first=false;
        }
    }
    std::printf("],\"includes_native_upload\":false,\"includes_game_update\":false}\n");
}
}
void* operator new(size_t bytes) {
    int value=failAfter.load();
    if(value>=0&&failAfter.fetch_sub(1)==0){failAfter=-1;throw std::bad_alloc();}
    if(countAllocations)++allocations;
    if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
int main(int argc,char** argv) {
    try {
        if(argc>=3&&std::string(argv[1])=="--benchmark") {benchmark(argv[2],argc==4&&std::string(argv[3])=="--legacy");return 0;}
        require(argc==2||argc==3,"usage: painted-compositor-test OWNED_FIXTURE_DIR [READ_ONLY_ART_DIR] | --benchmark READ_ONLY_ART_DIR [--legacy]");
        const std::filesystem::path dir(argv[1]);require(std::filesystem::is_directory(dir),"owned fixture directory");
        for(int kind=0;kind<3;++kind) {
            const auto path=dir/("painted-"+std::to_string(kind)+".bmp");require(!std::filesystem::exists(path),"fresh fixture");fixture(path,kind);
            for(auto size:{std::pair<int,int>{400,240},{850,510},{1600,960}})for(int variant=0;variant<4;++variant)parity(path,size.first,size.second,variant);
            for(bool mirror:{false,true})oracle(path,kind,mirror);
        }
        const auto source=dir/"painted-1.bmp",hidden=dir/"painted-hidden.bmp";fixture(hidden,1,true);
        lt::Gfx a,b;require(a.init(2)&&b.init(2),"hidden RGB init");a.setPainted2D(true,1);b.setPainted2D(true,8);
        const int ta=a.loadTexture(source.string()),tb=b.loadTexture(hidden.string());scene(a,ta,0);scene(b,tb,0);
        require(pixels(a)==pixels(b),"invisible source RGB never changes output");
        lifecycle(source);transformedAnd3D(source);concurrentInstances(source);
        if(argc==3) {
            std::vector<std::filesystem::path> paths;
            for(const auto& item:std::filesystem::directory_iterator(argv[2]))
                if(item.is_regular_file()&&item.path().extension()==".bmp")paths.push_back(item.path());
            std::sort(paths.begin(),paths.end());require(!paths.empty()&&paths.size()<=128,"bounded real art inventory");
            for(const auto& path:paths) {parity(path,850,510,2);++actualArt;}
        }
        std::printf("{\"status\":\"passed\",\"assertions\":%d,\"worker_frame_comparisons\":%d,\"sampler_oracle_pixels\":%zu,\"max_legacy_fixture_channel_delta\":%d,\"actual_art_files\":%d}\n",checks,frameComparisons,oraclePixels,legacyMaxDelta,actualArt);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"painted compositor: %s\n",e.what());return 1;}
}
