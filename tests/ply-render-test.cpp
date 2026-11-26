// Uses the production loading, GPU upload and display-mode code, with a pbuffer.
#include "STLQuickView.cpp"
#include <osgDB/WriteFile>
#include <iostream>

static int failures=0;
static void require(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;if(!ok)++failures;}
struct CapturePly : osg::Camera::DrawCallback {
    osg::ref_ptr<osg::Image> image;
    CapturePly():image(new osg::Image){}
    virtual void operator()(osg::RenderInfo&) const {image->readPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE);}
};
static bool finishLoad(){
    ULONGLONG start=GetTickCount64();
    while(job && GetTickCount64()-start<30000){pollLoad();Sleep(1);}
    return !job;
}
static osg::Image* draw(CapturePly* capture,const char* name,bool front=false){
    osg::BoundingSphere b=root->getBound();
    osg::Vec3d center(b.center());
    viewer->getCamera()->setViewMatrixAsLookAt(center+(front?osg::Vec3d(0,0,4):osg::Vec3d(2,-3,2))*b.radius(),center,front?osg::Vec3d(0,1,0):osg::Vec3d(0,0,1));
    viewer->frame();viewer->frame();
    require(osgDB::writeImageFile(*capture->image,name),name);
    return capture->image;
}
static unsigned colored(osg::Image* image,int channel){
    unsigned count=0;
    for(int y=0;y<480;++y)for(int x=0;x<640;++x){const unsigned char* p=image->data(x,y);count+=p[channel]>80 && p[channel]>p[(channel+1)%3]*2 && p[channel]>p[(channel+2)%3]*2;}
    return count;
}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    std::wstring base=argv[1];
    mainWindow=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,200,200,NULL,NULL,GetModuleHandleW(NULL),NULL);
    fileList=CreateWindowW(L"LISTBOX",L"",WS_CHILD,0,0,100,100,mainWindow,NULL,GetModuleHandleW(NULL),NULL);
    statusLabel=CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,100,100,mainWindow,NULL,GetModuleHandleW(NULL),NULL);
    folderLabel=statusLabel;uiFont=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for(int i=0;i<7;++i)buttons[i]=CreateWindowW(L"BUTTON",L"",WS_CHILD,0,0,100,30,mainWindow,NULL,GetModuleHandleW(NULL),NULL);
    osg::ref_ptr<osg::GraphicsContext::Traits> traits=new osg::GraphicsContext::Traits;
    traits->width=640;traits->height=480;traits->pbuffer=true;traits->doubleBuffer=false;traits->depth=24;
    graphics=osg::GraphicsContext::createGraphicsContext(traits);
    if(!graphics || !graphics->valid())return 3;
    viewer=new osgViewer::Viewer;viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    osg::Camera* camera=viewer->getCamera();camera->setGraphicsContext(graphics);
    camera->setViewport(0,0,640,480);camera->setDrawBuffer(GL_FRONT);camera->setReadBuffer(GL_FRONT);
    camera->setClearColor(osg::Vec4(.13f,.16f,.20f,1));camera->setProjectionMatrixAsPerspective(35,640.0/480,0.1,10000);
    manipulator=new osgGA::TrackballManipulator;
    initializeScene();viewer->setSceneData(root);
    osg::ref_ptr<CapturePly> capture=new CapturePly;camera->setFinalDrawCallback(capture);
    viewer->realize();if(!viewer->isRealized())return 4;
    graphics->makeCurrent();std::cout<<"RENDERER="<<glGetString(GL_RENDERER)<<std::endl;graphics->releaseContext();
    require(isModel(L"model.PLY") && isModel(L"model.stl") && isModel(L"model.obj") && !isModel(L"model.txt"),"supported file extensions");
    if(!loadFile(base+L"\\彩色四边形.PLY") || !finishLoad())return 5;
    require(currentTriangles==2 && !currentPoints && files.size()==1,"production worker + GPU upload + explicit history");
    osg::Image* image=draw(capture,"ply-color-solid.png",true);
    require(colored(image,0)>500 && colored(image,1)>500 && colored(image,2)>500,"PLY RGB visible in solid rendering");
    command(WireId);command(WireId);
    image=draw(capture,"ply-color-overlay.png",true);
    unsigned edges=0;for(int y=0;y<480;++y)for(int x=0;x<640;++x){const unsigned char* p=image->data(x,y);edges+=p[0]<25 && p[1]<25 && p[2]<25;}
    require(root->getEnabled() && edges>200 && colored(image,0)>500,"PLY solid + wireframe retains colors");
    if(!loadFile(base+L"\\点云.ply") || !finishLoad())return 6;
    wchar_t label[128]={0};GetWindowTextW(buttons[4],label,128);
    require(currentPoints==9 && !currentTriangles && !root->getEnabled() && !IsWindowEnabled(buttons[4]) && std::wstring(label)==L"点云","point-cloud UI and surface overlay disabled");
    GetWindowTextW(statusLabel,label,128);require(std::wstring(label).find(L"9 个点")!=std::wstring::npos,"point-cloud count in status");
    image=draw(capture,"ply-points.png",true);
    require(colored(image,0)>10 && colored(image,1)>10 && colored(image,2)>10,"point-cloud RGB visible without normals");
    command(WireId);require(displayMode==OverlayMode,"point-cloud W preserves previous mesh mode");
    std::wstring previous=currentPath;osg::ref_ptr<osg::Node> previousNode=root->getChild(0);
    loadFile(base+L"\\cancel-large.ply");command(CancelId);if(!finishLoad())return 7;
    require(currentPath==previous && root->getChild(0)==previousNode && files.size()==2,"cancellation retains current model and history");
    if(!loadFile(base+L"\\binary_big_endian.ply") || !finishLoad())return 8;
    require(currentTriangles==2 && !currentPoints && root->getEnabled() && IsWindowEnabled(buttons[4]),"mesh after point cloud restores overlay");
    if(argc>2){
        if(!loadFile(argv[2]) || !finishLoad())return 9;
        command(WireId);image=draw(capture,"ply-sample-solid.png");
        unsigned visible=0;for(int y=0;y<480;++y)for(int x=0;x<640;++x){const unsigned char* p=image->data(x,y);visible+=!(p[0]==33 && p[1]==41 && p[2]==51);}
        require(visible>1000 && currentTriangles>0,"user sample renders through production loader");
        command(WireId);command(WireId);draw(capture,"ply-sample-overlay.png");
    }
    viewer->setDone(true);viewer=NULL;graphics=NULL;root=NULL;polygonMode=NULL;manipulator=NULL;
    DestroyWindow(mainWindow);
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
