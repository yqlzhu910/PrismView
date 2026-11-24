#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "StlLoader.h"
#include <osg/NodeVisitor>
#include <osg/TriangleFunctor>
#include <osgDB/Registry>
#include <osgDB/Options>
#include <algorithm>
#include <fstream>
#include <locale>
#include <stdexcept>

namespace stlview {
namespace {
struct Canceled {};
void check(Progress& progress) { if(progress.canceled()) throw Canceled(); }

// The OBJ itself is always opened with a wide filename. OSG's dependent-file
// lookup follows the filename encoding used by this installed SDK.
std::string databasePath(const std::wstring& path) {
    size_t separator=path.find_last_of(L"\\/");
    std::wstring folder=separator==std::wstring::npos?L".":path.substr(0,separator+1);
#ifdef OSG_USE_UTF8_FILENAME
    UINT codepage=CP_UTF8;
#else
    UINT codepage=GetACP();
#endif
    BOOL substituted=FALSE;
    BOOL* usedDefault=codepage==CP_UTF8?NULL:&substituted;
    int length=WideCharToMultiByte(codepage,0,folder.c_str(),int(folder.size()),NULL,0,NULL,usedDefault);
    if(!length || substituted) throw std::runtime_error("path");
    std::string result(length,'\0');
    if(!WideCharToMultiByte(codepage,0,folder.c_str(),int(folder.size()),&result[0],length,NULL,usedDefault)
        || substituted) throw std::runtime_error("path");
    return result;
}

// Bound the extra input buffer and observe cancellation while the plugin parses.
// The final tessellation/material stage is owned by the plugin; cancel is checked
// again before any result can become the displayed scene.
class InputBuffer : public std::streambuf {
    std::ifstream& file;
    Progress& progress;
    unsigned __int64 total, consumed;
    char buffer[65536];
public:
    InputBuffer(std::ifstream& input,Progress& status,unsigned __int64 bytes)
        :file(input),progress(status),total(bytes),consumed(0) { setg(buffer,buffer,buffer); }
protected:
    virtual int_type underflow() {
        check(progress);
        file.read(buffer,sizeof(buffer));
        std::streamsize size=file.gcount();
        if(!size) {
            if(file.bad()) throw std::runtime_error("io");
            return traits_type::eof();
        }
        consumed+=size;
        InterlockedExchange(&progress.percent,total?LONG(std::min<unsigned __int64>(90,consumed*90/total)):0);
        setg(buffer,buffer,buffer+size);
        return traits_type::to_int_type(*gptr());
    }
};
struct CountTriangles {
    unsigned __int64 count;
    Progress* progress;
    CountTriangles():count(0),progress(NULL) {}
    void operator()(const osg::Vec3&,const osg::Vec3&,const osg::Vec3&,bool) {
        ++count;
        if((count&1023)==0) check(*progress);
    }
};
class PrepareGeometry : public osg::NodeVisitor {
    Model& model;
    Progress& progress;
public:
    PrepareGeometry(Model& output,Progress& status)
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

bool readObj(const std::wstring& path,Progress& progress,Model& result,std::wstring& error) {
    result=Model(); error.clear(); InterlockedExchange(&progress.percent,0);
    try {
        check(progress);
        std::ifstream file(path.c_str(),std::ios::in|std::ios::binary);
        if(!file) throw std::runtime_error("open");
        file.seekg(0,std::ios::end);
        std::streampos size=file.tellg();
        if(size<=0) throw std::runtime_error("empty");
        result.fileBytes=static_cast<unsigned __int64>(size);
        file.seekg(0);
        unsigned char bom[3]={0}; file.read(reinterpret_cast<char*>(bom),3); file.clear();
        file.seekg(bom[0]==0xef && bom[1]==0xbb && bom[2]==0xbf?3:0);
        osg::ref_ptr<osgDB::Options> options=new osgDB::Options("noRotation noTriStripPolygons generateFacetNormals");
        options->setDatabasePath(databasePath(path));
        options->setObjectCacheHint(osgDB::Options::CACHE_NONE);
        osgDB::ReaderWriter* reader=osgDB::Registry::instance()->getReaderWriterForExtension("obj");
        if(!reader) throw std::runtime_error("plugin");
        InputBuffer buffer(file,progress,result.fileBytes);
        std::istream input(&buffer);
        input.imbue(std::locale::classic());
        input.exceptions(std::ios::badbit);
        osgDB::ReaderWriter::ReadResult loaded=reader->readNode(input,options.get());
        check(progress);
        if(!loaded.validNode()) throw std::runtime_error("invalid");
        result.node=loaded.getNode();
        InterlockedExchange(&progress.percent,95);
        PrepareGeometry prepare(result,progress);
        result.node->accept(prepare);
        check(progress);
        if(!result.triangles || !result.node->getBound().valid()) throw std::runtime_error("empty");
        InterlockedExchange(&progress.percent,100);
        return true;
    } catch(const Canceled&) {
        error.clear();
    } catch(const std::bad_alloc&) {
        error=L"可用内存不足，无法继续读取这个 OBJ 模型。";
    } catch(const std::exception& ex) {
        if(progress.canceled()) error.clear();
        else if(std::string(ex.what())=="open") error=L"无法打开文件，请检查路径和文件是否仍然存在。";
        else if(std::string(ex.what())=="plugin") error=L"OBJ 读取插件不可用，请保留程序旁的 osgPlugins-3.4.0 文件夹。";
        else if(std::string(ex.what())=="path") error=L"OBJ 所在目录无法被当前 OSG 版本识别，请移到其他目录后重试。";
        else error=L"OBJ 文件不完整、格式不正确，或没有有效的多边形表面。";
    }
    result=Model(); return false;
}
}
