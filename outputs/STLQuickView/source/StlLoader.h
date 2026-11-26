#pragma once
#include <windows.h>
#include <osg/Geometry>
#include <osg/Geode>
#include <string>
#include <vector>

namespace stlview {
struct Progress {
    volatile LONG cancel;
    volatile LONG percent;
    Progress():cancel(0),percent(0) {}
    bool canceled() { return InterlockedCompareExchange(&cancel,0,0)!=0; }
};
struct Model {
    osg::ref_ptr<osg::Node> node;
    std::vector<osg::ref_ptr<osg::Geometry> > chunks;
    unsigned __int64 triangles;
    unsigned __int64 points;
    unsigned __int64 fileBytes;
    Model():triangles(0),points(0),fileBytes(0) {}
};
// No GL calls or shared scene mutations. One worker owns the result until joined.
bool read(const std::wstring& path,Progress& progress,Model& result,std::wstring& error);
bool readObj(const std::wstring& path,Progress& progress,Model& result,std::wstring& error);
bool readPly(const std::wstring& path,Progress& progress,Model& result,std::wstring& error);
}
