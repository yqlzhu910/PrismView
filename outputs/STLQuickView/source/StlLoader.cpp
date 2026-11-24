#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "StlLoader.h"
#include <osg/PrimitiveSet>
// OSG 3.4 / VC110 exports its std::ifstream base specialization. Import it
// consistently with the OBJ reader to avoid duplicate template symbols.
#include <osgDB/fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <float.h>
#include <fstream>
#include <limits>
#include <locale>
#include <stdexcept>

namespace stlview {
namespace {
const size_t ChunkTriangles=65536;
struct Canceled {};
void check(Progress& progress) { if(progress.canceled()) throw Canceled(); }
void memoryAvailable(unsigned __int64 required) {
    MEMORYSTATUSEX memory={sizeof(memory)};
    if(GlobalMemoryStatusEx(&memory) && required+256ULL*1024*1024>memory.ullAvailPhys)
        throw std::runtime_error("memory");
}
void update(Progress& progress,unsigned __int64 done,unsigned __int64 total) {
    InterlockedExchange(&progress.percent,total?LONG(std::min<unsigned __int64>(100,done*100/total)):0);
}
struct Chunk {
    osg::ref_ptr<osg::Vec3Array> vertices, normals;
    Chunk(size_t capacity):vertices(new osg::Vec3Array),normals(new osg::Vec3Array) {
        memoryAvailable(capacity*72);
        vertices->reserve(capacity*3); normals->reserve(capacity*3);
    }
    void append(const osg::Vec3& a,const osg::Vec3& b,const osg::Vec3& c) {
        for(int k=0;k<3;++k)
            if(!_finite(a[k]) || !_finite(b[k]) || !_finite(c[k])) throw std::runtime_error("invalid");
        // Match the stock STL reader's geometric face normals; never smooth or decimate.
        osg::Vec3d n=(osg::Vec3d(b)-osg::Vec3d(a))^(osg::Vec3d(c)-osg::Vec3d(a));
        n.normalize();
        osg::Vec3 normal(float(n.x()),float(n.y()),float(n.z()));
        vertices->push_back(a); vertices->push_back(b); vertices->push_back(c);
        normals->push_back(normal); normals->push_back(normal); normals->push_back(normal);
    }
    void finish(Model& model) {
        if(vertices->empty()) return;
        osg::ref_ptr<osg::Geometry> geometry=new osg::Geometry;
        geometry->setVertexArray(vertices);
        geometry->setNormalArray(normals,osg::Array::BIND_PER_VERTEX);
        geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES,0,GLsizei(vertices->size())));
        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);
        geometry->setDataVariance(osg::Object::STATIC);
        geometry->getBound(); // Compute each bounded chunk off the UI thread.
        static_cast<osg::Geode*>(model.node.get())->addDrawable(geometry);
        model.chunks.push_back(geometry);
        model.triangles+=vertices->size()/3;
    }
};
void binary(std::ifstream& file,unsigned int count,Progress& progress,Model& model) {
    if(!count) throw std::runtime_error("empty");
    memoryAvailable(static_cast<unsigned __int64>(count)*72);
    std::vector<char> data(ChunkTriangles*50);
    file.seekg(84);
    for(unsigned __int64 first=0;first<count;first+=ChunkTriangles) {
        check(progress);
        size_t length=size_t(std::min<unsigned __int64>(ChunkTriangles,count-first));
        if(!file.read(&data[0],std::streamsize(length*50))) throw std::runtime_error("truncated");
        Chunk chunk(length);
        for(size_t i=0;i<length;++i) {
            if((i&1023)==0) check(progress);
            float xyz[9]; std::memcpy(xyz,&data[i*50+12],sizeof(xyz));
            chunk.append(osg::Vec3(xyz[0],xyz[1],xyz[2]),osg::Vec3(xyz[3],xyz[4],xyz[5]),osg::Vec3(xyz[6],xyz[7],xyz[8]));
        }
        check(progress); chunk.finish(model);
        update(progress,first+length,count);
    }
}
std::string token(std::istream& file) {
    std::string word;
    // Bound token size even for malformed binary input mislabelled as ASCII.
    file >> std::ws;
    while(file && file.peek()!=EOF && !std::isspace(static_cast<unsigned char>(file.peek()))) {
        if(word.size()==256) throw std::runtime_error("invalid");
        word.push_back(char(std::tolower(static_cast<unsigned char>(file.get()))));
    }
    return word;
}
void expect(std::istream& file,const char* word) {
    if(token(file)!=word) throw std::runtime_error("invalid");
}
void ascii(std::ifstream& file,Progress& progress,Model& model,bool bom) {
    file.clear(); file.seekg(bom?3:0); file.imbue(std::locale::classic());
    Chunk chunk(ChunkTriangles);
    bool solid=false;
    for(;;) {
        check(progress);
        std::string word=token(file);
        if(word.empty()) break;
        if(word=="solid" || word=="endsolid") {
            solid=true; file.ignore((std::numeric_limits<std::streamsize>::max)(),'\n'); continue;
        }
        if(word!="facet" || !solid) throw std::runtime_error("invalid");
        expect(file,"normal");
        double nx,ny,nz; if(!(file>>nx>>ny>>nz)) throw std::runtime_error("invalid");
        expect(file,"outer"); expect(file,"loop");
        osg::Vec3 v[3];
        for(int j=0;j<3;++j) { expect(file,"vertex"); if(!(file>>v[j].x()>>v[j].y()>>v[j].z())) throw std::runtime_error("invalid"); }
        expect(file,"endloop"); expect(file,"endfacet");
        chunk.append(v[0],v[1],v[2]);
        if(chunk.vertices->size()==ChunkTriangles*3) {
            chunk.finish(model); chunk=Chunk(ChunkTriangles);
            std::streampos position=file.tellg();
            if(position>=0) update(progress,static_cast<unsigned __int64>(position),model.fileBytes);
        }
    }
    chunk.finish(model);
    if(!model.triangles) throw std::runtime_error("empty");
}
}
bool read(const std::wstring& path,Progress& progress,Model& result,std::wstring& error) {
    result=Model(); error.clear(); InterlockedExchange(&progress.percent,0);
    try {
        check(progress);
        std::ifstream file(path.c_str(),std::ios::in|std::ios::binary);
        if(!file) throw std::runtime_error("open");
        file.seekg(0,std::ios::end);
        std::streampos size=file.tellg(); if(size<0) throw std::runtime_error("open");
        result.fileBytes=static_cast<unsigned __int64>(size);
        file.seekg(0);
        char header[84]={0}; file.read(header,sizeof(header)); file.clear();
        unsigned int count=0; std::memcpy(&count,header+80,4);
        unsigned __int64 expected=84ULL+50ULL*count;
        result.node=new osg::Geode;
        bool bom=static_cast<unsigned char>(header[0])==0xef && static_cast<unsigned char>(header[1])==0xbb && static_cast<unsigned char>(header[2])==0xbf;
        std::string prefix(header+(bom?3:0),size_t(std::min<unsigned __int64>(result.fileBytes,sizeof(header)))-(bom?3:0));
        size_t start=prefix.find_first_not_of(" \r\n\t");
        bool text=start!=std::string::npos && prefix.compare(start,5,"solid")==0;
        if(result.fileBytes>=84 && expected==result.fileBytes) binary(file,count,progress,result);
        else if(text) ascii(file,progress,result,bom);
        else if(result.fileBytes>=84 && expected<result.fileBytes) binary(file,count,progress,result);
        else throw std::runtime_error("truncated");
        check(progress);
        if(!result.node->getBound().valid()) throw std::runtime_error("empty");
        InterlockedExchange(&progress.percent,100); return true;
    } catch(const Canceled&) {
        error.clear();
    } catch(const std::bad_alloc&) {
        error=L"可用内存不足，无法继续读取这个模型。请关闭其他占用内存的程序后重试。";
    } catch(const std::exception& ex) {
        if(std::string(ex.what())=="memory") error=L"可用内存不足，已停止读取以避免系统卡死。请关闭其他占用内存的程序后重试。";
        else if(std::string(ex.what())=="open") error=L"无法打开文件，请检查路径和文件是否仍然存在。";
        else error=L"STL 文件不完整、格式不正确，或没有有效三角形。";
    }
    result=Model(); return false;
}
}
