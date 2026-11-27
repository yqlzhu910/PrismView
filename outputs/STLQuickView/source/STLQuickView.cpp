#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <process.h>
#include "StlLoader.h"
#include "OpeningPreview.h"
#include <osg/Material>
#include <osg/PolygonMode>
#include <osg/LightModel>
#include <osg/RenderInfo>
#include <osgFX/Scribe>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/api/Win32/GraphicsWindowWin32>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

enum { OpenId=100, PrevId, NextId, HomeId, WireId, ListId, ReloadId, CancelId };
static HWND mainWindow, renderWindow, fileList, statusLabel, folderLabel, rendererLabel;
static HWND buttons[7];
static HFONT uiFont;
static osg::ref_ptr<osgViewer::Viewer> viewer;
static osg::ref_ptr<osg::GraphicsContext> graphics;
static osg::ref_ptr<osgGA::TrackballManipulator> manipulator;
static osg::ref_ptr<osgFX::Scribe> root;
static osg::ref_ptr<osg::PolygonMode> polygonMode;
static std::vector<std::wstring> files;
static std::wstring currentPath, currentFolder;
enum DisplayMode { SolidMode, WireMode, OverlayMode };
static DisplayMode displayMode=SolidMode;
static bool loading=false, closing=false;
static int uiDpi=96;
static int px(int value) { return MulDiv(value,uiDpi,96); }
struct FileStamp {
    unsigned __int64 bytes;
    FILETIME time;
    bool valid;
    FileStamp():bytes(0),valid(false) {time.dwHighDateTime=time.dwLowDateTime=0;}
    explicit FileStamp(const std::wstring& path):bytes(0),valid(false) {
        time.dwHighDateTime=time.dwLowDateTime=0;
        WIN32_FILE_ATTRIBUTE_DATA data;
        if(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data) && !(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            bytes=(static_cast<unsigned __int64>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
            time=data.ftLastWriteTime; valid=true;
        }
    }
    bool matches(const FileStamp& other) const {
        return valid && other.valid && bytes==other.bytes && CompareFileTime(&time,&other.time)==0;
    }
};
struct ReadyScene {
    std::wstring path;
    FileStamp stamp;
    osg::ref_ptr<osg::Node> node;
    unsigned __int64 triangles,points,estimatedBytes;
    ReadyScene():triangles(0),points(0),estimatedBytes(0) {}
    bool matches(const std::wstring& file,const FileStamp& disk) const {
        return node && _wcsicmp(path.c_str(),file.c_str())==0 && stamp.matches(disk);
    }
};
static ReadyScene activeScene,previousScene;
static bool canKeep(const ReadyScene& scene) {
    MEMORYSTATUSEX mem={sizeof(mem)};
    return scene.node && scene.stamp.valid && GlobalMemoryStatusEx(&mem) &&
        mem.ullAvailPhys>std::max(768ULL<<20,mem.ullTotalPhys/16) &&
        scene.estimatedBytes<std::min(6ULL<<30,mem.ullTotalPhys/4);
}
struct LoadJob {
    std::wstring path,error;
    stlview::Progress progress;
    stlview::Model model;
    HANDLE thread;
    size_t uploaded;
    FileStamp stamp;
    osg::ref_ptr<osg::Node> opening;
    volatile LONG openingReady;
    bool openingVisible;
    osg::Matrixd previousView,openingView;
    LoadJob(const std::wstring& value):path(value),thread(NULL),uploaded(0),stamp(value),openingReady(0),openingVisible(false) {}
};
static LoadJob* job=NULL;
static std::wstring pendingPath;
static bool pendingReload=false;
static ULONGLONG lastProgressTime=0;
static unsigned __int64 currentTriangles=0;
static unsigned __int64 currentPoints=0;
static std::wstring modelStatus;

static void applyDisplayMode();
static unsigned __stdcall readWorker(void* parameter) {
    LoadJob* work=static_cast<LoadJob*>(parameter);
    try {
        work->opening=stlview::opening::read(work->path,work->stamp.bytes,work->progress);
        if(work->opening) InterlockedExchange(&work->openingReady,1);
    } catch(const std::bad_alloc&) {} // The full reader can proceed without an opening preview.
    if(work->path.size()>4 && _wcsicmp(work->path.c_str()+work->path.size()-4,L".obj")==0)
        stlview::readObj(work->path,work->progress,work->model,work->error);
    else if(work->path.size()>4 && _wcsicmp(work->path.c_str()+work->path.size()-4,L".ply")==0)
        stlview::readPly(work->path,work->progress,work->model,work->error);
    else stlview::read(work->path,work->progress,work->model,work->error);
    return 0;
}

static std::wstring filename(const std::wstring& path) {
    size_t p=path.find_last_of(L"\\/");
    return p==std::wstring::npos ? path : path.substr(p+1);
}
static std::wstring directory(const std::wstring& path) {
    size_t p=path.find_last_of(L"\\/");
    return p==std::wstring::npos ? L"." : path.substr(0,p+1);
}
static bool isModel(const std::wstring& path) {
    return path.size()>4 && (_wcsicmp(path.c_str()+path.size()-4,L".stl")==0
        || _wcsicmp(path.c_str()+path.size()-4,L".obj")==0
        || _wcsicmp(path.c_str()+path.size()-4,L".ply")==0);
}
static int currentIndex() {
    for(size_t i=0;i<files.size();++i)
        if(_wcsicmp(files[i].c_str(),currentPath.c_str())==0) return int(i);
    return -1;
}
static void showStatus(const std::wstring& text) {
    SetWindowTextW(statusLabel,text.c_str());
}
static void updateFileList() {
    // History contains only successful explicit selections; never enumerate a folder.
    SendMessageW(fileList,WM_SETREDRAW,FALSE,0);
    SendMessageW(fileList,LB_RESETCONTENT,0,0);
    int extent=0;
    HDC dc=GetDC(fileList);
    HGDIOBJ previous=SelectObject(dc,uiFont);
    for(size_t i=0;i<files.size();++i) {
        std::wstring name=filename(files[i]);
        SendMessageW(fileList,LB_ADDSTRING,0,LPARAM(name.c_str()));
        SIZE size; GetTextExtentPoint32W(dc,name.c_str(),int(name.size()),&size);
        extent=std::max(extent,int(size.cx)+20);
    }
    SelectObject(dc,previous); ReleaseDC(fileList,dc);
    SendMessageW(fileList,LB_SETHORIZONTALEXTENT,extent,0);
    SendMessageW(fileList,LB_SETCURSEL,currentIndex(),0);
    SendMessageW(fileList,WM_SETREDRAW,TRUE,0);
    InvalidateRect(fileList,NULL,TRUE);
    SetWindowTextW(folderLabel,L"本次打开的文件\n只记录你主动选择的模型");
    EnableWindow(buttons[1],files.size()>1);
    EnableWindow(buttons[2],files.size()>1);
}
static void reportError(const std::wstring& text) {
    showStatus(L"打开失败，当前模型保持不变。");
    MessageBoxW(mainWindow,text.c_str(),L"棱镜 3D",MB_OK|MB_ICONWARNING);
    SendMessageW(fileList,LB_SETCURSEL,currentIndex(),0);
}
static void rememberAndDisplay(const ReadyScene& selected,bool keepView=false) {
    ReadyScene scene=selected; // selected may refer to previousScene, which is about to change.
    if(scene.node!=activeScene.node) {
        if(_wcsicmp(activeScene.path.c_str(),scene.path.c_str())!=0 && canKeep(activeScene)) previousScene=activeScene;
        else previousScene=ReadyScene();
        activeScene=scene;
    }
    root->removeChildren(0,root->getNumChildren());
    root->addChild(scene.node);
    currentPath=scene.path;
    currentTriangles=scene.triangles;
    currentPoints=scene.points;
    currentFolder=directory(currentPath);
    if(currentIndex()<0) files.push_back(currentPath);
    updateFileList();
    manipulator->setNode(root);
    if(!keepView) manipulator->home(0.0);
    viewer->requestRedraw();
    SetWindowTextW(mainWindow,(filename(currentPath)+L" — 棱镜 3D").c_str());
    std::wostringstream message;
    if(currentTriangles) message << L"完整模型：" << currentTriangles << L" 个三角形";
    if(currentPoints) message << (currentTriangles?L"，":L"点云：") << currentPoints << L" 个点";
    message << L"    |    " << filename(currentPath)
            << L"    |    左键旋转 · 滚轮缩放 · 空格复位";
    modelStatus=message.str();
    showStatus(modelStatus);
    applyDisplayMode();
    EnableWindow(buttons[3],TRUE); EnableWindow(buttons[4],currentTriangles>0);
    EnableWindow(buttons[5],TRUE);
}
static void restoreOpening() {
    if(!job || !job->openingVisible) return;
    root->removeChildren(0,root->getNumChildren());
    if(activeScene.node) root->addChild(activeScene.node);
    applyDisplayMode();
    manipulator->setByMatrix(job->previousView);
    job->openingVisible=false;
    viewer->requestRedraw();
}
static void displayOpening() {
    job->previousView=manipulator->getMatrix();
    root->removeChildren(0,root->getNumChildren());
    root->setEnabled(false);
    root->addChild(job->opening);
    manipulator->setNode(root); manipulator->home(0.0);
    job->openingView=manipulator->getMatrix();
    job->openingVisible=true;
    showStatus(L"正在读取完整模型，当前显示抽样点："+filename(job->path));
    viewer->requestRedraw();
}

static void startLoad(const std::wstring& path) {
    job=new LoadJob(path);
    job->thread=reinterpret_cast<HANDLE>(_beginthreadex(NULL,0,readWorker,job,0,NULL));
    if(!job->thread) { delete job; job=NULL; reportError(L"无法启动读取线程，请稍后重试。"); return; }
    loading=true; lastProgressTime=0;
    EnableWindow(buttons[6],TRUE);
    showStatus(L"正在后台读取："+filename(path)+L"    可以取消，或选择其他文件。");
}
static bool loadFile(const std::wstring& requested,bool forceReload=false) {
    if(closing) return false;
    wchar_t full[32768];
    DWORD count=GetFullPathNameW(requested.c_str(),32768,full,NULL);
    if(!count || count>=32768 || !isModel(full)) {
        reportError(L"请选择 .stl、.obj 或 .ply 文件。"); return false;
    }
    FileStamp disk(full);
    const ReadyScene* hit=NULL;
    if(!forceReload) {
        if(activeScene.matches(full,disk)) hit=&activeScene;
        else if(previousScene.matches(full,disk)) hit=&previousScene;
    }
    if(hit) {
        if(job) {
            InterlockedExchange(&job->progress.cancel,1);
            restoreOpening();
            pendingPath.clear();
        }
        rememberAndDisplay(*hit);
    } else if(job) {
        if(!forceReload && !job->progress.canceled() && _wcsicmp(job->path.c_str(),full)==0) return true;
        pendingPath=full; pendingReload=forceReload;
        InterlockedExchange(&job->progress.cancel,1);
        restoreOpening();
        showStatus(L"正在切换到："+filename(pendingPath));
    } else {
        previousScene=ReadyScene(); // Make room before parsing a third model.
        startLoad(full);
    }
    return true;
}
static void discardJob() {
    // Called only after the worker has exited. No background access remains.
    if(job->thread) CloseHandle(job->thread);
    delete job; job=NULL; loading=false;
    EnableWindow(buttons[6],FALSE);
}
static void pollLoad() {
    if(!job) return;
    if(!job->openingVisible && !closing && !job->progress.canceled() && InterlockedCompareExchange(&job->openingReady,0,0))
        displayOpening();
    if(job->thread && WaitForSingleObject(job->thread,0)!=WAIT_OBJECT_0) {
        if(!job->progress.canceled() && GetTickCount64()-lastProgressTime>100) {
            LONG percent=InterlockedCompareExchange(&job->progress.percent,0,0);
            std::wostringstream text; text << (job->openingVisible?L"抽样点预览 · 正在读取完整模型 ":L"正在读取 ");
            if(percent>=0) text << percent << L"%    ";
            text << filename(job->path) << L"    |    可取消 / 选择其他文件";
            showStatus(text.str()); lastProgressTime=GetTickCount64();
        }
        return;
    }
    if(job->thread) { CloseHandle(job->thread); job->thread=NULL; }
    if(job->progress.canceled() || closing || !job->error.empty() || !job->model.node) {
        restoreOpening();
        std::wstring error=job->progress.canceled()?std::wstring():job->error;
        discardJob();
        if(!closing && !pendingPath.empty()) {
            std::wstring next=pendingPath; pendingPath.clear(); loadFile(next,pendingReload);
        } else if(!closing) {
            SendMessageW(fileList,LB_SETCURSEL,currentIndex(),0);
            if(!error.empty()) reportError(error);
            else showStatus(activeScene.node?modelStatus:L"已取消读取。");
        }
        return;
    }
    // Same thread/context as viewer->frame(). Prepare one drawable per iteration.
    // STL drawables are bounded to 4.5 MiB; OBJ/PLY keep the plugin's geometry.
    if(job->uploaded<job->model.chunks.size()) {
        if(!graphics->makeCurrent()) { job->error=L"无法准备 OpenGL 显示数据。"; return; }
        while(glGetError()!=GL_NO_ERROR) {}
        osg::RenderInfo info(graphics->getState(),viewer.get());
        job->model.chunks[job->uploaded]->compileGLObjects(info);
        GLenum error=glGetError();
        graphics->getState()->dirtyAllVertexArrays();
        graphics->releaseContext();
        if(error!=GL_NO_ERROR) { job->error=L"显卡无法容纳或处理这个模型的显示数据。当前模型保持不变。"; return; }
        ++job->uploaded;
        if(GetTickCount64()-lastProgressTime>100) {
            std::wostringstream text; text << L"正在准备显示 " << job->uploaded*100/job->model.chunks.size()
                << L"%    " << filename(job->path) << L"    |    完整模型，可取消";
            showStatus(text.str()); lastProgressTime=GetTickCount64();
        }
        return;
    }
    ReadyScene next;
    next.node=job->model.node; next.path=job->path;
    next.triangles=job->model.triangles; next.points=job->model.points;
    if(job->stamp.matches(FileStamp(next.path))) next.stamp=job->stamp;
    // Approximate CPU arrays and GPU copies, with room for driver overhead.
    // Inspect the actual arrays: ASCII file size is a poor estimate for STL.
    unsigned __int64 arrays=0;
    for(size_t i=0;i<job->model.chunks.size();++i) {
        osg::Geometry& geometry=*job->model.chunks[i];
        osg::Geometry::ArrayList list;
        geometry.getArrayList(list);
        for(size_t j=0;j<list.size();++j) if(list[j]) arrays+=list[j]->getTotalDataSize();
        for(unsigned j=0;j<geometry.getNumPrimitiveSets();++j) arrays+=geometry.getPrimitiveSet(j)->getTotalDataSize();
    }
    next.estimatedBytes=arrays*2+(32ULL<<20);
    bool keepView=job->openingVisible && manipulator->getMatrix()!=job->openingView;
    discardJob();
    rememberAndDisplay(next,keepView);
}
static void openDialog() {
    wchar_t path[32768]={0};
    OPENFILENAMEW dialog={sizeof(dialog)};
    dialog.hwndOwner=mainWindow;
    dialog.lpstrFilter=L"支持的模型 (*.stl;*.obj;*.ply)\0*.stl;*.obj;*.ply\0STL 模型 (*.stl)\0*.stl\0OBJ 模型 (*.obj)\0*.obj\0PLY 模型 (*.ply)\0*.ply\0\0";
    dialog.lpstrFile=path; dialog.nMaxFile=32768;
    dialog.lpstrTitle=L"选择 STL / OBJ / PLY 模型";
    dialog.lpstrInitialDir=currentFolder.empty()?NULL:currentFolder.c_str();
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    if(GetOpenFileNameW(&dialog)) loadFile(path);
}
static void stepFile(int direction) {
    if(files.size()<2) return;
    int index=(currentIndex()+direction+int(files.size()))%int(files.size());
    std::wstring path=files[index];
    loadFile(path);
}
static void applyDisplayMode() {
    const bool pointCloud=currentPoints>0 && currentTriangles==0;
    const bool overlay=displayMode==OverlayMode && !pointCloud;
    root->setEnabled(overlay);
    polygonMode->setMode(osg::PolygonMode::FRONT_AND_BACK,
        displayMode==WireMode && !pointCloud?osg::PolygonMode::LINE:osg::PolygonMode::FILL);
    // The overlay's second pass must be able to select LINE itself.
    root->getOrCreateStateSet()->setAttributeAndModes(polygonMode,
        overlay?osg::StateAttribute::ON:(osg::StateAttribute::ON|osg::StateAttribute::OVERRIDE));
    const wchar_t* labels[]={L"实体 (W)",L"线框 (W)",L"实体+线框"};
    SetWindowTextW(buttons[4],pointCloud?L"点云":labels[displayMode]);
}
static void command(int id) {
    switch(id) {
    case OpenId: openDialog(); break;
    case PrevId: stepFile(-1); break;
    case NextId: stepFile(1); break;
    case HomeId: if(manipulator) manipulator->home(0.0); break;
    case ReloadId: if(!currentPath.empty()) loadFile(currentPath,true); break;
    case CancelId:
        pendingPath.clear();
        if(job) {
            InterlockedExchange(&job->progress.cancel,1);
            restoreOpening();
            showStatus(L"正在取消读取……");
        }
        break;
    case WireId:
        if(currentPoints>0 && !currentTriangles) break;
        displayMode=DisplayMode((displayMode+1)%3);
        applyDisplayMode();
        if(job && job->openingVisible) root->setEnabled(false);
        break;
    }
    if(viewer) viewer->requestRedraw();
}
static HWND control(const wchar_t* kind,const wchar_t* text,DWORD style,int id) {
    HWND handle=CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,
        0,0,1,1,mainWindow,HMENU(INT_PTR(id)),GetModuleHandleW(NULL),NULL);
    SendMessageW(handle,WM_SETFONT,WPARAM(uiFont),TRUE);
    return handle;
}
static void layout() {
    RECT r; GetClientRect(mainWindow,&r);
    int width=r.right, height=r.bottom;
    int sidebar=px(300), toolbar=px(48), footer=px(58);
    for(int i=0;i<7;++i) MoveWindow(buttons[i],px(12+i*108),px(10),px(100),px(30),TRUE);
    MoveWindow(folderLabel,px(12),toolbar+px(8),sidebar-px(24),px(42),TRUE);
    MoveWindow(fileList,px(12),toolbar+px(54),sidebar-px(24),std::max(1,height-toolbar-footer-px(66)),TRUE);
    MoveWindow(statusLabel,px(12),height-footer+px(5),std::max(1,width-px(24)),px(24),TRUE);
    MoveWindow(rendererLabel,px(12),height-footer+px(29),std::max(1,width-px(24)),px(24),TRUE);
    int vw=std::max(1,width-sidebar-px(12)), vh=std::max(1,height-toolbar-footer);
    MoveWindow(renderWindow,sidebar,toolbar,vw,vh,TRUE);
    if(graphics && viewer) {
        graphics->resized(0,0,vw,vh);
        viewer->getCamera()->setViewport(0,0,vw,vh);
        viewer->getCamera()->setProjectionMatrixAsPerspective(35.0,double(vw)/vh,0.01,100000.0);
        viewer->requestRedraw();
    }
}
static LRESULT CALLBACK windowProcedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_SIZE: if(renderWindow) layout(); return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.x=px(800);
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.y=px(480); return 0;
    case WM_COMMAND:
        if(LOWORD(wParam)==ListId && HIWORD(wParam)==LBN_SELCHANGE) {
            int index=int(SendMessageW(fileList,LB_GETCURSEL,0,0));
            if(index>=0 && index<int(files.size())) { std::wstring path=files[index]; loadFile(path); }
        } else command(LOWORD(wParam));
        return 0;
    case WM_DROPFILES: {
        HDROP drop=reinterpret_cast<HDROP>(wParam);
        wchar_t path[32768]={0}; DragQueryFileW(drop,0,path,32768); DragFinish(drop);
        loadFile(path); return 0;
    }
    case WM_CLOSE:
        closing=true; pendingPath.clear();
        if(job) InterlockedExchange(&job->progress.cancel,1);
        ShowWindow(hwnd,SW_HIDE); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
static void initializeScene() {
    root=new osgFX::Scribe;
    root->setWireframeColor(osg::Vec4(0.035f,0.045f,0.06f,1.0f));
    root->setWireframeLineWidth(1.0f);
    osg::StateSet* state=root->getOrCreateStateSet();
    osg::ref_ptr<osg::Material> material=new osg::Material;
    material->setDiffuse(osg::Material::FRONT_AND_BACK,osg::Vec4(0.75f,0.81f,0.88f,1));
    material->setAmbient(osg::Material::FRONT_AND_BACK,osg::Vec4(0.25f,0.28f,0.32f,1));
    material->setColorMode(osg::Material::OFF);
    // Default for STL/unmaterialed OBJ; let model materials/vertex colors win.
    state->setAttributeAndModes(material,osg::StateAttribute::ON);
    osg::ref_ptr<osg::LightModel> lightModel=new osg::LightModel;
    lightModel->setTwoSided(true);
    state->setAttributeAndModes(lightModel,osg::StateAttribute::ON);
    state->setMode(GL_LIGHTING,osg::StateAttribute::ON);
    state->setMode(GL_CULL_FACE,osg::StateAttribute::OFF|osg::StateAttribute::OVERRIDE);
    state->setMode(GL_NORMALIZE,osg::StateAttribute::ON);
    polygonMode=new osg::PolygonMode;
    applyDisplayMode();
}
static bool initializeViewer() {
    RECT r; GetClientRect(renderWindow,&r);
    osg::ref_ptr<osg::GraphicsContext::Traits> traits=new osg::GraphicsContext::Traits;
    traits->x=0; traits->y=0; traits->width=r.right; traits->height=r.bottom;
    traits->windowDecoration=false; traits->doubleBuffer=true; traits->vsync=true;
    traits->alpha=8; traits->depth=24;
    traits->inheritedWindowData=new osgViewer::GraphicsWindowWin32::WindowData(renderWindow);
    graphics=osg::GraphicsContext::createGraphicsContext(traits);
    if(!graphics || !graphics->valid()) return false;
    viewer=new osgViewer::Viewer;
    viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer->setRunFrameScheme(osgViewer::ViewerBase::ON_DEMAND);
    viewer->setKeyEventSetsDone(0);
    viewer->getCamera()->setGraphicsContext(graphics);
    viewer->getCamera()->setClearColor(osg::Vec4(0.13f,0.16f,0.20f,1.0f));
    viewer->getCamera()->setDrawBuffer(GL_BACK);
    viewer->getCamera()->setReadBuffer(GL_BACK);
    manipulator=new osgGA::TrackballManipulator;
    manipulator->setAllowThrow(false);
    viewer->setCameraManipulator(manipulator);
    initializeScene();
    viewer->setSceneData(root);
    layout(); viewer->realize();
    if(!viewer->isRealized() || !graphics->makeCurrent()) return false;
    const char* renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::string device=renderer?renderer:"Unknown";
    std::string glVersion=version?version:"Unknown";
    std::wstring detail=L"渲染设备："+std::wstring(device.begin(),device.end())
        +L"    |    OpenGL "+std::wstring(glVersion.begin(),glVersion.end());
    SetWindowTextW(rendererLabel,detail.c_str());
    graphics->releaseContext();
    return true;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDPIAware();
    HDC screen=GetDC(NULL); uiDpi=GetDeviceCaps(screen,LOGPIXELSX); ReleaseDC(NULL,screen);
    uiFont=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    WNDCLASSW wc={0}; wc.lpfnWndProc=windowProcedure; wc.hInstance=instance;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=HBRUSH(COLOR_BTNFACE+1);
    wc.lpszClassName=L"STLQuickViewMain"; wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    RegisterClassW(&wc);
    WNDCLASSW canvas={0}; canvas.style=CS_OWNDC; canvas.lpfnWndProc=DefWindowProcW;
    canvas.hInstance=instance; canvas.hCursor=LoadCursor(NULL,IDC_ARROW);
    canvas.lpszClassName=L"STLQuickViewCanvas"; RegisterClassW(&canvas);
    mainWindow=CreateWindowExW(WS_EX_ACCEPTFILES,wc.lpszClassName,L"棱镜 3D · PrismView",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,px(1180),px(800),NULL,NULL,instance,NULL);
    const wchar_t* labels[]={L"打开 (Ctrl+O)",L"上一个",L"下一个",L"视角复位",L"实体 (W)",L"重新读取",L"取消加载"};
    int ids[]={OpenId,PrevId,NextId,HomeId,WireId,ReloadId,CancelId};
    for(int i=0;i<7;++i) { buttons[i]=control(L"BUTTON",labels[i],BS_PUSHBUTTON|WS_TABSTOP,ids[i]); if(i>0) EnableWindow(buttons[i],FALSE); }
    folderLabel=control(L"STATIC",L"本次打开的文件\n只记录你主动选择的模型",SS_LEFT,0);
    fileList=control(L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_BORDER|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,ListId);
    statusLabel=control(L"STATIC",L"点击“打开”或拖入 STL / OBJ / PLY；不会自动扫描文件夹。大文件可取消加载。",SS_LEFT|SS_PATHELLIPSIS,0);
    rendererLabel=control(L"STATIC",L"正在检测渲染设备……",SS_LEFT|SS_ENDELLIPSIS,0);
    renderWindow=CreateWindowExW(0,canvas.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
        0,0,1,1,mainWindow,NULL,instance,NULL);
    layout(); ShowWindow(mainWindow,show); UpdateWindow(mainWindow);
    if(!initializeViewer()) {
        MessageBoxW(mainWindow,L"无法创建 OpenGL 查看窗口。",L"棱镜 3D",MB_OK|MB_ICONERROR); return 1;
    }
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1) loadFile(argv[1]);
    LocalFree(argv);
    ACCEL entries[]={ {FVIRTKEY|FCONTROL,'O',OpenId}, {FVIRTKEY,VK_PRIOR,PrevId},
        {FVIRTKEY,VK_NEXT,NextId}, {FVIRTKEY,VK_SPACE,HomeId}, {FVIRTKEY,'W',WireId}, {FVIRTKEY,VK_F5,ReloadId} };
    HACCEL accelerators=CreateAcceleratorTableW(entries,sizeof(entries)/sizeof(entries[0]));
    MSG message={0}; bool running=true;
    while(running) {
        while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT) { closing=true; pendingPath.clear(); if(job) InterlockedExchange(&job->progress.cancel,1); break; }
            if(!TranslateAcceleratorW(mainWindow,accelerators,&message)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        if(running) {
            pollLoad();
            if(closing && !job) { running=false; continue; }
            if(!closing && !IsIconic(mainWindow) && viewer->checkNeedToDoFrame()) viewer->frame();
            // Pump messages between uploads without imposing a timer tick per chunk.
            if(job && !job->thread) continue;
            HANDLE worker=job?job->thread:NULL;
            MsgWaitForMultipleObjectsEx(worker?1:0,worker?&worker:NULL,worker?20:10,
                QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
    }
    viewer->setDone(true); viewer=NULL; graphics=NULL; root=NULL; manipulator=NULL; polygonMode=NULL;
    DestroyAcceleratorTable(accelerators); DestroyWindow(mainWindow); DeleteObject(uiFont);
    return 0;
}
