#include <iostream>
#include <Windows.h>
#include "Utils.h"
#include "d3d.h"
#include <dwmapi.h>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>
#include <list>
#include <filesystem>
#include "offsets.h"
#include "settings.h"
#include "icons.h"
#include "ImGui/imstb_truetype.h"
#include "ImGui/imstb_rectpack.h"
static HWND Window = NULL;
IDirect3D9Ex* pObject = NULL;
static LPDIRECT3DDEVICE9 D3DDevice = NULL;
static LPDIRECT3DVERTEXBUFFER9 VertBuff = NULL;

#define OFFSET_UWORLD 0x985c970
int localplayerID;

float Aim_Speed = 3.0f;
const int Max_Aim_Speed = 7;
const int Min_Aim_Speed = 0;


ImFont* m_pFont;


DWORD_PTR Uworld;
DWORD_PTR LocalPawn;
DWORD_PTR Localplayer;
DWORD_PTR Rootcomp;
DWORD_PTR PlayerController;
DWORD_PTR Ulevel;
DWORD_PTR entityx;
bool isaimbotting;



Vector3 localactorpos;
Vector3 Localcam;

static void WindowMain();
static void InitializeD3D();
static void Loop();
static void ShutDown();

FTransform GetBoneIndex(DWORD_PTR mesh, int index)
{
	DWORD_PTR bonearray = read<DWORD_PTR>(DrverInit, FNProcID, mesh + 0x4A8);  // 4A8  changed often 4u

	if (bonearray == NULL) // added 4u
	{
		bonearray = read<DWORD_PTR>(DrverInit, FNProcID, mesh + 0x4A8 + 0x10); // added 4u
	}

	return read<FTransform>(DrverInit, FNProcID, bonearray + (index * 0x30));  // doesn't change
}

Vector3 GetBoneWithRotation(DWORD_PTR mesh, int id)
{
	FTransform bone = GetBoneIndex(mesh, id);
	FTransform ComponentToWorld = read<FTransform>(DrverInit, FNProcID, mesh + 0x1C0);  // have never seen this change 4u

	D3DMATRIX Matrix;
	Matrix = MatrixMultiplication(bone.ToMatrixWithScale(), ComponentToWorld.ToMatrixWithScale());

	return Vector3(Matrix._41, Matrix._42, Matrix._43);
}

D3DMATRIX Matrix(Vector3 rot, Vector3 origin = Vector3(0, 0, 0))
{
	float radPitch = (rot.x * float(M_PI) / 180.f);
	float radYaw = (rot.y * float(M_PI) / 180.f);
	float radRoll = (rot.z * float(M_PI) / 180.f);

	float SP = sinf(radPitch);
	float CP = cosf(radPitch);
	float SY = sinf(radYaw);
	float CY = cosf(radYaw);
	float SR = sinf(radRoll);
	float CR = cosf(radRoll);

	D3DMATRIX matrix;
	matrix.m[0][0] = CP * CY;
	matrix.m[0][1] = CP * SY;
	matrix.m[0][2] = SP;
	matrix.m[0][3] = 0.f;

	matrix.m[1][0] = SR * SP * CY - CR * SY;
	matrix.m[1][1] = SR * SP * SY + CR * CY;
	matrix.m[1][2] = -SR * CP;
	matrix.m[1][3] = 0.f;

	matrix.m[2][0] = -(CR * SP * CY + SR * SY);
	matrix.m[2][1] = CY * SR - CR * SP * SY;
	matrix.m[2][2] = CR * CP;
	matrix.m[2][3] = 0.f;

	matrix.m[3][0] = origin.x;
	matrix.m[3][1] = origin.y;
	matrix.m[3][2] = origin.z;
	matrix.m[3][3] = 1.f;

	return matrix;
}

//4u note:  changes to projectw2s and camera are the most diffucult changes to understand reworking old camloc, be careful blindly making edits

extern Vector3 CameraEXT(0, 0, 0);
float FovAngle;
Vector3 ProjectWorldToScreen(Vector3 WorldLocation, Vector3 camrot)
{
	Vector3 Screenlocation = Vector3(0, 0, 0);
	Vector3 Camera;

	auto chain69 = read<uintptr_t>(DrverInit, FNProcID, Localplayer + 0xa8);
	uint64_t chain699 = read<uintptr_t>(DrverInit, FNProcID, chain69 + 8);

	Camera.x = read<float>(DrverInit, FNProcID, chain699 + 0x7F8);  //camera pitch  watch out for x and y swapped 4u
	Camera.y = read<float>(DrverInit, FNProcID, Rootcomp + 0x12C);  //camera yaw

	float test = asin(Camera.x);
	float degrees = test * (180.0 / M_PI);
	Camera.x = degrees;

	if (Camera.y < 0)
		Camera.y = 360 + Camera.y;

	D3DMATRIX tempMatrix = Matrix(Camera);
	Vector3 vAxisX, vAxisY, vAxisZ;

	vAxisX = Vector3(tempMatrix.m[0][0], tempMatrix.m[0][1], tempMatrix.m[0][2]);
	vAxisY = Vector3(tempMatrix.m[1][0], tempMatrix.m[1][1], tempMatrix.m[1][2]);
	vAxisZ = Vector3(tempMatrix.m[2][0], tempMatrix.m[2][1], tempMatrix.m[2][2]);

	uint64_t chain = read<uint64_t>(DrverInit, FNProcID, Localplayer + 0x70);
	uint64_t chain1 = read<uint64_t>(DrverInit, FNProcID, chain + 0x98);
	uint64_t chain2 = read<uint64_t>(DrverInit, FNProcID, chain1 + 0x130);

	Vector3 vDelta = WorldLocation - read<Vector3>(DrverInit, FNProcID, chain2 + 0x10); //camera location credits for Object9999
	Vector3 vTransformed = Vector3(vDelta.Dot(vAxisY), vDelta.Dot(vAxisZ), vDelta.Dot(vAxisX));

	if (vTransformed.z < 1.f)
		vTransformed.z = 1.f;

	float zoom = read<float>(DrverInit, FNProcID, chain699 + 0x590);

	FovAngle = 80.0f / (zoom / 1.19f);
	float ScreenCenterX = Width / 2.0f;
	float ScreenCenterY = Height / 2.0f;

	Screenlocation.x = ScreenCenterX + vTransformed.x * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	Screenlocation.y = ScreenCenterY - vTransformed.y * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	CameraEXT = Camera;

	return Screenlocation;
}


DWORD GUI(LPVOID in)
{
	while (1)
	{
		/*if (GetAsyncKeyState(VK_INSERT) & 1) {
			Settings::ShowMenu = !Settings::ShowMenu;
		}
		Sleep(2);*/
		


		/*if (GetAsyncKeyState(VK_INSERT) & 1) {
			Settings::ShowMenu = !Settings::ShowMenu;
		}*/


		static bool pressed = false;

		if (GetKeyState(VK_INSERT) & 0x8000)
			pressed = true;

		else if (!(GetKeyState(VK_INSERT) & 0x8000) && pressed) {
			Settings::ShowMenu = !Settings::ShowMenu;
			pressed = false;
		}
	}
}

typedef struct
{
	DWORD R;
	DWORD G;
	DWORD B;
	DWORD A;
}RGBA;

class Color
{
public:
	RGBA red = { 255,0,0,255 };
	RGBA Magenta = { 255,0,255,255 };
	RGBA yellow = { 255,255,0,255 };
	RGBA grayblue = { 128,128,255,255 };
	RGBA green = { 128,224,0,255 };
	RGBA darkgreen = { 0,224,128,255 };
	RGBA brown = { 192,96,0,255 };
	RGBA pink = { 255,168,255,255 };
	RGBA DarkYellow = { 216,216,0,255 };
	RGBA SilverWhite = { 236,236,236,255 };
	RGBA purple = { 144,0,255,255 };
	RGBA Navy = { 88,48,224,255 };
	RGBA skyblue = { 0,136,255,255 };
	RGBA graygreen = { 128,160,128,255 };
	RGBA blue = { 0,96,192,255 };
	RGBA orange = { 255,128,0,255 };
	RGBA peachred = { 255,80,128,255 };
	RGBA reds = { 255,128,192,255 };
	RGBA darkgray = { 96,96,96,255 };
	RGBA Navys = { 0,0,128,255 };
	RGBA darkgreens = { 0,128,0,255 };
	RGBA darkblue = { 0,128,128,255 };
	RGBA redbrown = { 128,0,0,255 };
	RGBA purplered = { 128,0,128,255 };
	RGBA greens = { 0,255,0,255 };
	RGBA envy = { 0,255,255,255 };
	RGBA black = { 0,0,0,255 };
	RGBA gray = { 128,128,128,255 };
	RGBA white = { 255,255,255,255 };
	RGBA blues = { 30,144,255,255 };
	RGBA lightblue = { 135,206,250,160 };
	RGBA Scarlet = { 220, 20, 60, 160 };
	RGBA white_ = { 255,255,255,200 };
	RGBA gray_ = { 128,128,128,200 };
	RGBA black_ = { 0,0,0,200 };
	RGBA red_ = { 255,0,0,200 };
	RGBA Magenta_ = { 255,0,255,200 };
	RGBA yellow_ = { 255,255,0,200 };
	RGBA grayblue_ = { 128,128,255,200 };
	RGBA green_ = { 128,224,0,200 };
	RGBA darkgreen_ = { 0,224,128,200 };
	RGBA brown_ = { 192,96,0,200 };
	RGBA pink_ = { 255,168,255,200 };
	RGBA darkyellow_ = { 216,216,0,200 };
	RGBA silverwhite_ = { 236,236,236,200 };
	RGBA purple_ = { 144,0,255,200 };
	RGBA Blue_ = { 88,48,224,200 };
	RGBA skyblue_ = { 0,136,255,200 };
	RGBA graygreen_ = { 128,160,128,200 };
	RGBA blue_ = { 0,96,192,200 };
	RGBA orange_ = { 255,128,0,200 };
	RGBA pinks_ = { 255,80,128,200 };
	RGBA Fuhong_ = { 255,128,192,200 };
	RGBA darkgray_ = { 96,96,96,200 };
	RGBA Navy_ = { 0,0,128,200 };
	RGBA darkgreens_ = { 0,128,0,200 };
	RGBA darkblue_ = { 0,128,128,200 };
	RGBA redbrown_ = { 128,0,0,200 };
	RGBA purplered_ = { 128,0,128,200 };
	RGBA greens_ = { 0,255,0,200 };
	RGBA envy_ = { 0,255,255,200 };

	RGBA glassblack = { 0, 0, 0, 160 };
	RGBA GlassBlue = { 65,105,225,80 };
	RGBA glassyellow = { 255,255,0,160 };
	RGBA glass = { 200,200,200,60 };


	RGBA Plum = { 221,160,221,160 };

};
Color Col;


std::string GetGNamesByObjID(int32_t ObjectID)
{
	return 0;
}


typedef struct _FNlEntity
{
	uint64_t Actor;
	int ID;
	uint64_t mesh;
}FNlEntity;

std::vector<FNlEntity> entityList;

#define DEBUG



HWND GameWnd = NULL;
void Window2Target()
{
	while (true)
	{
		if (hWnd)
		{
			ZeroMemory(&ProcessWH, sizeof(ProcessWH));
			GetWindowRect(hWnd, &ProcessWH);
			Width = ProcessWH.right - ProcessWH.left;
			Height = ProcessWH.bottom - ProcessWH.top;
			DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);

			if (dwStyle & WS_BORDER)
			{
				ProcessWH.top += 32;
				Height -= 39;
			}
			ScreenCenterX = Width / 2;
			ScreenCenterY = Height / 2;
			MoveWindow(Window, ProcessWH.left, ProcessWH.top, Width, Height, true);
		}
		else
		{
			exit(0);
		}
	}
}


const MARGINS Margin = { -1 };

/*
void WindowMain()
{
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Window2Target, 0, 0, 0);

	WNDCLASSEX ctx;
	ZeroMemory(&ctx, sizeof(ctx));
	ctx.cbSize = sizeof(ctx);
	ctx.lpszClassName = L"Voltic";
	ctx.lpfnWndProc = WindowProc;
	RegisterClassEx(&ctx);

	if (hWnd)
	{
		GetClientRect(hWnd, &ProcessWH);
		POINT xy;
		ClientToScreen(hWnd, &xy);
		ProcessWH.left = xy.x;
		ProcessWH.top = xy.y;

		Width = ProcessWH.right;
		Height = ProcessWH.bottom;
	}
	else
		exit(2);

	Window = CreateWindowEx(NULL, L"Voltic", L"Voltic", WS_POPUP | WS_VISIBLE, 0, 0, Width, Height, 0, 0, 0, 0);
	DwmExtendFrameIntoClientArea(Window, &Margin);
	SetWindowLong(Window, GWL_EXSTYLE, WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_LAYERED);
	ShowWindow(Window, SW_SHOW);
	UpdateWindow(Window);
}
*/

void WindowMain()
{

	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Window2Target, 0, 0, 0);
	WNDCLASSEX wClass =
	{
		sizeof(WNDCLASSEX),
		0,
		WindowProc,
		0,
		0,
		nullptr,
		LoadIcon(nullptr, IDI_APPLICATION),
		LoadCursor(nullptr, IDC_ARROW),
		nullptr,
		nullptr,
		TEXT("Test1"),
		LoadIcon(nullptr, IDI_APPLICATION)
	};

	if (!RegisterClassEx(&wClass))
		exit(1);

	hWnd = FindWindowW(NULL, TEXT("Fortnite  "));

	//printf("GameWnd Found! : %p\n", GameWnd);

	if (hWnd)
	{
		GetClientRect(hWnd, &ProcessWH);
		POINT xy;
		ClientToScreen(hWnd, &xy);
		ProcessWH.left = xy.x;
		ProcessWH.top = xy.y;


		Width = ProcessWH.right;
		Height = ProcessWH.bottom;
	}
	

	Window = CreateWindowExA(NULL, "Test1", "Test1", WS_POPUP | WS_VISIBLE, ProcessWH.left, ProcessWH.top, Width, Height, NULL, NULL, 0, NULL);
	DwmExtendFrameIntoClientArea(Window, &Margin);
	SetWindowLong(Window, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
	ShowWindow(Window, SW_SHOW);
	UpdateWindow(Window);

}

void InitializeD3D()
{
	if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &pObject)))
		exit(3);

	ZeroMemory(&d3d, sizeof(d3d));
	d3d.BackBufferWidth = Width;
	d3d.BackBufferHeight = Height;
	d3d.BackBufferFormat = D3DFMT_A8R8G8B8;
	d3d.MultiSampleQuality = D3DMULTISAMPLE_NONE;
	d3d.AutoDepthStencilFormat = D3DFMT_D16;
	d3d.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3d.EnableAutoDepthStencil = TRUE;
	d3d.hDeviceWindow = Window;
	d3d.Windowed = TRUE;

	pObject->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3d, &D3DDevice);

	IMGUI_CHECKVERSION();

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;

	ImGui_ImplWin32_Init(Window);
	ImGui_ImplDX9_Init(D3DDevice);




	pObject->Release();
}







/*void CalcRadarPoint(FVector vOrigin, int& screenx, int& screeny)
{
	FRotator vAngle = FRotator{ CamRot.x, CamRot.y, CamRot.z };
	auto fYaw = vAngle.Yaw * PI / 180.0f;
	float dx = vOrigin.X - CamLoc.x;
	float dy = vOrigin.Y - CamLoc.y;

	float fsin_yaw = sinf(fYaw);
	float fminus_cos_yaw = -cosf(fYaw);

	float x = dy * fminus_cos_yaw + dx * fsin_yaw;
	x = -x;
	float y = dx * fminus_cos_yaw - dy * fsin_yaw;

	float range = (float)Settings.RadarESPRange;

	RadarRange(&x, &y, range);

	ImVec2 DrawPos = ImGui::GetCursorScreenPos();
	ImVec2 DrawSize = ImGui::GetContentRegionAvail();

	int rad_x = (int)DrawPos.x;
	int rad_y = (int)DrawPos.y;

	float r_siz_x = DrawSize.x;
	float r_siz_y = DrawSize.y;

	int x_max = (int)r_siz_x + rad_x - 5;
	int y_max = (int)r_siz_y + rad_y - 5;

	screenx = rad_x + ((int)r_siz_x / 2 + int(x / range * r_siz_x));
	screeny = rad_y + ((int)r_siz_y / 2 + int(y / range * r_siz_y));

	if (screenx > x_max)
		screenx = x_max;

	if (screenx < rad_x)
		screenx = rad_x;

	if (screeny > y_max)
		screeny = y_max;

	if (screeny < rad_y)
		screeny = rad_y;
}*/


void renderRadar() {
	//or (auto pawn : radarPawns) {
	//	auto player = pawn;

	int screenx = 0;
	int screeny = 0;

	float Color_R = 255 / 255.f;
	float Color_G = 128 / 255.f;
	float Color_B = 0 / 255.f;

	//	FVector pos = *GetPawnRootLocation((PVOID)pawn);
	//CalcRadarPoint(pos, screenx, screeny);

	ImDrawList* Draw = ImGui::GetOverlayDrawList();

	//FVector viewPoint = { 0 };
	//if (IsTargetVisible(pawn)) {
	//	Color_R = 128 / 255.f;
	//	Color_G = 224 / 255.f;
	//	Color_B = 0 / 255.f;

	Draw->AddRectFilled(ImVec2((float)screenx, (float)screeny),
		ImVec2((float)screenx + 5, (float)screeny + 5),
		ImColor(Color_R, Color_G, Color_B));
}

bool firstS = false;
void RadarRange(float* x, float* y, float range)
{
	if (fabs((*x)) > range || fabs((*y)) > range)
	{
		if ((*y) > (*x))
		{
			if ((*y) > -(*x))
			{
				(*x) = range * (*x) / (*y);
				(*y) = range;
			}
			else
			{
				(*y) = -range * (*y) / (*x);
				(*x) = -range;
			}
		}
		else
		{
			if ((*y) > -(*x))
			{
				(*y) = range * (*y) / (*x);
				(*x) = range;
			}
			else
			{
				(*x) = -range * (*x) / (*y);
				(*y) = -range;
			}
		}
	}
}




void Radar()
{
	if (Settings::Radar) {
		ImGui::StyleColorsLight();

		ImGuiStyle* style = &ImGui::GetStyle();

		style->ChildBorderSize = 0.01;
		style->WindowMinSize = ImVec2(500, 600);
		style->WindowTitleAlign = ImVec2(0.5, 0.5);
		style->AntiAliasedFill = true;

		style->WindowRounding = 0;
		style->ChildRounding = 2;
		style->FrameRounding = 0;
		style->ScrollbarRounding = 0;
		style->TabRounding = 0;
		style->GrabRounding = 0;
		style->ScrollbarSize = 0.9;




		style->Colors[ImGuiCol_TitleBg] = ImColor(25, 25, 25, 230);
		style->Colors[ImGuiCol_TitleBgActive] = ImColor(25, 25, 25, 230);
		style->Colors[ImGuiCol_TitleBgCollapsed] = ImColor(25, 25, 25, 130);

		style->Colors[ImGuiCol_WindowBg] = ImColor(15, 15, 15, 230);
		style->Colors[ImGuiCol_CheckMark] = ImColor(255, 255, 255, 255);


		// C CA
		style->Colors[ImGuiCol_ChildBg] = ImColor(20, 20, 20, 255);
		style->Colors[ImGuiCol_Border] = ImColor(20, 20, 20, 255);




		style->Colors[ImGuiCol_FrameBg] = ImColor(25, 25, 25, 230);
		style->Colors[ImGuiCol_FrameBgActive] = ImColor(25, 25, 25, 230);
		style->Colors[ImGuiCol_FrameBgHovered] = ImColor(25, 25, 25, 230);

		style->Colors[ImGuiCol_Header] = ImColor(35, 35, 35, 230);
		style->Colors[ImGuiCol_HeaderActive] = ImColor(35, 35, 35, 230);
		style->Colors[ImGuiCol_HeaderHovered] = ImColor(35, 35, 35, 230);

		style->Colors[ImGuiCol_ResizeGrip] = ImColor(35, 35, 35, 255);
		style->Colors[ImGuiCol_ResizeGripActive] = ImColor(35, 35, 35, 255);
		style->Colors[ImGuiCol_ResizeGripHovered] = ImColor(35, 35, 35, 255);

		//    style->Colors[ImGuiCol_Button] = ImColor(37, 37, 37, 255);
		style->Colors[ImGuiCol_Button] = ImColor(37, 37, 37, 255);
		style->Colors[ImGuiCol_ButtonActive] = ImColor(41, 41, 41, 255);
		style->Colors[ImGuiCol_ButtonHovered] = ImColor(15, 15, 15, 230);

		style->Colors[ImGuiCol_SliderGrab] = ImColor(255, 255, 255, 255);
		style->Colors[ImGuiCol_SliderGrabActive] = ImColor(255, 255, 255, 255);


		ImGuiWindowFlags TargetFlags;
		if (Settings::ShowMenu)
			TargetFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
		else
			TargetFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

		//ImGui::SetNextWindowPosCenter(ImGuiCond_Once);
		if (!firstS) {
			ImGui::SetNextWindowPos(ImVec2{ - 220, 600 }, ImGuiCond_Once);
			firstS = true;
		}
		if (ImGui::Begin(""), 0, ImVec2(200, 200), -1.f, TargetFlags) {
			ImDrawList* Draw = ImGui::GetOverlayDrawList();
			ImVec2 DrawPos = ImGui::GetCursorScreenPos();
			ImVec2 DrawSize = ImGui::GetContentRegionAvail();

			ImVec2 midRadar = ImVec2(DrawPos.x + (DrawSize.x / 2), DrawPos.y + (DrawSize.y / 2));

			ImGui::GetWindowDrawList()->AddLine(ImVec2(midRadar.x - DrawSize.x / 2.f, midRadar.y), ImVec2(midRadar.x + DrawSize.x / 2.f, midRadar.y), IM_COL32(0, 0, 0, 50));
			ImGui::GetWindowDrawList()->AddLine(ImVec2(midRadar.x, midRadar.y - DrawSize.y / 2.f), ImVec2(midRadar.x, midRadar.y + DrawSize.y / 2.f), IM_COL32(0, 0, 0, 50));


		}
		ImGui::End();
	}
}

Vector3 Camera(unsigned __int64 RootComponent)
{
	unsigned __int64 PtrPitch;
	Vector3 Camera;

	auto pitch = read<uintptr_t>(DrverInit, FNProcID, Offsets::LocalPlayer + 0xb0);
	Camera.x = read<float>(DrverInit, FNProcID, RootComponent + 0x12C);
	Camera.y = read<float>(DrverInit, FNProcID, pitch + 0x678);

	float test = asin(Camera.y);
	float degrees = test * (180.0 / M_PI);

	Camera.y = degrees;

	if (Camera.x < 0)
		Camera.x = 360 + Camera.x;

	return Camera;
}

bool GetAimKey()
{
	return (GetAsyncKeyState(VK_RBUTTON));
}

void aimbot(float x, float y)
{
	float ScreenCenterX = (Width / 2);
	float ScreenCenterY = (Height / 2);
	int AimSpeed = Aim_Speed;
	float TargetX = 0;
	float TargetY = 0;

	if (x != 0)
	{
		if (x > ScreenCenterX)
		{
			TargetX = -(ScreenCenterX - x);
			TargetX /= AimSpeed;
			if (TargetX + ScreenCenterX > ScreenCenterX * 2) TargetX = 0;
		}

		if (x < ScreenCenterX)
		{
			TargetX = x - ScreenCenterX;
			TargetX /= AimSpeed;
			if (TargetX + ScreenCenterX < 0) TargetX = 0;
		}
	}

	if (y != 0)
	{
		if (y > ScreenCenterY)
		{
			TargetY = -(ScreenCenterY - y);
			TargetY /= AimSpeed;
			if (TargetY + ScreenCenterY > ScreenCenterY * 2) TargetY = 0;
		}

		if (y < ScreenCenterY)
		{
			TargetY = y - ScreenCenterY;
			TargetY /= AimSpeed;
			if (TargetY + ScreenCenterY < 0) TargetY = 0;
		}
	}

	mouse_event(MOUSEEVENTF_MOVE, static_cast<DWORD>(TargetX), static_cast<DWORD>(TargetY), NULL, NULL);

	return;
}

double GetCrossDistance(double x1, double y1, double x2, double y2)
{
	return sqrt(pow((x2 - x1), 2) + pow((y2 - y1), 2));
}

bool GetClosestPlayerToCrossHair(Vector3 Pos, float& max, float aimfov, DWORD_PTR entity)
{
	if (!GetAimKey() || !isaimbotting)
	{
		if (entity)
		{
			float Dist = GetCrossDistance(Pos.x, Pos.y, Width / 2, Height / 2);

			if (Dist < max)
			{
				max = Dist;
				entityx = entity;
				Settings::AimbotFOV = aimfov;
			}
		}
	}
	return false;
}



void AimAt(DWORD_PTR entity, Vector3 Localcam)
{

	
	{
		uint64_t currentactormesh = read<uint64_t>(DrverInit, FNProcID, entity + 0x280);
		auto rootHead = GetBoneWithRotation(currentactormesh, 66);
		Vector3 rootHeadOut = ProjectWorldToScreen(rootHead, Vector3(Localcam.y, Localcam.x, Localcam.z));
		Vector3 Headpos = GetBoneWithRotation(currentactormesh, 66);
		Vector3 HeadposW2s = ProjectWorldToScreen(Headpos, Vector3(Localcam.y, Localcam.x, Localcam.z));

		if (rootHeadOut.x != 0 || rootHeadOut.y != 0)
		{
			if ((GetCrossDistance(rootHeadOut.x, rootHeadOut.y, Width / 2, Height / 2) <= Settings::AimbotFOV * 8) || isaimbotting)
			{
				aimbot(rootHeadOut.x, rootHeadOut.y);
				//DrawString(_xor_("TRACKED").c_str(), 13, rootHeadOut.x, rootHeadOut.y - 0, 255, 255, 1);
				

			}
		}
	}


}

void aimbot(Vector3 Localcam)
{
	if (entityx != 0)
	{
		if (GetAimKey())
		{
			AimAt(entityx, Localcam);
		}
		else
		{
			isaimbotting = false;
		}
	}
}

void AIms(DWORD_PTR entity, Vector3 Localcam)
{
	float max = 100.f;

	uint64_t currentactormesh = read<uint64_t>(DrverInit, FNProcID, entity + 0x280);  // changed often 

	Vector3 rootHead = GetBoneWithRotation(currentactormesh, 66);
	Vector3 rootHeadOut = ProjectWorldToScreen(rootHead, Vector3(Localcam.y, Localcam.x, Localcam.z));

	if (GetClosestPlayerToCrossHair(rootHeadOut, max, Settings::AimbotFOV, entity))
		entityx = entity;
}


float TextShadowNigg(ImFont* pFont, const std::string& text, const ImVec2& pos, float size, ImU32 color, bool center)
{
	

	std::stringstream stream(text);
	std::string line;

	float y = 0.0f;
	int i = 0;

	while (std::getline(stream, line))
	{
		ImVec2 textSize = pFont->CalcTextSizeA(size, FLT_MAX, 0.0f, line.c_str());

		if (center)
		{
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x - textSize.x / 2.0f) + 1, (pos.y + textSize.y * i) + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x - textSize.x / 2.0f) - 1, (pos.y + textSize.y * i) - 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			//window->DrawList->AddText(pFont, size, ImVec2((pos.x - textSize.x / 2.0f) + 1, (pos.y + textSize.y * i) - 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			//window->DrawList->AddText(pFont, size, ImVec2((pos.x - textSize.x / 2.0f) - 1, (pos.y + textSize.y * i) + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());

			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2(pos.x - textSize.x / 2.0f, pos.y + textSize.y * i), ImGui::GetColorU32(color), line.c_str());
		}
		else
		{
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x) + 1, (pos.y + textSize.y * i) + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x) - 1, (pos.y + textSize.y * i) - 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			//window->DrawList->AddText(pFont, size, ImVec2((pos.x) + 1, (pos.y + textSize.y * i) - 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			//window->DrawList->AddText(pFont, size, ImVec2((pos.x) - 1, (pos.y + textSize.y * i) + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());

			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2(pos.x, pos.y + textSize.y * i), ImGui::GetColorU32(color), line.c_str());
		}

		y = pos.y + textSize.y * (i + 1);
		i++;
	}
	return y;
}

void DrawLine(int x1, int y1, int x2, int y2, RGBA* color, int thickness)
{
	ImGui::GetOverlayDrawList()->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), thickness);
}

void DrawFilledRect(int x, int y, int w, int h, RGBA* color)
{
	ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), 0, 0);
}


void DrawSkeleton(DWORD_PTR mesh)
{
	Vector3 vHeadBone = GetBoneWithRotation(mesh, 96);
	Vector3 vHip = GetBoneWithRotation(mesh, 2);
	Vector3 vNeck = GetBoneWithRotation(mesh, 65);
	Vector3 vUpperArmLeft = GetBoneWithRotation(mesh, 34);
	Vector3 vUpperArmRight = GetBoneWithRotation(mesh, 91);
	Vector3 vLeftHand = GetBoneWithRotation(mesh, 35);
	Vector3 vRightHand = GetBoneWithRotation(mesh, 63);
	Vector3 vLeftHand1 = GetBoneWithRotation(mesh, 33);
	Vector3 vRightHand1 = GetBoneWithRotation(mesh, 60);
	Vector3 vRightThigh = GetBoneWithRotation(mesh, 74);
	Vector3 vLeftThigh = GetBoneWithRotation(mesh, 67);
	Vector3 vRightCalf = GetBoneWithRotation(mesh, 75);
	Vector3 vLeftCalf = GetBoneWithRotation(mesh, 68);
	Vector3 vLeftFoot = GetBoneWithRotation(mesh, 69);
	Vector3 vRightFoot = GetBoneWithRotation(mesh, 76);



	Vector3 vHeadBoneOut = ProjectWorldToScreen(vHeadBone, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vHipOut = ProjectWorldToScreen(vHip, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vNeckOut = ProjectWorldToScreen(vNeck, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vUpperArmLeftOut = ProjectWorldToScreen(vUpperArmLeft, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vUpperArmRightOut = ProjectWorldToScreen(vUpperArmRight, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vLeftHandOut = ProjectWorldToScreen(vLeftHand, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vRightHandOut = ProjectWorldToScreen(vRightHand, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vLeftHandOut1 = ProjectWorldToScreen(vLeftHand1, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vRightHandOut1 = ProjectWorldToScreen(vRightHand1, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vRightThighOut = ProjectWorldToScreen(vRightThigh, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vLeftThighOut = ProjectWorldToScreen(vLeftThigh, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vRightCalfOut = ProjectWorldToScreen(vRightCalf, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vLeftCalfOut = ProjectWorldToScreen(vLeftCalf, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vLeftFootOut = ProjectWorldToScreen(vLeftFoot, Vector3(Localcam.y, Localcam.x, Localcam.z));
	Vector3 vRightFootOut = ProjectWorldToScreen(vRightFoot, Vector3(Localcam.y, Localcam.x, Localcam.z));

	//DrawCircle(vHeadBone.x , vHeadBone.y , vHeadBoneOut.x , vHeadBoneOut.y, 1.f , 255.f, 0.f, 0.f, 200.f),


	DrawLine(vHipOut.x, vHipOut.y, vNeckOut.x, vNeckOut.y, &Col.green, 1.f);

	DrawLine(vUpperArmLeftOut.x, vUpperArmLeftOut.y, vNeckOut.x, vNeckOut.y, &Col.green, 1.f);
	DrawLine(vUpperArmRightOut.x, vUpperArmRightOut.y, vNeckOut.x, vNeckOut.y, &Col.green, 1.f);

	DrawLine(vLeftHandOut.x, vLeftHandOut.y, vUpperArmLeftOut.x, vUpperArmLeftOut.y, &Col.green, 1.f);
	DrawLine(vRightHandOut.x, vRightHandOut.y, vUpperArmRightOut.x, vUpperArmRightOut.y, &Col.green, 1.f);

	DrawLine(vLeftHandOut.x, vLeftHandOut.y, vLeftHandOut1.x, vLeftHandOut1.y, &Col.green, 1.f);
	DrawLine(vRightHandOut.x, vRightHandOut.y, vRightHandOut1.x, vRightHandOut1.y, &Col.green, 1.f);

	DrawLine(vLeftThighOut.x, vLeftThighOut.y, vHipOut.x, vHipOut.y, &Col.green, 1.f);
	DrawLine(vRightThighOut.x, vRightThighOut.y, vHipOut.x, vHipOut.y, &Col.green, 1.f);

	DrawLine(vLeftCalfOut.x, vLeftCalfOut.y, vLeftThighOut.x, vLeftThighOut.y, &Col.green, 1.f);
	DrawLine(vRightCalfOut.x, vRightCalfOut.y, vRightThighOut.x, vRightThighOut.y, &Col.green, 1.f);

	DrawLine(vLeftFootOut.x, vLeftFootOut.y, vLeftCalfOut.x, vLeftCalfOut.y, &Col.green, 1.f);
	DrawLine(vRightFootOut.x, vRightFootOut.y, vRightCalfOut.x, vRightCalfOut.y, &Col.green, 1.f);
}
std::string GetObjectNames(int32_t ObjectID) /* maybe working.. */
{
	uint64_t gname = read<uint64_t>(DrverInit, FNProcID, base_address + 0x9643080);

	int64_t fNamePtr = read<uint64_t>(DrverInit, FNProcID, gname + int(ObjectID / 0x4000) * 0x8);
	int64_t fName = read<uint64_t>(DrverInit, FNProcID, fNamePtr + int(ObjectID % 0x4000) * 0x8);

	char pBuffer[64] = { NULL };

	info_t blyat;
	blyat.pid = FNProcID;
	blyat.address = fName + 0x10;
	blyat.value = &pBuffer;
	blyat.size = sizeof(pBuffer);

	unsigned long int asd;
	DeviceIoControl(DrverInit, ctl_read, &blyat, sizeof blyat, &blyat, sizeof blyat, &asd, nullptr);

	return std::string(pBuffer);
}
void DrawCornerBox(int x, int y, int w, int h, int borderPx, RGBA* color)
{
	DrawFilledRect(x + borderPx, y, w / 3, borderPx, color); //top 
	DrawFilledRect(x + w - w / 3 + borderPx, y, w / 3, borderPx, color); //top 
	DrawFilledRect(x, y, borderPx, h / 3, color); //left 
	DrawFilledRect(x, y + h - h / 3 + borderPx * 2, borderPx, h / 3, color); //left 
	DrawFilledRect(x + borderPx, y + h + borderPx, w / 3, borderPx, color); //bottom 
	DrawFilledRect(x + w - w / 3 + borderPx, y + h + borderPx, w / 3, borderPx, color); //bottom 
	DrawFilledRect(x + w + borderPx, y, borderPx, h / 3, color);//right 
	DrawFilledRect(x + w + borderPx, y + h - h / 3 + borderPx * 2, borderPx, h / 3, color);//right 
}

void drawLoop() {


	Uworld = read<DWORD_PTR>(DrverInit, FNProcID, base_address + OFFSET_UWORLD);
	//printf(_xor_("Uworld: %p.\n").c_str(), Uworld);

	DWORD_PTR Gameinstance = read<DWORD_PTR>(DrverInit, FNProcID, Uworld + 0x180); // changes sometimes 4u

	if (Gameinstance == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("Gameinstance: %p.\n").c_str(), Gameinstance);

	DWORD_PTR LocalPlayers = read<DWORD_PTR>(DrverInit, FNProcID, Gameinstance + 0x38);

	if (LocalPlayers == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("LocalPlayers: %p.\n").c_str(), LocalPlayers);

	Localplayer = read<DWORD_PTR>(DrverInit, FNProcID, LocalPlayers);

	if (Localplayer == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("LocalPlayer: %p.\n").c_str(), Localplayer);

	PlayerController = read<DWORD_PTR>(DrverInit, FNProcID, Localplayer + 0x30);

	if (PlayerController == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("playercontroller: %p.\n").c_str(), PlayerController);

	LocalPawn = read<uint64_t>(DrverInit, FNProcID, PlayerController + 0x2A0);  // changed often 4u sometimes called AcknowledgedPawn

	if (LocalPawn == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("Pawn: %p.\n").c_str(), LocalPawn);

	Rootcomp = read<uint64_t>(DrverInit, FNProcID, LocalPawn + 0x130);

	if (Rootcomp == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("Rootcomp: %p.\n").c_str(), Rootcomp);

	if (LocalPawn != 0) {
		localplayerID = read<int>(DrverInit, FNProcID, LocalPawn + 0x18);
	}

	Ulevel = read<DWORD_PTR>(DrverInit, FNProcID, Uworld + 0x30);
	//printf(_xor_("Ulevel: %p.\n").c_str(), Ulevel);

	if (Ulevel == (DWORD_PTR)nullptr)
		return;

	DWORD64 PlayerState = read<DWORD64>(DrverInit, FNProcID, LocalPawn + 0x240);  //changes often 4u

	if (PlayerState == (DWORD_PTR)nullptr)
		return;

	DWORD ActorCount = read<DWORD>(DrverInit, FNProcID, Ulevel + 0xA0);

	DWORD_PTR AActors = read<DWORD_PTR>(DrverInit, FNProcID, Ulevel + 0x98);
	//printf(_xor_("AActors: %p.\n").c_str(), AActors);

	if (AActors == (DWORD_PTR)nullptr)
		return;

	for (int i = 0; i < ActorCount; i++)
	{
		uint64_t CurrentActor = read<uint64_t>(DrverInit, FNProcID, AActors + i * 0x8);

		auto name = GetObjectNames(CurrentActor);

		int curactorid = read<int>(DrverInit, FNProcID, CurrentActor + 0x18);

		if (curactorid == localplayerID || curactorid == 9907819 || curactorid == 9875145 || curactorid == 9873134 || curactorid == 9876800 || curactorid == 9874439) // this number changes for bot and NPC often, modified from original 4u
		// you will need to print out the actorID on screen and find the new numbers, currently different numbers for bots, NPC(2), and bot in solo and 2 player games are different.
		//if (curactorid == localplayerID) //original changed 4u to target bots and NPC
		{
			if (CurrentActor == (uint64_t)nullptr || CurrentActor == -1 || CurrentActor == NULL)
				continue;

			uint64_t CurrentActorRootComponent = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x130);

			if (CurrentActorRootComponent == (uint64_t)nullptr || CurrentActorRootComponent == -1 || CurrentActorRootComponent == NULL)
				continue;

			uint64_t currentactormesh = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x280); // change as needed 4u

			if (currentactormesh == (uint64_t)nullptr || currentactormesh == -1 || currentactormesh == NULL)
				continue;

			int MyTeamId = read<int>(DrverInit, FNProcID, PlayerState + 0xED0);  //changes often 4u

			DWORD64 otherPlayerState = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x240); //changes often 4u

			if (otherPlayerState == (uint64_t)nullptr || otherPlayerState == -1 || otherPlayerState == NULL)
				continue;

			int ActorTeamId = read<int>(DrverInit, FNProcID, otherPlayerState + 0xED0); //changes often 4u

			Vector3 Headpos = GetBoneWithRotation(currentactormesh, 66);
			Localcam = CameraEXT;
			localactorpos = read<Vector3>(DrverInit, FNProcID, Rootcomp + 0x11C);

			float distance = localactorpos.Distance(Headpos) / 100.f;

			if (distance < 0.5)
				continue;
			Vector3 CirclePOS = GetBoneWithRotation(currentactormesh, 2);
			

			Vector3 rootOut = GetBoneWithRotation(currentactormesh, 0);

			Vector3 Out = ProjectWorldToScreen(rootOut, Vector3(Localcam.y, Localcam.x, Localcam.z));

			Vector3 HeadposW2s = ProjectWorldToScreen(Headpos, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 bone0 = GetBoneWithRotation(currentactormesh, 0);
			Vector3 bottom = ProjectWorldToScreen(bone0, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Headbox = ProjectWorldToScreen(Vector3(Headpos.x, Headpos.y, Headpos.z + 15), Vector3(Localcam.y, Localcam.x, Localcam.z));	

			float boxsize = (float)(Out.y - HeadposW2s.y);
			float boxwidth = boxsize / 3.0f;
			float Height1 = abs(Headbox.y - bottom.y);
			float Width1 = Height1 * 0.65;
			float dwpleftx = (float)Out.x - boxwidth / 2.0f;
			float dwplefty = (float)Out.y;
			//ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), Settings::AimbotFOV, ImColor(255,255,255, 230), Settings::Roughness);
			m_pFont = ImGui::GetIO().Fonts->ImFontAtlas::AddFontDefault();

			

			if (Settings::PlayerESP)
			{
				ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(dwpleftx, dwplefty), ImVec2(HeadposW2s.x + boxwidth, HeadposW2s.y + 7.0f), IM_COL32(0, 0, 0, 80));
				ImGui::GetOverlayDrawList()->AddRect(ImVec2(dwpleftx, dwplefty), ImVec2(HeadposW2s.x + boxwidth, HeadposW2s.y + 7.0f), IM_COL32(0, 255, 0, 255));
			}
			if (Settings::Skeleton)
			{
				DrawSkeleton(currentactormesh);
			}
			if (Settings::CornerESP)
			{
				DrawLine(Headbox.x - (Width1 / 2), Headbox.y, Headbox.x - (Width1 / 2) + (Width1 / 4), Headbox.y, &Col.purple, 1.5f);//x top corner left
				DrawLine(Headbox.x - (Width1 / 2), Headbox.y, Headbox.x - (Width1 / 2), Headbox.y + (Width1 / 4), &Col.purple, 1.5f);//y
				DrawLine(Headbox.x - (Width1 / 2) + Width1, Headbox.y, Headbox.x + (Width1 / 2) - (Width1 / 4), Headbox.y, &Col.purple, 1.5f);//x  top corner right
				DrawLine(Headbox.x - (Width1 / 2) + Width1, Headbox.y, Headbox.x - (Width1 / 2) + Width1, Headbox.y + (Width1 / 4), &Col.purple, 1.5f);//y
				DrawLine(Headbox.x - (Width1 / 2), Headbox.y + Height1, Headbox.x - (Width1 / 2) + (Width1 / 4), Headbox.y + Height1, &Col.purple, 1.5f);//x bottom left
				DrawLine(Headbox.x - (Width1 / 2), Headbox.y - (Width1 / 4) + Height1, Headbox.x - (Width1 / 2), Headbox.y + Height1, &Col.purple, 1.5f);//y bottom left
				DrawLine(Headbox.x - (Width1 / 2) + Width1, Headbox.y + Height1, Headbox.x + (Width1 / 2) - (Width1 / 4), Headbox.y + Height1, &Col.purple, 1.5f);//x bottom right
				DrawLine(Headbox.x - (Width1 / 2) + Width1, Headbox.y - (Width1 / 4) + Height1, Headbox.x - (Width1 / 2) + Width1, Headbox.y + Height1, &Col.purple, 1.5f);//y bottom right
			}
			if (Settings::lineesp)
			{
				DrawLine(Width / 2, Height, bottom.x, bottom.y, &Col.black, 1.5f);
			}


				
			if (Settings::Distance)
			{
				CHAR dist[50];
				sprintf_s(dist, ("%.fm"), distance);

				//TextShadowNigg(dist, 13, HeadposW2s.x, HeadposW2s.y - 35, 255.f, 0.f, 0.f, 255.f);
				//TextShadowNigg(m_pFont, dist, ImVec2(HeadposW2s.x, HeadposW2s.y - 35), 16.0f, IM_COL32(255, 0, 0, 255), true);
				TextShadowNigg(m_pFont, dist, ImVec2(HeadposW2s.x, HeadposW2s.y - 35), 16.0f, IM_COL32(255, 0, 0, 255), true);
			}


			if (Settings::MouseAimbot)
			{
				AIms(CurrentActor, Localcam);
			}
		}
	}
	if (Settings::MouseAimbot)
	{
		aimbot(Localcam);
	}
}
int r, g, b;
int r1, g2, b2;

float color_red = 1.;
float color_green = 0;
float color_blue = 0;
float color_random = 0.0;
float color_speed = -10.0;
bool rainbowmode = true;

void ColorChange()
{
	if (rainbowmode)
	{
		static float Color[3];
		static DWORD Tickcount = 0;
		static DWORD Tickcheck = 0;
		ImGui::ColorConvertRGBtoHSV(color_red, color_green, color_blue, Color[0], Color[1], Color[2]);
		if (GetTickCount() - Tickcount >= 1)
		{
			if (Tickcheck != Tickcount)
			{
				Color[0] += 0.001f * color_speed;
				Tickcheck = Tickcount;
			}
			Tickcount = GetTickCount();
		}
		if (Color[0] < 0.0f) Color[0] += 1.0f;
		ImGui::ColorConvertHSVtoRGB(Color[0], Color[1], Color[2], color_red, color_green, color_blue);
	}
}

void Render() {

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	int X;
	int Y;
	float size1 = 3.0f;
	float size2 = 2.0f;

	if (Settings::AimbotCircle)
	{
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), Settings::AimbotFOV, ImColor(r, g, b, 230), Settings::Roughness);
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), Settings::AimbotFOV, ImColor(255, 255, 255, 230), Settings::Roughness);
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(Width / 2, Height / 2), Settings::AimbotFOV, ImGui::GetColorU32({ 255, 0, 166, 255 }), 20);
	}
	

	
	if (Settings::Crosshair)
	{
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(ScreenCenterX - 12, ScreenCenterY), ImVec2((ScreenCenterX - 12) + (12 * 2), ScreenCenterY), ImGui::GetColorU32({ 255, 255, 255, 0.65f }), 1.9);
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(ScreenCenterX, ScreenCenterY - 12), ImVec2(ScreenCenterX, (ScreenCenterY - 12) + (12 * 2)), ImGui::GetColorU32({ 255, 255, 255, 0.65f }), 1.9);
	}
	if (Settings::Radar)
	{
		Radar();
	}

	ColorChange();

	bool circleedit = false;
	auto& style = ImGui::GetStyle();
	style.WindowBorderSize = 0.2f;
	style.WindowRounding = 0.0f;


	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImColor(255, 255, 255);
	colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.09f, 0.94f);
	colors[ImGuiCol_Button] = ImColor(1, 51, 188);
	colors[ImGuiCol_CheckMark] = ImColor(65, 149, 249);
	colors[ImGuiCol_FrameBg] = ImColor(1, 51, 188);
	colors[ImGuiCol_FrameBgHovered] = ImColor(66, 150, 250);



	if (Settings::ShowMenu)
	{
		ImGui::SetNextWindowSize({ 400, 288 });


		static int fortnitetab;

		ImGui::Begin("VOLTIC LEAKED SOURCE BY INIT1.EXE and MCLVAN", 0, ImGuiWindowFlags_::ImGuiWindowFlags_NoResize);
		{
			ImGui::Columns(1);
			if (ImGui::Button(" Aimbot", ImVec2(80, 20))) fortnitetab = 1;
			ImGui::SameLine();
			if (ImGui::Button(" ESP", ImVec2(80, 20))) fortnitetab = 2;
			ImGui::SameLine();
			if (ImGui::Button(" Misc", ImVec2(80, 20))) fortnitetab = 3;


			if (fortnitetab == 1)
			{
				ImGui::Checkbox("Aimbot ", &Settings::MouseAimbot);
				ImGui::Checkbox("Show FOV ", &Settings::AimbotCircle);

				if (Settings::AimbotCircle)
				{
					ImGui::SliderFloat("Fov Size", &Settings::AimbotFOV, 15, 900);
				}

				ImGui::SliderFloat("Aim Speed", &Aim_Speed, 0, 6);
				ImGui::Checkbox("Crosshair ", &Settings::Crosshair);
				ImGui::Checkbox("Reticle", &Settings::Reticle);



			}


			if (fortnitetab == 2)
			{
				ImGui::Checkbox("Snaplines ", &Settings::lineesp);
				ImGui::Checkbox("Skeleton ESP ", &Settings::Skeleton);
				ImGui::Checkbox("Box ESP  ", &Settings::PlayerESP);
				ImGui::Checkbox("Corner ESP ", &Settings::CornerESP);
				ImGui::Checkbox("SelfESP ", &Settings::PlayerESP);
				ImGui::Checkbox("3D ", &Settings::ThreeDESP);




			}



			if (fortnitetab == 3)
			{
				ImGui::Checkbox("NoSpread [Currently Outdated]", &Settings::NoSpread);
				ImGui::Checkbox("Radar ESP [Currently Outdated]", &Settings::Radar);
				if (Settings::Radar)
				{
					ImGui::SliderFloat("Radar Distance", &Settings::RadarDistance, 1000, 50000);
				}
				ImGui::Text("More soon to come");

			}

			ImGui::End();
			ImGui::EndFrame();


		}
	}
	
	

		
		
	
	
	drawLoop();

	ImGui::EndFrame();
	D3DDevice->SetRenderState(D3DRS_ZENABLE, false);
	D3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
	D3DDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, false);
	D3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

	if (D3DDevice->BeginScene() >= 0)
	{
		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		D3DDevice->EndScene();
	}
	HRESULT Results = D3DDevice->Present(NULL, NULL, NULL, NULL);

	if (Results == D3DERR_DEVICELOST && D3DDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
	{
		ImGui_ImplDX9_InvalidateDeviceObjects();
		D3DDevice->Reset(&d3d);
		ImGui_ImplDX9_CreateDeviceObjects();
	}
}

DWORD Menuthread(LPVOID in)
{
	while (1)
	{
		HWND test = FindWindowA(0, ("Fortnite  "));

		if (test == NULL)
		{
			ExitProcess(0);
		}

		
		/*if (GetAsyncKeyState(VK_INSERT) & 1) {
			Settings::ShowMenu = !Settings::ShowMenu;
		}*/


		if (Settings::ShowMenu)
		{



			if (GetAsyncKeyState(VK_F1) & 1) {
				Settings::MouseAimbot = !Settings::MouseAimbot;
			}

			if (GetAsyncKeyState(VK_F2) & 1) {
				Settings::AimbotCircle = !Settings::AimbotCircle;
			}

			if (GetAsyncKeyState(VK_F3) & 1) {
				Settings::Crosshair = !Settings::Crosshair;
			}

			if (GetAsyncKeyState(VK_F4) & 1) {
				Settings::lineesp = !Settings::lineesp;
			}

			if (GetAsyncKeyState(VK_F5) & 1) {
				Settings::Skeleton = !Settings::Skeleton;
			}

			if (GetAsyncKeyState(VK_F6) & 1) {
				Settings::PlayerESP = !Settings::PlayerESP;
			}

			if (GetAsyncKeyState(VK_F7) & 1) {
				Settings::CornerESP = !Settings::CornerESP;
			}

			
		}
	}
}


MSG Message_Loop = { NULL };

void Loop()
{
	static RECT old_rc;
	ZeroMemory(&Message_Loop, sizeof(MSG));

	while (Message_Loop.message != WM_QUIT)
	{
		if (PeekMessage(&Message_Loop, Window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message_Loop);
			DispatchMessage(&Message_Loop);
		}

		HWND hwnd_active = GetForegroundWindow();

		if (hwnd_active == hWnd) {
			HWND hwndtest = GetWindow(hwnd_active, GW_HWNDPREV);
			SetWindowPos(Window, hwndtest, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		if (GetAsyncKeyState(0x23) & 1)
			exit(8);

		RECT rc;
		POINT xy;

		ZeroMemory(&rc, sizeof(RECT));
		ZeroMemory(&xy, sizeof(POINT));
		GetClientRect(hWnd, &rc);
		ClientToScreen(hWnd, &xy);
		rc.left = xy.x;
		rc.top = xy.y;

		ImGuiIO& io = ImGui::GetIO();
		io.ImeWindowHandle = hWnd;
		io.DeltaTime = 1.0f / 60.0f;

		POINT p;
		GetCursorPos(&p);
		io.MousePos.x = p.x - xy.x;
		io.MousePos.y = p.y - xy.y;

		if (GetAsyncKeyState(VK_LBUTTON)) {
			io.MouseDown[0] = true;
			io.MouseClicked[0] = true;
			io.MouseClickedPos[0].x = io.MousePos.x;
			io.MouseClickedPos[0].x = io.MousePos.y;
		}
		else
			io.MouseDown[0] = false;

		if (rc.left != old_rc.left || rc.right != old_rc.right || rc.top != old_rc.top || rc.bottom != old_rc.bottom)
		{
			old_rc = rc;

			Width = rc.right;
			Height = rc.bottom;

			d3d.BackBufferWidth = Width;
			d3d.BackBufferHeight = Height;
			SetWindowPos(Window, (HWND)0, xy.x, xy.y, Width, Height, SWP_NOREDRAW);
			D3DDevice->Reset(&d3d);
		}
		Render();
	}
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	DestroyWindow(Window);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, Message, wParam, lParam))
		return true;

	switch (Message)
	{
	case WM_DESTROY:
		ShutDown();
		PostQuitMessage(0);
		exit(4);
		break;
	case WM_SIZE:
		if (D3DDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			ImGui_ImplDX9_InvalidateDeviceObjects();
			d3d.BackBufferWidth = LOWORD(lParam);
			d3d.BackBufferHeight = HIWORD(lParam);
			HRESULT hr = D3DDevice->Reset(&d3d);
			if (hr == D3DERR_INVALIDCALL)
				IM_ASSERT(0);
			ImGui_ImplDX9_CreateDeviceObjects();
		}
		break;
	default:
		return DefWindowProc(hWnd, Message, wParam, lParam);
		break;
	}
	return 0;
}


void ShutDown()
{
	VertBuff->Release();
	D3DDevice->Release();
	pObject->Release();

	DestroyWindow(Window);
	UnregisterClass(L"fgers", NULL);
}

int main(int argc, const char* argv[])
{
	Sleep(300);  //slowed it down a bit 4u
	SetConsoleTitleA("Voltic Leaked Cuz Im leaving comm lol ");



	Sleep(3000);

	CreateThread(NULL, NULL, GUI, NULL, NULL, NULL);
	DrverInit = CreateFileW((L"\\\\.\\may2h2drve"), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (DrverInit == INVALID_HANDLE_VALUE)
	{
		printf("\nVoltic Leaked Cuz Im leaving comm lol");
		Sleep(2000);
		exit(0);
	}

	info_t Input_Output_Data1;
	unsigned long int Readed_Bytes_Amount1;
	DeviceIoControl(DrverInit, ctl_clear, &Input_Output_Data1, sizeof Input_Output_Data1, &Input_Output_Data1, sizeof Input_Output_Data1, &Readed_Bytes_Amount1, nullptr);
	std::string name = ("[Voltic Leaked Cuz Im leaving comm lol]");

	while (hWnd == NULL)
	{
		hWnd = FindWindowA(0, ("Fortnite  "));
		system("cls");
		printf("\n [Voltic Leaked Cuz Im leaving comm lol] | Looking for Fortnite Process!");
		Sleep(1000);

	}
	GetWindowThreadProcessId(hWnd, &FNProcID);

	
	info_t Input_Output_Data;
	Input_Output_Data.pid = FNProcID;
	unsigned long int Readed_Bytes_Amount;
	CreateThread(NULL, NULL, Menuthread, NULL, NULL, NULL);
	DeviceIoControl(DrverInit, ctl_base, &Input_Output_Data, sizeof Input_Output_Data, &Input_Output_Data, sizeof Input_Output_Data, &Readed_Bytes_Amount, nullptr);
	base_address = (unsigned long long int)Input_Output_Data.data;
	printf("\n [Voltic Leaked Cuz Im leaving comm lol] | Cheat Loaded!!");
	std::printf(("ID: %p.\n"), (void*)base_address);
	printf("\n [Voltic Leaked Cuz Im leaving comm lol] | Dont close this! If you close it cheat will stop working");




	//HANDLE handle = CreateThread(nullptr, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(cache), nullptr, NULL, nullptr);
	//CloseHandle(handle);

	WindowMain();
	InitializeD3D();

	Loop();
	ShutDown();

	return 0;

}