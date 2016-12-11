#include <vsl/application.h>
#include <iostream>
#include <sstream>
#include <windowsx.h>


namespace
{
	static const char* kDebugLayerNames[] = {
		"VK_LAYER_LUNARG_standard_validation",
	};

	static const wchar_t*	kClassName = L"VulcanSample";
	static const wchar_t*	kAppName = L"VulcanSample";

	static const uint64_t	kFenceTimeout = 100000000000;

	static vsl::InputData* pInputData_ = nullptr;

	static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_SETFOCUS:
		{
			return 0;
		}

		case WM_KILLFOCUS:
		{
			return 0;
		}

		case WM_SYSCOMMAND:
		{
			switch (wParam & 0xfff0)
			{
			case SC_SCREENSAVE:
			case SC_MONITORPOWER:
			{
				break;
			}

			case SC_KEYMENU:
				return 0;
			}
			break;
		}

		case WM_CLOSE:
		{
			PostQuitMessage(0);
			return 0;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			// TODO: キー入力に対する処理
			break;
		}

		case WM_LBUTTONDOWN:
		{
			pInputData_->mouseButton_ |= vsl::MouseButton::LEFT;
			return 0;
		}
		case WM_RBUTTONDOWN:
		{
			pInputData_->mouseButton_ |= vsl::MouseButton::RIGHT;
			return 0;
		}
		case WM_MBUTTONDOWN:
		{
			pInputData_->mouseButton_ |= vsl::MouseButton::MIDDLE;
			return 0;
		}
		case WM_LBUTTONUP:
		{
			pInputData_->mouseButton_ &= ~vsl::MouseButton::LEFT;
			return 0;
		}
		case WM_RBUTTONUP:
		{
			pInputData_->mouseButton_ &= ~vsl::MouseButton::RIGHT;
			return 0;
		}
		case WM_MBUTTONUP:
		{
			pInputData_->mouseButton_ &= ~vsl::MouseButton::MIDDLE;
			return 0;
		}
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		{
			// TODO: マウス操作に対する処理
			return 0;
		}

		case WM_MOUSEMOVE:
		{
			// TODO: マウス移動に対する処理
			pInputData_->mouseX_ = GET_X_LPARAM(lParam);
			pInputData_->mouseY_ = GET_Y_LPARAM(lParam);
			return 0;
		}

		case WM_MOUSELEAVE:
		{
			return 0;
		}

		case WM_MOUSEWHEEL:
		{
			return 0;
		}

		case WM_MOUSEHWHEEL:
		{
			return 0;
		}

		case WM_SIZE:
		{
			// TODO: サイズ変更時の処理
			return 0;
		}

		case WM_MOVE:
		{
			return 0;
		}

		case WM_PAINT:
		{
			//_glfwInputWindowDamage(window);
			break;
		}

		}

		return DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}

}	// namespace

namespace vsl
{
	//----
	void Application::Run(uint16_t screenWidth, uint16_t screenHeight)
	{
		screenWidth_ = screenWidth;
		screenHeight_ = screenHeight;
		pInputData_ = &inputData_;

		// ウィンドウの初期化
		if (!InitializeWindow())
		{
			return;
		}
		// コンテキストの初期化
		if (!device_.InitializeContext(hInstance_, hWnd_, screenWidth, screenHeight))
		{
			return;
		}

		// アプリごとの初期化
		if (!initFunc_(device_))
		{
			return;
		}

		while (true)
		{
			PollEvents();

			if (closeRequest_)
			{
				break;
			}

			// 入力に対する関数を呼び出す
			inputFunc_(inputData_);

			// アプリごとのループ処理
			if (!loopFunc_(device_))
			{
				break;
			}
		}

		// アプリごとの終了処理
		termFunc_(device_);

		device_.DestroyContext();
		UnregisterClass(kClassName, hInstance_);
	}

	//----
	void Application::PollEvents()
	{
		MSG msg;

		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				// TODO: 終了リクエスト
				closeRequest_ = true;
			}
			else
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}
	}

	//----
	bool Application::InitializeWindow()
	{
		{
			WNDCLASSEXW wc;

			ZeroMemory(&wc, sizeof(wc));
			wc.cbSize = sizeof(wc);
			wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
			wc.lpfnWndProc = (WNDPROC)windowProc;
			wc.hInstance = hInstance_;
			wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
			wc.lpszClassName = kClassName;

			if (!RegisterClassEx(&wc))
			{
				return false;
			}

			RECT wr = { 0, 0, screenWidth_, screenHeight_ };
			AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
			hWnd_ = CreateWindowEx(0,
				kClassName,
				kAppName,
				WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_SYSMENU,
				100, 100,				// window x,y
				wr.right - wr.left,		// width
				wr.bottom - wr.top,		// height
				nullptr,				// handle to parent
				nullptr,				// handle to menu
				hInstance_,				// hInstance
				nullptr);
			if (!hWnd_)
			{
				return false;
			}
		}

		PollEvents();

		return true;
	}

}	// namespace vsl


//	EOF
