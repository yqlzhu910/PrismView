#pragma once
#include "StlLoader.h"
#include <osg/Point>
#include <osgDB/fstream>
#include <algorithm>
#include <cstring>
#include <float.h>
#include <locale>
#include <sstream>

namespace stlview {
namespace opening {
// A small, independent scene for the first frame. The full reader is always
// responsible for validating the file and publishing the final model.
struct Layout {
    unsigned stride, count, offsets[3], widths[3];
    int rgb[3];
    std::streamoff start;
    Layout():stride(0),count(0),start(0) {
        for(int i=0;i<3;++i) { offsets[i]=widths[i]=0; rgb[i]=-1; }
    }
};
inline unsigned scalarSize(const std::string& type) {
    if(type=="char" || type=="uchar" || type=="uint8" || type=="int8") return 1;
    if(type=="ushort" || type=="short" || type=="int16" || type=="uint16") return 2;
    if(type=="float" || type=="float32" || type=="int" || type=="uint" || type=="int32" || type=="uint32") return 4;
    if(type=="double" || type=="float64") return 8;
    return 0;
}
inline bool vertexLayout(std::ifstream& in,Layout& l) {
    bool little=false,vertex=false,first=true;
    char line[512];
    while(in.tellg()<32768 && in.getline(line,sizeof(line))) {
        std::istringstream words(line);
        std::string key,type,name; words>>key>>type>>name;
        if(first) { if(key!="ply") return false; first=false; continue; }
        if(key=="format") little=type=="binary_little_endian" && name=="1.0";
        if(key=="element") {
            vertex=type=="vertex";
            if(!l.count) {
                std::istringstream number(name);
                if(!vertex || !(number>>l.count) || !l.count) return false;
            } else if(vertex) return false;
        }
        if(key=="property" && vertex) {
            unsigned width=scalarSize(type);
            if(!width || l.stride+width>1024) return false;
            for(int axis=0;axis<3;++axis) {
                const char* color[]={"red","green","blue"};
                if(name.size()==1 && name[0]=="xyz"[axis]) {
                    if(type!="float" && type!="float32" && type!="double" && type!="float64") return false;
                    l.offsets[axis]=l.stride; l.widths[axis]=width;
                }
                if(name==color[axis] && (type=="uchar" || type=="uint8")) l.rgb[axis]=l.stride;
            }
            l.stride+=width;
        }
        if(key=="end_header") {
            l.start=in.tellg();
            return little && l.count && l.widths[0] && l.widths[1] && l.widths[2];
        }
    }
    return false;
}
inline void sampleBinary(std::ifstream& in,const Layout& l,unsigned __int64 bytes,Progress& progress,
                         osg::Vec3Array& points,osg::Vec4ubArray& colors) {
    if(l.start<0 || !l.count || !l.stride || l.start+static_cast<unsigned __int64>(l.count)*l.stride>bytes) return;
    const unsigned groups=std::min(256u,l.count);
    const unsigned rows=std::min(256u,l.count/groups);
    std::vector<char> block(rows*l.stride);
    for(unsigned n=0;n<groups;++n) {
        if(progress.canceled()) return;
        // Spread the samples across the file, including the final section.
        unsigned first=groups==1?0:unsigned(static_cast<unsigned __int64>(n)*(l.count-rows)/(groups-1));
        in.seekg(l.start+static_cast<std::streamoff>(first)*l.stride);
        if(!in.read(&block[0],block.size())) return;
        for(unsigned j=0;j<rows;++j) {
            const char* record=&block[j*l.stride];
            osg::Vec3 v;
            for(int axis=0;axis<3;++axis) {
                if(l.widths[axis]==4) std::memcpy(&v[axis],record+l.offsets[axis],4);
                else {double value; std::memcpy(&value,record+l.offsets[axis],8); v[axis]=float(value);}
            }
            if(!_finite(v.x()) || !_finite(v.y()) || !_finite(v.z())) continue;
            points.push_back(v);
            if(l.rgb[0]>=0 && l.rgb[1]>=0 && l.rgb[2]>=0) colors.push_back(osg::Vec4ub(
                (unsigned char)record[l.rgb[0]],(unsigned char)record[l.rgb[1]],(unsigned char)record[l.rgb[2]],255));
        }
    }
}
inline void sampleText(std::ifstream& in,unsigned __int64 bytes,bool obj,Progress& progress,osg::Vec3Array& points) {
    const size_t size=8192;
    char block[size+1];
    for(unsigned n=0;n<128;++n) {
        if(progress.canceled()) return;
        in.clear(); in.seekg(std::streamoff((bytes-size)*n/127));
        if(!in.read(block,size)) return;
        block[size]=0;
        char* row=block;
        if(n) { row=std::strchr(row,'\n'); if(!row) continue; ++row; }
        while(char* end=std::strchr(row,'\n')) {
            *end=0;
            row+=std::strspn(row," \t");
            const char* prefix=obj?"v":"vertex";
            size_t len=obj?1:6;
            if(std::strncmp(row,prefix,len)==0 && (row[len]==' ' || row[len]=='\t')) {
                osg::Vec3 v;
                std::istringstream xyz(row+len); xyz.imbue(std::locale::classic());
                if(xyz>>v.x()>>v.y()>>v.z() && _finite(v.x()) && _finite(v.y()) && _finite(v.z())) points.push_back(v);
            }
            row=end+1;
        }
    }
}
inline osg::ref_ptr<osg::Node> read(const std::wstring& path,unsigned __int64 bytes,Progress& progress) {
    if(bytes<16ULL*1024*1024 || progress.canceled()) return NULL;
    std::ifstream in(path.c_str(),std::ios::binary);
    if(!in) return NULL;
    const wchar_t* ext=path.c_str()+path.size()-4;
    osg::ref_ptr<osg::Vec3Array> points=new osg::Vec3Array;
    osg::ref_ptr<osg::Vec4ubArray> colors=new osg::Vec4ubArray;
    Layout l;
    if(!_wcsicmp(ext,L".ply")) {
        if(!vertexLayout(in,l)) return NULL;
        sampleBinary(in,l,bytes,progress,*points,*colors);
    } else if(!_wcsicmp(ext,L".stl")) {
        char header[84];
        if(!in.read(header,84)) return NULL;
        std::memcpy(&l.count,header+80,4); l.stride=50; l.start=84;
        if(84ULL+50ULL*l.count==bytes) {
            for(int i=0;i<3;++i) {l.offsets[i]=12+4*i; l.widths[i]=4;}
            sampleBinary(in,l,bytes,progress,*points,*colors);
        } else sampleText(in,bytes,false,progress,*points);
    } else sampleText(in,bytes,true,progress,*points);
    if(progress.canceled() || points->size()<3) return NULL;
    osg::ref_ptr<osg::Geometry> geometry=new osg::Geometry;
    geometry->setVertexArray(points);
    if(!colors->empty()) geometry->setColorArray(colors,osg::Array::BIND_PER_VERTEX);
    else {
        colors->push_back(osg::Vec4ub(183,203,221,255));
        geometry->setColorArray(colors,osg::Array::BIND_OVERALL);
    }
    geometry->addPrimitiveSet(new osg::DrawArrays(GL_POINTS,0,GLsizei(points->size())));
    osg::StateSet* state=geometry->getOrCreateStateSet();
    state->setMode(GL_LIGHTING,osg::StateAttribute::OFF|osg::StateAttribute::OVERRIDE);
    state->setAttributeAndModes(new osg::Point(2.5f),osg::StateAttribute::ON);
    geometry->setUseDisplayList(false); geometry->setUseVertexBufferObjects(true);
    osg::ref_ptr<osg::Geode> geode=new osg::Geode; geode->addDrawable(geometry);
    return geode;
}
}
}
