#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "StlLoader.h"
#include <osg/Material>
#include <process.h>
#include <iostream>
#include <cmath>
static int failures=0;
static void require(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;if(!ok)++failures;}
static void verifyMesh(const stlview::Model& model){
    require(model.triangles==2 && model.points==0,"quad becomes two triangles");
    require(model.chunks.size()==1,"one mesh drawable");
    if(model.chunks.empty())return;
    osg::Geometry* g=model.chunks[0];
    osg::Vec3Array* vertices=dynamic_cast<osg::Vec3Array*>(g->getVertexArray());
    osg::Vec3Array* normals=dynamic_cast<osg::Vec3Array*>(g->getNormalArray());
    require(vertices && vertices->size()==4 && (*vertices)[2]==osg::Vec3(2,2,0),"vertices and axes preserved");
    require(normals && normals->size()==4 && (*normals)[0]==osg::Vec3(0,0,1),"input normals preserved");
    osg::Vec4Array* colors=dynamic_cast<osg::Vec4Array*>(g->getColorArray());
    require(colors && colors->size()==4 && (*colors)[0]==osg::Vec4(1,0,0,1),"input RGBA preserved");
    osg::Material* material=dynamic_cast<osg::Material*>(g->getStateSet()->getAttribute(osg::StateAttribute::MATERIAL));
    require(material && material->getColorMode()==osg::Material::AMBIENT_AND_DIFFUSE,"vertex colors enabled for rendering");
    require(!g->getUseDisplayList() && g->getUseVertexBufferObjects(),"VBO rendering enabled");
}
struct Run {std::wstring path,error;stlview::Progress progress;stlview::Model model;bool ok;};
static unsigned __stdcall load(void* p){Run* run=static_cast<Run*>(p);run->ok=stlview::readPly(run->path,run->progress,run->model,run->error);return 0;}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    std::wstring base=argv[1],error;stlview::Model model;stlview::Progress progress;
    const wchar_t* files[]={L"彩色四边形.PLY",L"binary_little_endian.ply",L"binary_big_endian.ply"};
    const char* labels[]={"ASCII + Chinese path + uppercase extension","little-endian binary + custom face property","big-endian binary"};
    for(int i=0;i<3;++i){bool ok=stlview::readPly(base+L"\\"+files[i],progress,model,error);require(ok,labels[i]);if(ok)verifyMesh(model);}
    bool ok=stlview::readPly(base+L"\\点云.ply",progress,model,error);
    require(ok && model.points==9 && model.triangles==0,"point-only PLY");
    if(ok && !model.chunks.empty())require(!(model.chunks[0]->getStateSet()->getMode(GL_LIGHTING)&osg::StateAttribute::ON),"point cloud lighting disabled");
    ok=stlview::readPly(base+L"\\plain-triangle.ply",progress,model,error);
    require(ok && model.triangles==1 && !model.points,"PLY mesh without colors or normals");
    if(ok && !model.chunks.empty()){
        osg::Vec3Array* generated=dynamic_cast<osg::Vec3Array*>(model.chunks[0]->getNormalArray());
        require(generated && !generated->empty() && (*generated)[0].z()>0.99f,"missing mesh normals generated");
    }
    require(!stlview::readPly(base+L"\\empty.ply",progress,model,error) && !model.node && !error.empty(),"empty PLY rejected");
    require(!stlview::readPly(base+L"\\invalid.ply",progress,model,error) && !model.node && !error.empty(),"invalid PLY rejected");
    require(!stlview::readPly(base+L"\\missing.ply",progress,model,error) && !model.node && !error.empty(),"missing PLY rejected");
    InterlockedExchange(&progress.cancel,1);
    require(!stlview::readPly(base+L"\\点云.ply",progress,model,error) && !model.node && error.empty(),"pre-canceled PLY discarded");
    InterlockedExchange(&progress.cancel,0);
    Run run;run.path=base+L"\\cancel-large.ply";run.ok=false;
    HANDLE thread=(HANDLE)_beginthreadex(NULL,0,load,&run,0,NULL);
    if(!thread)return 3;
    ULONGLONG begin=GetTickCount64();
    while(WaitForSingleObject(thread,1)==WAIT_TIMEOUT && InterlockedCompareExchange(&run.progress.percent,0,0)!=-1 && GetTickCount64()-begin<2000){}
    InterlockedExchange(&run.progress.cancel,1);
    DWORD joined=WaitForSingleObject(thread,10000);
    require(joined==WAIT_OBJECT_0 && !run.ok && !run.model.node && run.error.empty(),"in-flight PLY cancel discards result");
    CloseHandle(thread);if(joined!=WAIT_OBJECT_0)return 4;
    require(stlview::readPly(base+L"\\binary_little_endian.ply",progress,model,error),"PLY loads after failures and cancel");
    if(argc>2){
        bool sample=stlview::readPly(argv[2],progress,model,error);require(sample,"user-provided sample");
        unsigned __int64 vertices=0;
        for(size_t i=0;i<model.chunks.size();++i)vertices+=model.chunks[i]->getVertexArray()->getNumElements();
        std::cout<<"SAMPLE vertices="<<vertices<<" triangles="<<model.triangles<<" points="<<model.points<<std::endl;
    }
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
