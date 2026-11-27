#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "StlLoader.h"
#include <osg/Material>
#include <osg/Point>
#include <osg/NodeVisitor>
#include <osg/TriangleFunctor>
#include <osgDB/Registry>
#include <osgDB/Options>
#include <osgDB/fstream>
#include <algorithm>
#include <cstring>
#include <float.h>
#include <sstream>
#include <stdexcept>

namespace stlview {
namespace {
struct Canceled {};
void check(Progress& progress) { if(progress.canceled()) throw Canceled(); }

bool countLine(const std::string& line,const std::string& prefix,unsigned& count) {
    if(line.compare(0,prefix.size(),prefix)!=0) return false;
    std::string digits=line.substr(prefix.size());
    if(digits.empty() || digits.find_first_not_of("0123456789")!=std::string::npos) return false;
    std::istringstream input(digits);
    return (input>>count) && count>0 && count<=INT_MAX/3;
}

// A common export has fixed-width vertex records and a triangle list. Read
// complete blocks instead of asking the general PLY parser for each property.
// Any other header/layout goes through the original plugin.
osg::ref_ptr<osg::Node> readPackedMesh(const std::wstring& path,unsigned __int64 fileBytes,Progress& progress) {
    std::ifstream file(path.c_str(),std::ios::binary);
    std::vector<std::string> lines;
    char line[256];
    while(lines.size()<20 && file.getline(line,sizeof(line))) {
        std::string clean(line);
        if(!clean.empty() && clean.back()=='\r') clean.resize(clean.size()-1);
        lines.push_back(clean);
        if(clean=="end_header") break;
    }
    if(lines.size()<16 || lines[0]!="ply" || lines[1]!="format binary_little_endian 1.0") return NULL;
    unsigned vertexCount=0,faceCount=0;
    if(!countLine(lines[2],"element vertex ",vertexCount)) return NULL;
    const char* properties[]={"property float x","property float y","property float z","property uchar red",
        "property uchar green","property uchar blue","property uchar alpha",
        "property float nx","property float ny","property float nz"};
    // Keep the plugin's crease-normal generation for exports without normals.
    for(unsigned i=0;i<sizeof(properties)/sizeof(properties[0]);++i)
        if(lines[i+3]!=properties[i]) return NULL;
    size_t next=13;
    if(lines.size()<next+3 || !countLine(lines[next++],"element face ",faceCount)) return NULL;
    if(lines[next++]!="property list uchar uint vertex_indices") return NULL;
    const bool processId=lines[next]=="property uint process_id";
    if(processId) ++next;
    if(next+1!=lines.size() || lines[next]!="end_header") return NULL;
    const unsigned vertexStride=28,faceStride=processId?17:13;
    const unsigned __int64 headerBytes=static_cast<unsigned __int64>(file.tellg());
    if(headerBytes+static_cast<unsigned __int64>(vertexCount)*vertexStride+
       static_cast<unsigned __int64>(faceCount)*faceStride!=fileBytes) return NULL;
    check(progress);
    const unsigned block=16384;
    std::vector<unsigned char> buffer(block*std::max(vertexStride,faceStride));
    osg::ref_ptr<osg::Vec3Array> vertices=new osg::Vec3Array(vertexCount);
    osg::ref_ptr<osg::Vec4Array> colors=new osg::Vec4Array(vertexCount);
    osg::ref_ptr<osg::Vec3Array> normals=new osg::Vec3Array(vertexCount);
    for(unsigned first=0;first<vertexCount;first+=block) {
        check(progress);
        unsigned length=std::min(block,vertexCount-first);
        if(!file.read(reinterpret_cast<char*>(&buffer[0]),length*vertexStride)) throw std::runtime_error("truncated");
        for(unsigned i=0;i<length;++i) {
            const unsigned char* record=&buffer[i*vertexStride];
            osg::Vec3& v=(*vertices)[first+i];
            std::memcpy(v.ptr(),record,12);
            if(!_finite(v.x()) || !_finite(v.y()) || !_finite(v.z())) throw std::runtime_error("invalid");
            osg::Vec4& color=(*colors)[first+i];
            for(unsigned k=0;k<4;++k) color[k]=float(record[12+k]/255.0);
            std::memcpy((*normals)[first+i].ptr(),record+16,12);
        }
        InterlockedExchange(&progress.percent,LONG((headerBytes+static_cast<unsigned __int64>(first+length)*vertexStride)*90/fileBytes));
    }
    osg::ref_ptr<osg::DrawElementsUInt> faces=new osg::DrawElementsUInt(GL_TRIANGLES,faceCount*3);
    for(unsigned first=0;first<faceCount;first+=block) {
        check(progress);
        unsigned length=std::min(block,faceCount-first);
        if(!file.read(reinterpret_cast<char*>(&buffer[0]),length*faceStride)) throw std::runtime_error("truncated");
        for(unsigned i=0;i<length;++i) {
            const unsigned char* record=&buffer[i*faceStride];
            if(record[0]!=3) return NULL;
            unsigned* face=&(*faces)[(first+i)*3];
            std::memcpy(face,record+1,12);
            if(face[0]>=vertexCount || face[1]>=vertexCount || face[2]>=vertexCount) throw std::runtime_error("invalid");
        }
        InterlockedExchange(&progress.percent,LONG((fileBytes-static_cast<unsigned __int64>(faceCount-first-length)*faceStride)*90/fileBytes));
    }
    osg::ref_ptr<osg::Geometry> geometry=new osg::Geometry;
    geometry->setVertexArray(vertices);
    geometry->setColorArray(colors,osg::Array::BIND_PER_VERTEX);
    geometry->addPrimitiveSet(faces);
    check(progress);
    geometry->setNormalArray(normals,osg::Array::BIND_PER_VERTEX);
    osg::ref_ptr<osg::Geode> geode=new osg::Geode;
    geode->addDrawable(geometry);
    return geode;
}

bool encodePath(const std::wstring& path,std::string& result) {
#ifdef OSG_USE_UTF8_FILENAME
    UINT codepage=CP_UTF8;
#else
    UINT codepage=GetACP();
#endif
    BOOL substituted=FALSE;
    BOOL* usedDefault=codepage==CP_UTF8?NULL:&substituted;
    DWORD flags=codepage==CP_UTF8?0:WC_NO_BEST_FIT_CHARS;
    int length=WideCharToMultiByte(codepage,flags,path.c_str(),int(path.size()),NULL,0,NULL,usedDefault);
    if(!length || substituted) return false;
    result.assign(length,'\0');
    return WideCharToMultiByte(codepage,flags,path.c_str(),int(path.size()),&result[0],length,NULL,usedDefault)!=0 && !substituted;
}
std::string pluginPath(const std::wstring& path) {
    std::string result;
    if(encodePath(path,result)) return result;
    DWORD size=GetShortPathNameW(path.c_str(),NULL,0);
    if(size) {
        std::vector<wchar_t> shortPath(size);
        DWORD written=GetShortPathNameW(path.c_str(),&shortPath[0],size);
        if(written && written<size && encodePath(&shortPath[0],result)) return result;
    }
    throw std::runtime_error("path");
}
struct CountTriangles {
    unsigned __int64 count;
    Progress* progress;
    CountTriangles():count(0),progress(NULL) {}
    void operator()(const osg::Vec3&,const osg::Vec3&,const osg::Vec3&,bool) {
        ++count;
        if((count&1023)==0) check(*progress);
    }
};
class PreparePly : public osg::NodeVisitor {
    Model& model;
    Progress& progress;
public:
    PreparePly(Model& output,Progress& status)
        :osg::NodeVisitor(TRAVERSE_ALL_CHILDREN),model(output),progress(status) {}
    virtual void apply(osg::Geode& geode) {
        for(unsigned int i=0;i<geode.getNumDrawables();++i) {
            check(progress);
            osg::Geometry* geometry=dynamic_cast<osg::Geometry*>(geode.getDrawable(i));
            if(!geometry) continue;
            osg::TriangleFunctor<CountTriangles> counter;
            counter.progress=&progress;
            geometry->accept(counter);
            model.triangles+=counter.count;
            unsigned __int64 points=0;
            for(unsigned int j=0;j<geometry->getNumPrimitiveSets();++j) {
                osg::PrimitiveSet* primitive=geometry->getPrimitiveSet(j);
                if(primitive->getMode()==GL_POINTS) points+=primitive->getNumIndices();
            }
            model.points+=points;
            osg::StateSet* state=geometry->getOrCreateStateSet();
            if(geometry->getColorArray() && geometry->getColorArray()->getNumElements()) {
                osg::ref_ptr<osg::Material> material=new osg::Material;
                material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
                state->setAttributeAndModes(material,osg::StateAttribute::ON);
            }
            if(points && !counter.count) {
                state->setMode(GL_LIGHTING,osg::StateAttribute::OFF|osg::StateAttribute::OVERRIDE);
                state->setAttributeAndModes(new osg::Point(3.0f),osg::StateAttribute::ON);
            }
            geometry->setUseDisplayList(false);
            geometry->setUseVertexBufferObjects(true);
            geometry->setDataVariance(osg::Object::STATIC);
            geometry->getBound();
            model.chunks.push_back(geometry);
        }
        traverse(geode);
    }
};
}

bool readPly(const std::wstring& path,Progress& progress,Model& result,std::wstring& error) {
    result=Model(); error.clear();
    // This OSG version exposes only a filename-based synchronous PLY reader.
    // Keep it on the existing worker and show indeterminate progress honestly.
    InterlockedExchange(&progress.percent,-1);
    try {
        check(progress);
        WIN32_FILE_ATTRIBUTE_DATA data;
        if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)
            || (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("open");
        result.fileBytes=(static_cast<unsigned __int64>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
        if(!result.fileBytes) throw std::runtime_error("empty");
        result.node=readPackedMesh(path,result.fileBytes,progress);
        if(!result.node) {
            check(progress);
            InterlockedExchange(&progress.percent,-1);
            osgDB::ReaderWriter* reader=osgDB::Registry::instance()->getReaderWriterForExtension("ply");
            if(!reader) throw std::runtime_error("plugin");
            osg::ref_ptr<osgDB::Options> options=new osgDB::Options;
            options->setObjectCacheHint(osgDB::Options::CACHE_NONE);
            osgDB::ReaderWriter::ReadResult loaded=reader->readNode(pluginPath(path),options.get());
            check(progress); // Never publish a partial/canceled model.
            if(!loaded.validNode()) throw std::runtime_error("invalid");
            result.node=loaded.getNode();
        }
        InterlockedExchange(&progress.percent,95);
        PreparePly prepare(result,progress);
        result.node->accept(prepare);
        check(progress);
        if((!result.triangles && !result.points) || !result.node->getBound().valid()) throw std::runtime_error("empty");
        InterlockedExchange(&progress.percent,100);
        return true;
    } catch(const Canceled&) {
        error.clear();
    } catch(const std::bad_alloc&) {
        error=L"可用内存不足，无法继续读取这个 PLY 模型。";
    } catch(const std::exception& ex) {
        if(progress.canceled()) error.clear();
        else if(std::string(ex.what())=="open") error=L"无法打开文件，请检查路径和文件是否仍然存在。";
        else if(std::string(ex.what())=="plugin") error=L"PLY 读取插件不可用，请保留程序旁的 osgPlugins-3.4.0 文件夹。";
        else if(std::string(ex.what())=="path") error=L"PLY 路径无法被当前 OSG 版本识别，请移到其他目录后重试。";
        else error=L"PLY 文件不完整、格式不正确，或没有有效的网格或点云。";
    }
    result=Model(); return false;
}
}
