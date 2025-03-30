// Client.cpp : 애플리케이션에 대한 진입점을 정의합니다.
//

#include "stdafx.h"
#include "Client.h"
#include "DDSTextureLoader12.h"

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
XMFLOAT4X4* mappedpointer{};
//XMFLOAT4X4 playerMatrix{};

ComPtr<ID3D12DescriptorHeap> ChessboardView{};
ComPtr<ID3D12Resource> ChessboardTexture{};
ComPtr<ID3D12Resource> BoardWorldMatrix{};

ComPtr<ID3D12DescriptorHeap> ChessPieceView{};
ComPtr<ID3D12Resource> ChessPieceTexture{};
//ComPtr<ID3D12Resource> PieceWorldMatrix{};  

bool bFirst = true;

// 서버 통신용 변수
SOCKET c_socket;
constexpr short SERVER_PORT = 6487;

char recv_buffer[1024];
WSABUF recv_wsabuf[1]{};
WSAOVERLAPPED recv_over;

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
void MakeTextureFromFile(const wchar_t* pszFileName, ComPtr<ID3D12Resource>& resource, ComPtr<ID3D12DescriptorHeap>& view, bool bDDS = true);
void InitRootSignature();
void InitPipelineState();

void Flush();
void Render();

void CALLBACK recv_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
//void CALLBACK send_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);

void do_recv();

struct MyUser {
    MyUser()
    {
        XMStoreFloat4x4(&_xmf4x4WorldMatrix, XMMatrixIdentity());
        _xmf4x4WorldMatrix._11 = _xmf4x4WorldMatrix._22 = 0.125f;
        _xmf4x4WorldMatrix._43 = -5.0f;
        _xmf4x4WorldMatrix._41 = _xmf4x4WorldMatrix._42 = 62.5;
        auto desc = BASIC_BUFFER_DESC;
        desc.Width = Align(sizeof(XMFLOAT4X4), 256);
        device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_worldBuffer.GetAddressOf()));
        _worldBuffer->Map(0, nullptr, reinterpret_cast<void**>(&_mappedptr));
    }

    ~MyUser()
    {
        _worldBuffer->Unmap(0, nullptr);
    }

    void Render() const
    {
        if (0 != state) {
            XMStoreFloat4x4(_mappedptr, XMMatrixTranspose(XMLoadFloat4x4(&_xmf4x4WorldMatrix)));
            cmdList->SetGraphicsRootConstantBufferView(1, _worldBuffer->GetGPUVirtualAddress());
            cmdList->DrawInstanced(6, 1, 0, 0);
        }
    }
    int state{};
    XMFLOAT4X4* _mappedptr;
    XMFLOAT4X4 _xmf4x4WorldMatrix;
    ComPtr<ID3D12Resource> _worldBuffer{};
};

std::vector<MyUser> g_users;

void print_error_message(int s_err)
{
    WCHAR* lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, s_err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    std::wcout << L" 에러 " << lpMsgBuf << std::endl;
    while (true); // 디버깅 용
    LocalFree(lpMsgBuf);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    WSADATA WSAData{};
    auto res = WSAStartup(MAKEWORD(2, 2), &WSAData);

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: 여기에 코드를 입력합니다.
    AllocConsole();
    FILE* newStdout, *newStdin, *newStderr;

    freopen_s(&newStdout, "CONOUT$", "w", stdout);
    freopen_s(&newStdin, "CONIN$", "r", stdin);
    freopen_s(&newStderr, "CONOUT$", "w", stderr);
    char IPAddress[20]{};
    std::cout << "Enter IP address: ";
    std::cin >> IPAddress;

    c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, IPAddress, &addr.sin_addr);
    auto ret = WSAConnect(c_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN), nullptr, nullptr, nullptr, nullptr);
    if (SOCKET_ERROR == ret) {
        std::cout << "Error at WSARecv : Error Code - ";
        auto s_err = WSAGetLastError();
        std::cout << s_err << std::endl;
        Sleep(2000);
        exit(-1);
    }

    recv_wsabuf[0].buf = recv_buffer;
    recv_wsabuf[0].len = sizeof(recv_buffer);

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
            if (msg.message == WM_QUIT) {
                FreeConsole();
                closesocket(c_socket);
                WSACleanup();
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Render();
    }
    FreeConsole();
    closesocket(c_socket);
    WSACleanup();
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
        switch (wParam) {
        case VK_UP: {
            char buffer[1]{ 'w' };
            WSABUF wsabuf[1]{};
            wsabuf[0].buf = buffer;
            wsabuf[0].len = 1;
            DWORD size_sent;
            WSASend(c_socket, wsabuf, 1, &size_sent, 0, nullptr, nullptr);
            break;
        }
        case VK_LEFT: {
            char buffer[1]{ 'a' };
            WSABUF wsabuf[1]{};
            wsabuf[0].buf = buffer;
            wsabuf[0].len = static_cast<ULONG>(strlen(buffer));
            DWORD size_sent;
            WSASend(c_socket, wsabuf, 1, &size_sent, 0, nullptr, nullptr);

            break;
        }
        case VK_DOWN: {
            char buffer[1]{ 's' };
            WSABUF wsabuf[1]{};
            wsabuf[0].buf = buffer;
            wsabuf[0].len = static_cast<ULONG>(strlen(buffer));
            DWORD size_sent;
            WSASend(c_socket, wsabuf, 1, &size_sent, 0, nullptr, nullptr);
            break;
        }
        case VK_RIGHT: {
            char buffer[1]{ 'd' };
            WSABUF wsabuf[1]{};
            wsabuf[0].buf = buffer;
            wsabuf[0].len = static_cast<ULONG>(strlen(buffer));
            DWORD size_sent;
            WSASend(c_socket, wsabuf, 1, &size_sent, 0, nullptr, nullptr);
            break;
        }
        }
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
    if (bFirst) {
        do_recv();
        bFirst = false;
    }
    SleepEx(0, TRUE);

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

    D3D12_CPU_DESCRIPTOR_HANDLE d3dCPUdsvHandle = DepthStencilView->GetCPUDescriptorHandleForHeapStart();
    cmdList->ClearDepthStencilView(d3dCPUdsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &d3dCPUHandle, TRUE, &d3dCPUdsvHandle);

    D3D12_VIEWPORT viewport{};
    viewport.Width = viewport.Height = BUFFER_WIDTH;
    viewport.MaxDepth = 1.0f;

    cmdList->RSSetViewports(1, &viewport);
    D3D12_RECT sRect = { 0, 0 ,1000, 1000 };
    cmdList->RSSetScissorRects(1, &sRect);


    // 렌더링 작업(Set & Draw) ===================
    cmdList->SetGraphicsRootSignature(RootSignature.Get());
    cmdList->SetGraphicsRootConstantBufferView(0, CameraMatrix->GetGPUVirtualAddress());
    cmdList->SetPipelineState(StandardPipeline.Get());
    
    // 체스 판
    cmdList->SetGraphicsRootConstantBufferView(1, BoardWorldMatrix->GetGPUVirtualAddress());
    cmdList->SetDescriptorHeaps(1, ChessboardView.GetAddressOf());
    cmdList->SetGraphicsRootDescriptorTable(2, ChessboardView->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW vview{};
    vview.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vview.SizeInBytes = sizeof(UVVertex) * 6;
    vview.StrideInBytes = sizeof(UVVertex);
    cmdList->IASetVertexBuffers(0, 1, &vview);
    cmdList->DrawInstanced(6, 1, 0, 0);


    cmdList->SetDescriptorHeaps(1, ChessPieceView.GetAddressOf());
    cmdList->SetGraphicsRootDescriptorTable(2, ChessPieceView->GetGPUDescriptorHandleForHeapStart());
    for (MyUser& u : g_users)
        u.Render();
    // ===========================================

    barrier(BackBuffer[nCurrentBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    cmdList->Close();
    cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(cmdList.GetAddressOf()));
    Flush();

    swapChain->Present(0, 0);
}

void InitScene()
{
    InitCameraMatrix();
    InitMesh();
    MakeTextureFromFile(L"ChessBoard.dds", ChessboardTexture, ChessboardView);
    MakeTextureFromFile(L"ChessPiece.dds", ChessPieceTexture, ChessPieceView);
    InitRootSignature();
    InitPipelineState();

    for(int i = 0 ; i < 10; ++i)
        g_users.emplace_back();

    /*XMStoreFloat4x4(&playerMatrix, XMMatrixIdentity());
    playerMatrix._11 = playerMatrix._22 = 0.125f;
    playerMatrix._43 = -5.0f;
    playerMatrix._41 = playerMatrix._42 = 62.5;*/
}

void InitCameraMatrix()
{
    auto desc = BASIC_BUFFER_DESC;
    desc.Width = Align(sizeof(XMFLOAT4X4), 256);
    device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(CameraMatrix.GetAddressOf()));
    device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(BoardWorldMatrix.GetAddressOf()));
    //device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(PieceWorldMatrix.GetAddressOf()));

    BoardWorldMatrix->Map(0, nullptr, (void**)&mappedpointer);
    XMStoreFloat4x4(mappedpointer, XMMatrixIdentity());
    BoardWorldMatrix->Unmap(0, nullptr);
    //PieceWorldMatrix->Map(0, nullptr, (void**)&mappedpointer);

    void* temp{};
    XMFLOAT3 eye = XMFLOAT3(0.0, 0.0, -10.0);
    XMFLOAT3 at = XMFLOAT3(0.0, 0.0, 1.0);
    XMFLOAT3 up = XMFLOAT3(0.0, 1.0, 0.0);
    XMMATRIX viewMat = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&at), XMLoadFloat3(&up));
    XMMATRIX ortho = XMMatrixOrthographicLH(BUFFER_WIDTH, BUFFER_HEIGHT, 0.0, 100.0f);

    viewMat = viewMat * ortho;
    CameraMatrix->Map(0, nullptr, &temp);
    XMStoreFloat4x4((XMFLOAT4X4*)temp, XMMatrixTranspose(viewMat));
    CameraMatrix->Unmap(0, nullptr);
}

void InitMesh()
{
    ComPtr<ID3D12Resource> UploadBuffer{};
    std::vector<UVVertex> pos{};
    pos.push_back(UVVertex(XMFLOAT3(-500.0, 500.0, 0.0), XMFLOAT2(0.0, 0.0)));
    pos.push_back(UVVertex(XMFLOAT3(500.0, 500.0, 0.0), XMFLOAT2(1.0, 0.0)));
    pos.push_back(UVVertex(XMFLOAT3(-500.0, -500.0, 0.0), XMFLOAT2(0.0, 1.0)));

    pos.push_back(UVVertex(XMFLOAT3(500.0, 500.0, 0.0), XMFLOAT2(1.0, 0.0)));
    pos.push_back(UVVertex(XMFLOAT3(500.0, -500.0, 0.0), XMFLOAT2(1.0, 1.0)));
    pos.push_back(UVVertex(XMFLOAT3(-500.0, -500.0, 0.0), XMFLOAT2(0.0, 1.0)));

    auto desc = BASIC_BUFFER_DESC;
    desc.Width = sizeof(UVVertex) * 6;
    device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(UploadBuffer.GetAddressOf()));
    device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(vertexBuffer.GetAddressOf()));
    void* temp{};
    UploadBuffer->Map(0, nullptr, &temp);
    memcpy(temp, pos.data(), sizeof(UVVertex) * 6);
    UploadBuffer->Unmap(0, nullptr);



    cmdAllocator->Reset();
    cmdList->Reset(cmdAllocator.Get(), nullptr);

    cmdList->CopyResource(vertexBuffer.Get(), UploadBuffer.Get());

    D3D12_RESOURCE_BARRIER br{};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = vertexBuffer.Get();
    br.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    br.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(1, &br);
    cmdList->Close();
    cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(cmdList.GetAddressOf()));
    Flush();

}
void MakeTextureFromFile(const wchar_t* pszFileName, ComPtr<ID3D12Resource>& resource, ComPtr<ID3D12DescriptorHeap>& view, bool bDDS)
{
    cmdAllocator->Reset();
    cmdList->Reset(cmdAllocator.Get(), nullptr);
    ComPtr<ID3D12Resource> pd3dUploadBuffer{};

    std::unique_ptr<uint8_t[]> decodedData;	// dds 데이터로도 사용
    std::vector<D3D12_SUBRESOURCE_DATA> vSubresources;
    DDS_ALPHA_MODE ddsAlphaMode = DDS_ALPHA_MODE_UNKNOWN;
    bool blsCubeMap = false;
    D3D12_SUBRESOURCE_DATA subResourceData;
    if (bDDS)
        LoadDDSTextureFromFileEx(device.Get(), pszFileName, 0, D3D12_RESOURCE_FLAG_NONE, DDS_LOADER_DEFAULT, resource.GetAddressOf(), decodedData, vSubresources, &ddsAlphaMode, &blsCubeMap);


    UINT64 nBytes{};
    UINT nSubResource = (UINT)vSubresources.size();
    if (bDDS) {
        nBytes = GetRequiredIntermediateSize(resource.Get(), 0, nSubResource);
    }
    auto desc = BASIC_BUFFER_DESC;
    desc.Width = nBytes;

    device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pd3dUploadBuffer));

    if (bDDS)
        ::UpdateSubresources(cmdList.Get() , resource.Get(), pd3dUploadBuffer.Get(), 0, 0, nSubResource, &vSubresources[0]);

    D3D12_RESOURCE_BARRIER d3dRB;
    ::ZeroMemory(&d3dRB, sizeof(D3D12_RESOURCE_BARRIER));
    d3dRB.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    d3dRB.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    d3dRB.Transition.pResource = resource.Get();
    d3dRB.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    d3dRB.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    d3dRB.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(1, &d3dRB);

    cmdList->Close();
    cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(cmdList.GetAddressOf()));
    Flush();

    D3D12_DESCRIPTOR_HEAP_DESC ddesc{};
    ddesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ddesc.NumDescriptors = 1;
    ddesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    device->CreateDescriptorHeap(&ddesc, IID_PPV_ARGS(view.GetAddressOf()));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    D3D12_RESOURCE_DESC d3dRD = resource->GetDesc();
    srvDesc.Format = d3dRD.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = -1;

    device->CreateShaderResourceView(resource.Get(), &srvDesc, view->GetCPUDescriptorHandleForHeapStart());
}
void InitRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &range;

    D3D12_STATIC_SAMPLER_DESC sdesc{};
    sdesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sdesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sdesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sdesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sdesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sdesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sdesc.MaxAnisotropy = 1;
    sdesc.MaxLOD = FLT_MAX;
    sdesc.MinLOD = -FLT_MAX;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = 3;
    desc.NumStaticSamplers = 1;
    desc.pParameters = params;
    desc.pStaticSamplers = &sdesc;

    ID3DBlob* blob{};
    D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, nullptr);
    device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(RootSignature.GetAddressOf()));
    blob->Release();
}
void InitPipelineState()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = RootSignature.Get();

    D3D12_INPUT_ELEMENT_DESC edesc[2]{};
    edesc[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    edesc[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    desc.InputLayout.NumElements = 2;
    desc.InputLayout.pInputElementDescs = edesc;

    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.DepthStencilState.StencilReadMask = 0x00;
    desc.DepthStencilState.StencilWriteMask = 0x00;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;

    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.AntialiasedLineEnable = FALSE;
    desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    desc.RasterizerState.DepthBias = 0;
    desc.RasterizerState.DepthBiasClamp = 0.0f;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.RasterizerState.ForcedSampleCount = 0;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.MultisampleEnable = FALSE;
    desc.RasterizerState.SlopeScaledDepthBias = 0.0f;

    desc.BlendState.AlphaToCoverageEnable = FALSE;
    desc.BlendState.IndependentBlendEnable = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;

    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.NumRenderTargets = 1;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    ID3DBlob* vsBlob{};
    D3DCompileFromFile(L"Shader.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", 0, 0, &vsBlob, nullptr);
    desc.VS.BytecodeLength = vsBlob->GetBufferSize();
    desc.VS.pShaderBytecode = vsBlob->GetBufferPointer();

    ID3DBlob* psBlob{};
    D3DCompileFromFile(L"Shader.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", 0, 0, &psBlob, nullptr);
    desc.PS.BytecodeLength = psBlob->GetBufferSize();
    desc.PS.pShaderBytecode = psBlob->GetBufferPointer();

    device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(StandardPipeline.GetAddressOf()));

    vsBlob->Release();
    psBlob->Release();
}


void do_recv()
{
    DWORD recv_flag{};
    ZeroMemory(&recv_over, sizeof(recv_over));
    int ret = WSARecv(c_socket, recv_wsabuf, 1, NULL, &recv_flag, &recv_over, recv_callback);
    if (0 != ret) {
        auto err_no = WSAGetLastError();
        if (WSA_IO_PENDING != err_no) {
            exit(-1);
        }
    }
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
    char* p = recv_buffer;
    while (p < recv_buffer + num_bytes) {
        int j = 0;
        for (int i = 0; i < 10; ++i) {
            long long id = static_cast<long long>(p[j]);
            g_users[i].state = static_cast<long long>(p[j + 1]);
            float pos[2]{};
            memcpy(pos, &p[j + 2], sizeof(float) * 2);
            g_users[i]._xmf4x4WorldMatrix._41 = pos[0]; g_users[i]._xmf4x4WorldMatrix._42 = pos[1];
            j += 10;
        }
        p += 100;
    }

    do_recv();
}
//void CALLBACK send_callback(DWORD err, DWORD , LPWSAOVERLAPPED, DWORD)
//{
//
//}