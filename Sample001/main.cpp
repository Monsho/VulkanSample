#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <iostream>
#include <sstream>
#include <cmath>


namespace
{
	static const char* kDebugLayerNames[] = {
		"VK_LAYER_LUNARG_standard_validation",
	};

	static const wchar_t*	kClassName = L"VulcanSample";
	static const wchar_t*	kAppName = L"VulcanSample";

	static const int		kScreenWidth = 1920;
	static const int		kScreenHeight = 1080;
	static const uint64_t	kFenceTimeout = 100000000000;

	HINSTANCE	g_hInstance;
	HWND		g_hWnd;

	vk::Instance					g_VkInstance;
	vk::PhysicalDevice				g_VkPhysicalDevice;
	vk::Device						g_VkDevice;
	vk::PipelineCache				g_VkPipelineCache;
	vk::Queue						g_VkQueue;
	vk::CommandPool					g_VkCmdPool;
	std::vector<vk::Framebuffer>	g_VkFramebuffers;
	vk::Semaphore					g_VkPresentComplete;
	vk::Semaphore					g_VkRenderComplete;
	std::vector<vk::CommandBuffer>	g_VkCmdBuffers;

	bool							g_CloseRequest = false;

#if defined(_DEBUG)
	PFN_vkCreateDebugReportCallbackEXT	g_fCreateDebugReportCallback = VK_NULL_HANDLE;
	PFN_vkDestroyDebugReportCallbackEXT	g_fDestroyDebugReportCallback = VK_NULL_HANDLE;
	PFN_vkDebugReportMessageEXT			g_fDebugBreakCallback = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT			g_fMsgCallback;
#endif

	// 対象フラグのキューインデックスを取得する
	static const uint32_t	kQueueIndexNotFound = 0xffffffff;
	uint32_t FindQueue(vk::QueueFlags queueFlag, const vk::SurfaceKHR& surface = vk::SurfaceKHR())
	{
		std::vector<vk::QueueFamilyProperties> queueProps = g_VkPhysicalDevice.getQueueFamilyProperties();
		size_t queueCount = queueProps.size();
		for (uint32_t i = 0; i < queueCount; i++)
		{
			if (queueProps[i].queueFlags & queueFlag)
			{
				if (!g_VkPhysicalDevice.getSurfaceSupportKHR(i, surface))
				{
					continue;
				}
				return i;
			}
		}
		return kQueueIndexNotFound;
	}

	uint32_t GetMemoryTypeIndex(uint32_t bits, const vk::MemoryPropertyFlags& properties)
	{
		uint32_t result = 0;
		vk::PhysicalDeviceMemoryProperties deviceMemoryProperties = g_VkPhysicalDevice.getMemoryProperties();
		for (uint32_t i = 0; i < 32; i++)
		{
			if ((bits & 1) == 1)
			{
				if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					return i;
				}
			}
			bits >>= 1;
		}

		assert(!"NOT found memory type.\n");
		return 0xffffffff;
	}

	// イメージレイアウトを設定するため、バリアを貼る
	void SetImageLayout(
		vk::CommandBuffer cmdbuffer,
		vk::Image image,
		vk::ImageLayout oldImageLayout,
		vk::ImageLayout newImageLayout,
		vk::ImageSubresourceRange subresourceRange)
	{
		// イメージバリアオブジェクト設定
		vk::ImageMemoryBarrier imageMemoryBarrier;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;

		// 現在のレイアウト

		// イメージ生成時の状態
		// 以降、この状態に戻ることはない
		if (oldImageLayout == vk::ImageLayout::ePreinitialized)						imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
		else if (oldImageLayout == vk::ImageLayout::eTransferDstOptimal)			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		// カラー
		else if (oldImageLayout == vk::ImageLayout::eColorAttachmentOptimal)		imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		// 深度・ステンシル
		else if (oldImageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)	imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		// 転送元
		else if (oldImageLayout == vk::ImageLayout::eTransferSrcOptimal)			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		// シェーダリソース
		else if (oldImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal)			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;

		// 次のレイアウト

		// 転送先
		if (newImageLayout == vk::ImageLayout::eTransferDstOptimal)					imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		// 転送元
		else if (newImageLayout == vk::ImageLayout::eTransferSrcOptimal)			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;// | imageMemoryBarrier.srcAccessMask;
		// カラー
		else if (newImageLayout == vk::ImageLayout::eColorAttachmentOptimal)		imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		// 深度・ステンシル
		else if (newImageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)	imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		// シェーダリソース
		else if (newImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal)			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		// Put barrier on top
		// Put barrier inside setup command buffer
		cmdbuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::DependencyFlags(),
			nullptr, nullptr, imageMemoryBarrier);
	}

	// スワップチェイン
	class MySwapchain
	{
	public:
		struct Image
		{
			vk::Image		image;
			vk::ImageView	view;
			vk::Fence		fence;
		};	// struct Image

	public:
		MySwapchain()
		{
			m_presentInfo.swapchainCount = 1;
			m_presentInfo.pSwapchains = &m_swapchain;
			m_presentInfo.pImageIndices = &m_currentImage;
		}
		~MySwapchain()
		{}

		bool Initialize()
		{
			{
				vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo;
				surfaceCreateInfo.hinstance = g_hInstance;
				surfaceCreateInfo.hwnd = g_hWnd;
				m_surface = g_VkInstance.createWin32SurfaceKHR(surfaceCreateInfo);
				if (!m_surface)
				{
					return false;
				}
			}

			// サポートされているフォーマットを取得
			auto surfaceFormats = g_VkPhysicalDevice.getSurfaceFormatsKHR(m_surface);
			if (surfaceFormats[0].format == vk::Format::eUndefined)
			{
				m_format = vk::Format::eR8G8B8A8Unorm;
			}
			else
			{
				m_format = surfaceFormats[0].format;
			}
			m_colorSpace = surfaceFormats[0].colorSpace;

			m_graphicsQueueIndex = FindQueue(vk::QueueFlagBits::eGraphics, m_surface);
			if (m_graphicsQueueIndex == kQueueIndexNotFound)
			{
				return false;
			}

			return true;
		}

		bool InitializeSwapchain(uint32_t width, uint32_t height, bool enableVSync)
		{
			vk::SwapchainKHR oldSwapchain = m_swapchain;
			m_currentImage = 0;

			vk::SurfaceCapabilitiesKHR surfaceCaps = g_VkPhysicalDevice.getSurfaceCapabilitiesKHR(m_surface);
			auto presentModes = g_VkPhysicalDevice.getSurfacePresentModesKHR(m_surface);

			vk::Extent2D swapchainExtent(width, height);
			if (surfaceCaps.currentExtent.width > -1 && surfaceCaps.currentExtent.height > -1)
			{
				swapchainExtent = surfaceCaps.currentExtent;
			}

			// Presentモードを選択する
			vk::PresentModeKHR swapchainPresentMode = vk::PresentModeKHR::eFifo;
			if (!enableVSync)
			{
				auto numPresentMode = presentModes.size();
				for (size_t i = 0; i < numPresentMode; i++)
				{
					if (presentModes[i] == vk::PresentModeKHR::eMailbox)
					{
						swapchainPresentMode = vk::PresentModeKHR::eMailbox;
						break;
					}
					else if (presentModes[i] == vk::PresentModeKHR::eImmediate)
					{
						swapchainPresentMode = vk::PresentModeKHR::eImmediate;
					}
				}
			}

			// イメージ数を決定する
			uint32_t desiredNumSwapchainImages = surfaceCaps.minImageCount + 1;
			if ((surfaceCaps.maxImageCount > 0) && (desiredNumSwapchainImages > surfaceCaps.maxImageCount))
			{
				desiredNumSwapchainImages = surfaceCaps.maxImageCount;
			}

			// サーフェイスのトランスフォームを決定
			// 通常はIdentityでいいはずだが、スマホとかだとそれ以外があるのかも？
			vk::SurfaceTransformFlagBitsKHR preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
			if (!(surfaceCaps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity))
			{
				preTransform = surfaceCaps.currentTransform;
			}

			// Swapchainの生成
			{
				vk::SwapchainCreateInfoKHR swapchainCreateInfo;
				swapchainCreateInfo.surface = m_surface;
				swapchainCreateInfo.minImageCount = desiredNumSwapchainImages;
				swapchainCreateInfo.imageFormat = m_format;
				swapchainCreateInfo.imageColorSpace = m_colorSpace;
				swapchainCreateInfo.imageExtent = swapchainExtent;
				swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
				swapchainCreateInfo.preTransform = preTransform;
				swapchainCreateInfo.imageArrayLayers = 1;
				swapchainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
				swapchainCreateInfo.queueFamilyIndexCount = 0;
				swapchainCreateInfo.pQueueFamilyIndices = nullptr;
				swapchainCreateInfo.presentMode = swapchainPresentMode;
				swapchainCreateInfo.oldSwapchain = oldSwapchain;
				swapchainCreateInfo.clipped = true;
				swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;

				m_swapchain = g_VkDevice.createSwapchainKHR(swapchainCreateInfo);
				if (!m_swapchain)
				{
					return false;
				}
			}

			// 前回のSwapchainが残っている場合は削除
			if (oldSwapchain)
			{
				for (uint32_t i = 0; i < m_imageCount; i++)
				{
					g_VkDevice.destroyImageView(m_images[i].view);
				}
				g_VkDevice.destroySwapchainKHR(oldSwapchain);
			}

			vk::ImageViewCreateInfo viewCreateInfo;
			viewCreateInfo.format = m_format;
			viewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			viewCreateInfo.subresourceRange.levelCount = 1;
			viewCreateInfo.subresourceRange.layerCount = 1;
			viewCreateInfo.viewType = vk::ImageViewType::e2D;

			auto swapChainImages = g_VkDevice.getSwapchainImagesKHR(m_swapchain);
			m_imageCount = static_cast<uint32_t>(swapChainImages.size());

			// Swapchainの各イメージに対してViewとFenceを作成
			m_images.resize(m_imageCount);
			for (uint32_t i = 0; i < m_imageCount; i++)
			{
				m_images[i].image = swapChainImages[i];
				viewCreateInfo.image = swapChainImages[i];
				m_images[i].view = g_VkDevice.createImageView(viewCreateInfo);
				m_images[i].fence = vk::Fence();
			}

			return true;
		}

		std::vector<vk::Framebuffer> CreateFramebuffers(vk::FramebufferCreateInfo framebufferCreateInfo)
		{
			std::vector<vk::ImageView> views;
			views.resize(framebufferCreateInfo.attachmentCount);
			for (size_t i = 0; i < framebufferCreateInfo.attachmentCount; i++)
			{
				views[i] = framebufferCreateInfo.pAttachments[i];
			}
			framebufferCreateInfo.pAttachments = views.data();

			std::vector<vk::Framebuffer> framebuffers;
			framebuffers.resize(m_imageCount);
			for (uint32_t i = 0; i < m_imageCount; i++)
			{
				views[0] = m_images[i].view;
				framebuffers[i] = g_VkDevice.createFramebuffer(framebufferCreateInfo);
			}
			return framebuffers;
		}

		void Destroy()
		{
			for (auto& image : m_images)
			{
				g_VkDevice.destroyImageView(image.view);
				g_VkDevice.destroyFence(image.fence);
			}
			g_VkDevice.destroySwapchainKHR(m_swapchain);
			g_VkInstance.destroySurfaceKHR(m_surface);
		}

		uint32_t AcquireNextImage(vk::Semaphore presentCompleteSemaphore)
		{
			auto resultValue = g_VkDevice.acquireNextImageKHR(m_swapchain, UINT64_MAX, presentCompleteSemaphore, vk::Fence());
			assert(resultValue.result == vk::Result::eSuccess);

			m_currentImage = resultValue.value;
			return m_currentImage;
		}

		vk::Result Present(vk::Semaphore waitSemaphore)
		{
			m_presentInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
			m_presentInfo.pWaitSemaphores = &waitSemaphore;
			return g_VkQueue.presentKHR(m_presentInfo);
		}

		vk::Fence GetSubmitFence(bool destroy = false)
		{
			auto& image = m_images[m_currentImage];
			while (image.fence)
			{
				// Fenceが有効な間は完了するまで待つ
				vk::Result fenceRes = g_VkDevice.waitForFences(image.fence, VK_TRUE, kFenceTimeout);
				if (fenceRes == vk::Result::eSuccess)
				{
					if (destroy)
					{
						g_VkDevice.destroyFence(image.fence);
					}
					image.fence = vk::Fence();
				}
			}

			image.fence = g_VkDevice.createFence(vk::FenceCreateFlags());
			return image.fence;
		}

		// getter
		vk::SurfaceKHR&		GetSurface() { return m_surface; }
		vk::SwapchainKHR&	GetSwapchain() { return m_swapchain; }
		vk::PresentInfoKHR&	GetPresentInfo() { return m_presentInfo; }
		std::vector<Image>&	GetImages() { return m_images; }
		vk::Format			GetFormat() const { return m_format; }
		vk::ColorSpaceKHR	GetColorSpace() const { return m_colorSpace; }
		uint32_t			GetImageCount() const { return m_imageCount; }

	private:
		vk::SurfaceKHR		m_surface;
		vk::SwapchainKHR	m_swapchain;
		vk::PresentInfoKHR	m_presentInfo;
		std::vector<Image>	m_images;
		vk::Format			m_format;
		vk::ColorSpaceKHR	m_colorSpace;
		uint32_t			m_imageCount{ 0 };
		uint32_t			m_currentImage{ 0 };
		uint32_t			m_graphicsQueueIndex;
	};	// class MySwapchain

	MySwapchain		g_VkSwapchain;
}	// namespace


// デバッグ時のメッセージコールバック
VkBool32 DebugMessageCallback(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objType,
	uint64_t srcObject,
	size_t location,
	int32_t msgCode,
	const char* pLayerPrefix,
	const char* pMsg,
	void* pUserData)
{
	std::string message;
	{
		std::stringstream buf;
		if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
			buf << "ERROR: ";
		}
		else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
			buf << "WARNING: ";
		}
		else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
			buf << "PERF: ";
		}
		else {
			return false;
		}
		buf << "[" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg;
		message = buf.str();
	}

	std::cout << message << std::endl;

	// デバッグウィンドウにも出力
	OutputDebugStringA(message.c_str());
	OutputDebugStringA("\n");

	return false;
}

// コンテキスト作成
bool InitializeContext()
{
	// Vulkanインスタンスを作成
	{
		vk::ApplicationInfo appInfo;
		appInfo.pApplicationName = "VulkanSample";
		appInfo.pEngineName = "VulkanSample";
		appInfo.apiVersion = VK_API_VERSION_1_0;

		// Extension
		const char* extensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,			// Windows用Extension
#if defined(_DEBUG)
			VK_EXT_DEBUG_REPORT_EXTENSION_NAME,				// デバッグレポート用Extension
#endif
		};

		// インスタンス生成情報
		vk::InstanceCreateInfo createInfo;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = ARRAYSIZE(extensions);
		createInfo.ppEnabledExtensionNames = extensions;
#if defined(_DEBUG)
		// デバッグ関連
		createInfo.enabledLayerCount = ARRAYSIZE(kDebugLayerNames);
		createInfo.ppEnabledLayerNames = kDebugLayerNames;
#endif

		// インスタンス生成
		g_VkInstance = vk::createInstance(createInfo);
		if (!g_VkInstance)
		{
			return false;
		}
	}

	// 物理デバイス
	g_VkPhysicalDevice = g_VkInstance.enumeratePhysicalDevices()[0];
	{
		struct Version {
			uint32_t patch : 12;
			uint32_t minor : 10;
			uint32_t major : 10;
		} _version;

		vk::PhysicalDeviceProperties deviceProperties = g_VkPhysicalDevice.getProperties();
		memcpy(&_version, &deviceProperties.apiVersion, sizeof(uint32_t));
		vk::PhysicalDeviceFeatures deviceFeatures = g_VkPhysicalDevice.getFeatures();
		vk::PhysicalDeviceMemoryProperties deviceMemoryProperties = g_VkPhysicalDevice.getMemoryProperties();

		// プロパティやFeatureの確認はここで行う
	}

	// Vulkan device
	uint32_t graphicsQueueIndex = 0;
	{
		// グラフィクス用のキューを検索する
		graphicsQueueIndex = FindQueue(vk::QueueFlagBits::eGraphics);

		float queuePriorities[] = { 0.0f };
		vk::DeviceQueueCreateInfo queueCreateInfo;
		queueCreateInfo.queueFamilyIndex = graphicsQueueIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = queuePriorities;

		vk::PhysicalDeviceFeatures deviceFeatures = g_VkPhysicalDevice.getFeatures();

		const char* enabledExtensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if defined(_DEBUG)
			//VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
#endif
		};
		vk::DeviceCreateInfo deviceCreateInfo;
		deviceCreateInfo.queueCreateInfoCount = 1;
		deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
		deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
		deviceCreateInfo.enabledExtensionCount = (uint32_t)ARRAYSIZE(enabledExtensions);
		deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions;
		deviceCreateInfo.enabledLayerCount = ARRAYSIZE(kDebugLayerNames);
		deviceCreateInfo.ppEnabledLayerNames = kDebugLayerNames;

		g_VkDevice = g_VkPhysicalDevice.createDevice(deviceCreateInfo);
	}

#if defined(_DEBUG)
	// デバッグ用コールバック設定
	{
		g_fCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_VkInstance, "vkCreateDebugReportCallbackEXT");
		g_fDestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_VkInstance, "vkDestroyDebugReportCallbackEXT");
		g_fDebugBreakCallback = (PFN_vkDebugReportMessageEXT)vkGetInstanceProcAddr(g_VkInstance, "vkDebugReportMessageEXT");

		VkDebugReportCallbackCreateInfoEXT dbgCreateInfo = {};
		vk::DebugReportFlagsEXT flags = vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning;
		dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
		dbgCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)DebugMessageCallback;
		dbgCreateInfo.flags = flags.operator VkSubpassDescriptionFlags();

		VkResult err = g_fCreateDebugReportCallback(g_VkInstance, &dbgCreateInfo, nullptr, &g_fMsgCallback);
		assert(!err);
	}
#endif

	g_VkPipelineCache = g_VkDevice.createPipelineCache(vk::PipelineCacheCreateInfo());
	g_VkQueue = g_VkDevice.getQueue(graphicsQueueIndex, 0);

	// コマンドプール作成
	vk::CommandPoolCreateInfo cmdPoolInfo;
	cmdPoolInfo.queueFamilyIndex = graphicsQueueIndex;
	cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	g_VkCmdPool = g_VkDevice.createCommandPool(cmdPoolInfo);

	return true;
}

// コンテキスト破棄
void DestroyContext()
{
	// Idle状態になるのを待つ
	g_VkQueue.waitIdle();
	g_VkDevice.waitIdle();

	g_VkDevice.destroyCommandPool(g_VkCmdPool);
	g_VkDevice.destroyPipelineCache(g_VkPipelineCache);
	g_VkDevice.destroy();
#if defined(_DEBUG)
	g_fDestroyDebugReportCallback(g_VkInstance, g_fMsgCallback, nullptr);
#endif
	g_VkInstance.destroy();
}

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
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_XBUTTONUP:
	{
		// TODO: マウス操作に対する処理
		return 0;
	}

	case WM_MOUSEMOVE:
	{
		// TODO: マウス移動に対する処理
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

void PollEvents()
{
	MSG msg;

	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
		{
			// TODO: 終了リクエスト
			g_CloseRequest = true;
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
}

bool InitializeWindow()
{
	{
		WNDCLASSEXW wc;

		ZeroMemory(&wc, sizeof(wc));
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = (WNDPROC)windowProc;
		wc.hInstance = g_hInstance;
		wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpszClassName = kClassName;

		if (!RegisterClassEx(&wc))
		{
			return false;
		}

		RECT wr = { 0, 0, kScreenWidth, kScreenHeight };
		AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
		g_hWnd = CreateWindowEx(0,
			kClassName,
			kAppName,
			WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_SYSMENU,
			100, 100,				// window x,y
			wr.right - wr.left,		// width
			wr.bottom - wr.top,		// height
			nullptr,				// handle to parent
			nullptr,				// handle to menu
			g_hInstance,			// hInstance
			nullptr);
		if (!g_hWnd)
		{
			return false;
		}
	}

	PollEvents();

	return true;
}

void DestroyWindow()
{
	UnregisterClass(kClassName, g_hInstance);
}

bool InitializeRenderSettings()
{
	// セマフォ設定
	{
		vk::SemaphoreCreateInfo semaphoreCreateInfo;

		// Presentの完了を確認するため
		g_VkPresentComplete = g_VkDevice.createSemaphore(semaphoreCreateInfo);

		// 描画コマンドの処理完了を確認するため
		g_VkRenderComplete = g_VkDevice.createSemaphore(semaphoreCreateInfo);

		if (!g_VkPresentComplete || !g_VkRenderComplete)
		{
			return false;
		}
	}

	// コマンドバッファ作成
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = g_VkCmdPool;
		allocInfo.commandBufferCount = g_VkSwapchain.GetImageCount();
		g_VkCmdBuffers = g_VkDevice.allocateCommandBuffers(allocInfo);
	}

	return true;
}

void DestroyRenderSettings()
{
	g_VkDevice.destroySemaphore(g_VkPresentComplete);
	g_VkDevice.destroySemaphore(g_VkRenderComplete);
}

void DrawScene()
{
	// 次のバックバッファを取得する
	uint32_t currentBuffer = g_VkSwapchain.AcquireNextImage(g_VkPresentComplete);

	// コマンドバッファの積み込み
	{
		auto& cmdBuffer = g_VkCmdBuffers[currentBuffer];

		cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
		vk::CommandBufferBeginInfo cmdBufInfo;
		cmdBuffer.begin(cmdBufInfo);

		vk::ClearColorValue clearColor(std::array<float, 4>{ 0.0f, 0.0f, 0.5f, 1.0f });

		// カラーバッファクリア
		{
			MySwapchain::Image& colorImage = g_VkSwapchain.GetImages()[currentBuffer];
			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;
			
			// カラーバッファクリアのため、レイアウトを変更する
			SetImageLayout(
				cmdBuffer,
				colorImage.image,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal,
				subresourceRange);

			// クリアコマンド
			cmdBuffer.clearColorImage(colorImage.image, vk::ImageLayout::eTransferDstOptimal, clearColor, subresourceRange);

			// Presentのため、レイアウトを変更する
			SetImageLayout(
				cmdBuffer,
				colorImage.image,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::ePresentSrcKHR,
				subresourceRange);
		}

		cmdBuffer.end();
	}

	// Submit
	{
		vk::PipelineStageFlags pipelineStages = vk::PipelineStageFlagBits::eBottomOfPipe;
		vk::SubmitInfo submitInfo;
		submitInfo.pWaitDstStageMask = &pipelineStages;
		// 待つ必要があるセマフォの数とその配列を渡す
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &g_VkPresentComplete;
		// Submitするコマンドバッファの配列を渡す
		// 複数のコマンドバッファをSubmitしたい場合は配列にする
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &g_VkCmdBuffers[currentBuffer];
		// 描画完了を知らせるセマフォを登録する
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &g_VkRenderComplete;

		// Queueに対してSubmitする
		vk::Fence fence = g_VkSwapchain.GetSubmitFence(true);
		g_VkQueue.submit(submitInfo, fence);
		vk::Result fenceRes = g_VkDevice.waitForFences(fence, VK_TRUE, kFenceTimeout);
		assert(fenceRes == vk::Result::eSuccess);
		//g_VkQueue.waitIdle();
	}

	// Present
	g_VkSwapchain.Present(g_VkRenderComplete);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
	g_hInstance = hInstance;

	InitializeContext();
	InitializeWindow();

	g_VkSwapchain.Initialize();
	g_VkSwapchain.InitializeSwapchain(kScreenWidth, kScreenHeight, false);

	InitializeRenderSettings();

	while (true)
	{
		PollEvents();

		if (g_CloseRequest)
		{
			break;
		}

		DrawScene();
	}

	DestroyRenderSettings();
	g_VkSwapchain.Destroy();
	DestroyWindow();
	DestroyContext();
	return 0;
}


//	EOF
