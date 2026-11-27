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
#include <sstream>

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
struct Word {
    const char* data;
    size_t size;
    Word(const char* p,size_t n):data(p),size(n) {}
    bool is(const char* expected) const {
        if(std::strlen(expected)!=size) return false;
        for(size_t i=0;i<size;++i) if((data[i]|32)!=expected[i]) return false;
        return true;
    }
};

class TextInput {
    std::ifstream& file;
    Progress& progress;
    unsigned __int64 total,readBytes;
    std::vector<char> buffer;
    size_t pos,end;
    bool eof;
    std::string longNumber;
    static bool space(char c) { return c==' ' || (c>='\t' && c<='\r'); }
    void refill() {
        check(progress);
        size_t left=end-pos;
        if(left) std::memmove(&buffer[0],&buffer[pos],left);
        file.read(&buffer[left],buffer.size()-left);
        if(file.bad()) throw std::runtime_error("io");
        size_t n=size_t(file.gcount());
        pos=0; end=left+n; readBytes+=n; eof=end<buffer.size();
        update(progress,readBytes,total);
    }
public:
    TextInput(std::ifstream& stream,Progress& status,unsigned __int64 bytes)
        :file(stream),progress(status),total(bytes),readBytes(0),buffer(256*1024),pos(0),end(0),eof(false) {}
    Word word() {
        for(;;) {
            while(pos<end && space(buffer[pos])) ++pos;
            if(pos<end || eof) break;
            refill();
        }
        if(end-pos<257 && !eof) refill();
        size_t start=pos;
        while(pos<end && !space(buffer[pos])) {
            if(pos-start==256) {
                char lead=buffer[start];
                if((lead<'0' || lead>'9') && lead!='+' && lead!='-' && lead!='.') throw std::runtime_error("invalid");
                longNumber.assign(&buffer[start],pos-start);
                for(;;) {
                    size_t more=pos;
                    while(pos<end && !space(buffer[pos])) ++pos;
                    longNumber.append(&buffer[0]+more,pos-more);
                    if(pos<end || eof) return Word(longNumber.data(),longNumber.size());
                    refill();
                }
            }
            ++pos;
        }
        return Word(&buffer[0]+start,pos-start);
    }
    void expect(const char* expected) {
        if(!word().is(expected)) throw std::runtime_error("invalid");
    }
    void restOfLine() {
        for(;;) {
            while(pos<end) if(buffer[pos++]=='\n') return;
            if(eof) return;
            refill();
        }
    }
};

// Run the same classic-locale extraction as the original loader, on a token
// already in RAM. Reusing the stream also preserves VC110 rounding behavior.
class NumberInput : private std::streambuf {
    std::istream stream;
public:
    NumberInput():stream(this) { stream.imbue(std::locale::classic()); }
    template<typename T> T read(const Word& word) {
        if(!word.size) throw std::runtime_error("invalid");
        char* p=const_cast<char*>(word.data); // The stream only reads the get area.
        setg(p,p,p+word.size);
        stream.clear();
        T value;
        if(!(stream>>value) || gptr()!=egptr() || !_finite(value)) throw std::runtime_error("invalid");
        return value;
    }
};

// STL exports repeat the same coordinate literals on adjacent facets. Keep
// a small, bounded per-read table; collisions just use the normal converter.
class Coordinates {
    struct Entry {
        char text[32];
        unsigned length;
        float value;
        Entry():length(0),value(0) {}
    };
    std::vector<Entry> entries;
    NumberInput& numbers;
public:
    explicit Coordinates(NumberInput& input):entries(8192),numbers(input) {}
    float get(const Word& word) {
        if(!word.size || word.size>32) return numbers.read<float>(word);
        unsigned hash=unsigned(word.size);
        for(size_t i=0;i<word.size;++i) hash=hash*31+static_cast<unsigned char>(word.data[i]);
        Entry& entry=entries[(hash^(hash>>16)) & 8191];
        if(entry.length==word.size && std::memcmp(word.data,entry.text,word.size)==0) return entry.value;
        entry.value=numbers.read<float>(word);
        std::memcpy(entry.text,word.data,word.size); entry.length=unsigned(word.size);
        return entry.value;
    }
};
inline void validateNormal(const Word& word,NumberInput& numbers) {
    // Facet normals are computed from the vertices. Plain short decimals
    // necessarily fit in a double, so only their syntax needs checking.
    if(word.size && word.size<=36) {
        size_t i=(word.data[0]=='+' || word.data[0]=='-')?1:0;
        bool digit=false,dot=false;
        for(;i<word.size;++i) {
            char c=word.data[i];
            if(c>='0' && c<='9') digit=true;
            else if(c=='.' && !dot) dot=true;
            else break;
        }
        if(digit && i==word.size) return;
    }
    numbers.read<double>(word);
}

void ascii(std::ifstream& file,Progress& progress,Model& model,bool bom) {
    file.clear(); file.seekg(bom?3:0);
    TextInput input(file,progress,model.fileBytes);
    NumberInput numbers;
    Coordinates coordinates(numbers);
    Chunk chunk(ChunkTriangles);
    bool solid=false;
    for(;;) {
        Word word=input.word();
        if(!word.size) break;
        if(word.is("solid") || word.is("endsolid")) {
            solid=true; input.restOfLine(); continue;
        }
        if(!solid || !word.is("facet")) throw std::runtime_error("invalid");
        input.expect("normal");
        for(int k=0;k<3;++k) validateNormal(input.word(),numbers);
        input.expect("outer"); input.expect("loop");
        osg::Vec3 v[3];
        for(int j=0;j<3;++j) {
            input.expect("vertex");
            for(int k=0;k<3;++k) v[j][k]=coordinates.get(input.word());
        }
        input.expect("endloop"); input.expect("endfacet");
        chunk.append(v[0],v[1],v[2]);
        if(chunk.vertices->size()==ChunkTriangles*3) {
            check(progress); chunk.finish(model); chunk=Chunk(ChunkTriangles);
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
