// Client.cpp : 애플리케이션에 대한 진입점을 정의합니다.
//

#include "stdafx.h"
#include "Client.h"

#define MAX_LOADSTRING 100
#define BUFFER_WIDTH 1000
#define BUFFER_HEIGHT 1000

// 전역 변수:
HINSTANCE hInst;                                // 현재 인스턴스입니다.
WCHAR szTitle[MAX_LOADSTRING];                  // 제목 표시줄 텍스트입니다.
WCHAR szWindowClass[MAX_LOADSTRING];            // 기본 창 클래스 이름입니다.
HWND g_HWND;

// d3d12 variables
ComPtr<IDXGIFactory4> factory{};

ComPtr<ID3D12Device5> device{};   
ComPtr<IDXGISwapChain3> swapChain{};

ComPtr<ID3D12CommandAllocator> cmdAllocator{};
ComPtr<ID3D12CommandQueue> cmdQueue{};
ComPtr<ID3D12GraphicsCommandList4> cmdList{};

ComPtr<ID3D12Fence> Fence{};

ComPtr<ID3D12Resource> BackBuffer[2]{};
ComPtr<ID3D12DescriptorHeap> RenderTargetView{};
UINT m_nRTVIncrementSize{};

ComPtr<ID3D12Resource> DepthStencilBuffer{};
ComPtr<ID3D12DescriptorHeap> DepthStencilView{};

ComPtr<ID3D12Resource> CameraMatrix{};

ComPtr<ID3D12RootSignature> RootSignature{};
ComPtr<ID3D12PipelineState> StandardPipeline{};

ComPtr<ID3D12Resource> vertexBuffer{};  // 정점 버퍼(사각형)
ComPtr<ID3D12Resource> WorldMatrix{};  
XMFLOAT4X4 playerMatrix{};

ComPtr<ID3D12Resource> ChessboardTexture{};
ComPtr<ID3D12Resource> ChessPieceTexture{};



// 이 코드 모듈에 포함된 함수의 선언을 전달합니다:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

// 필요 함수 선언
void PrepareApp();
void InitDevice();
void InitCommand();
void InitSwapChain();
void InitRTVDSV();

void InitScene();
void InitCameraMatrix();
void InitMesh();
void MakeTextureFromFile(const wchar_t* pszFileName, ComPtr<ID3D12Resource>& resource, bool bDDS = true);
void InitRootSignature();
void InitPipelineState();

void Flush();
void Render();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: 여기에 코드를 입력합니다.

    // 전역 문자열을 초기화합니다.
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_CLIENT, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 애플리케이션 초기화를 수행합니다:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_CLIENT));

    MSG msg;

    for (MSG msg;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Render();
    }
}



//
//  함수: MyRegisterClass()
//
//  용도: 창 클래스를 등록합니다.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CLIENT));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = nullptr;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   함수: InitInstance(HINSTANCE, int)
//
//   용도: 인스턴스 핸들을 저장하고 주 창을 만듭니다.
//
//   주석:
//
//        이 함수를 통해 인스턴스 핸들을 전역 변수에 저장하고
//        주 프로그램 창을 만든 다음 표시합니다.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 인스턴스 핸들을 전역 변수에 저장합니다.

   DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU | WS_BORDER;
   RECT rt = { 0, 0, BUFFER_WIDTH, BUFFER_HEIGHT };

   AdjustWindowRect(&rt, dwStyle, FALSE);

   g_HWND = CreateWindow(szWindowClass, szTitle, dwStyle,
       CW_USEDEFAULT, CW_USEDEFAULT, rt.right - rt.left, rt.bottom - rt.top, nullptr, nullptr, hInstance, nullptr);

   if (!g_HWND)
   {
       return FALSE;
   }

   PrepareApp();

   ShowWindow(g_HWND, nCmdShow);
   UpdateWindow(g_HWND);

   return TRUE;
}

//
//  함수: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  용도: 주 창의 메시지를 처리합니다.
//
//  WM_COMMAND  - 애플리케이션 메뉴를 처리합니다.
//  WM_PAINT    - 주 창을 그립니다.
//  WM_DESTROY  - 종료 메시지를 게시하고 반환합니다.
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_KEYDOWN:
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 정보 대화 상자의 메시지 처리기입니다.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void PrepareApp()
{
    InitDevice();
    InitCommand();
    InitSwapChain();
    InitRTVDSV();
    InitScene();
}
void InitDevice()
{
    CreateDXGIFactory(IID_PPV_ARGS(factory.GetAddressOf()));
    ComPtr<IDXGIAdapter> adapter{};

    D3D12CreateDevice(nullptr , D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));

    if (!device) {
        OutputDebugString(L"Device not Created. Create WrapAdapter\n");
        factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf()));
        D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));
    }

    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(Fence.GetAddressOf()));
}
void InitCommand()
{
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    device->CreateCommandQueue(&desc, IID_PPV_ARGS(cmdQueue.GetAddressOf()));
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdAllocator.GetAddressOf()));
    device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(cmdList.GetAddressOf()));
}
void InitSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = BUFFER_WIDTH;	// 바뀔 수 있다.
    desc.Height = BUFFER_HEIGHT;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = NO_AA;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* swapchain1{};
    factory->CreateSwapChainForHwnd(cmdQueue.Get(), g_HWND, &desc, nullptr, nullptr, &swapchain1);
    swapchain1->QueryInterface(swapChain.GetAddressOf());
    swapchain1->Release();

    if (!swapChain) {
        OutputDebugString(L"Not SwapChain Created\n");
        exit(0);
    }
}
void InitRTVDSV()
{
    // RTV
    D3D12_DESCRIPTOR_HEAP_DESC heapdesc{};
    heapdesc.NumDescriptors = 2;
    heapdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    device->CreateDescriptorHeap(&heapdesc, IID_PPV_ARGS(RenderTargetView.GetAddressOf()));

    m_nRTVIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle = RenderTargetView->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < 2; ++i) {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&BackBuffer[i]));
        device->CreateRenderTargetView(BackBuffer[i].Get(), nullptr, cpuDescriptorHandle);
        cpuDescriptorHandle.ptr += m_nRTVIncrementSize;
    }

    heapdesc.NumDescriptors = 1;
    heapdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    device->CreateDescriptorHeap(&heapdesc, IID_PPV_ARGS(DepthStencilView.GetAddressOf()));

    D3D12_RESOURCE_DESC resourceDesc = BASIC_BUFFER_DESC;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    resourceDesc.Width = BUFFER_WIDTH;		// 바뀔 수 있다
    resourceDesc.Height = BUFFER_HEIGHT;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr, IID_PPV_ARGS(&DepthStencilBuffer));

    cpuDescriptorHandle = DepthStencilView->GetCPUDescriptorHandleForHeapStart();
    device->CreateDepthStencilView(DepthStencilBuffer.Get(), nullptr, cpuDescriptorHandle);
}

void Flush()
{
    static UINT64 nFenceValue = 1;
    cmdQueue->Signal(Fence.Get(), nFenceValue);
    Fence->SetEventOnCompletion(nFenceValue++, nullptr);
}

void Render()
{
    auto barrier = [&](ID3D12Resource* pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER resBarrier{};
            resBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            resBarrier.Transition.pResource = pResource;
            resBarrier.Transition.StateBefore = before;
            resBarrier.Transition.StateAfter = after;
            resBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            cmdList->ResourceBarrier(1, &resBarrier);
        };

    cmdAllocator->Reset();
    cmdList->Reset(cmdAllocator.Get(), nullptr);

    UINT nCurrentBufferIndex = swapChain->GetCurrentBackBufferIndex();

    barrier(BackBuffer[nCurrentBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE d3dCPUHandle = RenderTargetView->GetCPUDescriptorHandleForHeapStart();
    d3dCPUHandle.ptr += (m_nRTVIncrementSize * nCurrentBufferIndex);
    float colors[] = { 0.5f, 0.5f, 1.0f, 1.0f };
    cmdList->ClearRenderTargetView(d3dCPUHandle, colors, 0, nullptr);

    d3dCPUHandle = DepthStencilView->GetCPUDescriptorHandleForHeapStart();
    cmdList->ClearDepthStencilView(d3dCPUHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // 렌더링 작업(Set & Draw) ===================

    // ===========================================

    barrier(BackBuffer[nCurrentBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    cmdList->Close();
    cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(cmdList.GetAddressOf()));
    Flush();

    swapChain->Present(0, 0);
}

void InitScene()
{

}

void InitCameraMatrix()
{

}

void InitMesh()
{
    
}
void MakeTextureFromFile(const wchar_t* pszFileName, ComPtr<ID3D12Resource>& resource, bool bDDS = true);
void InitRootSignature();
void InitPipelineState();