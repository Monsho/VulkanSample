#include <vsl/device.h>
#include <vsl/image.h>
#include <iostream>
#include <sstream>


namespace
{
	static const char* kDebugLayerNames[] = {
		"VK_LAYER_LUNARG_standard_validation",
	};

	static const wchar_t*	kClassName = L"VulcanSample";
	static const wchar_t*	kAppName = L"VulcanSample";

	static const uint64_t	kFenceTimeout = 100000000000;

#if defined(_DEBUG)
	PFN_vkCreateDebugReportCallbackEXT	g_fCreateDebugReportCallback = VK_NULL_HANDLE;
	PFN_vkDestroyDebugReportCallbackEXT	g_fDestroyDebugReportCallback = VK_NULL_HANDLE;
	PFN_vkDebugReportMessageEXT			g_fDebugBreakCallback = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT			g_fMsgCallback;

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
#endif

}	// namespace

namespace vsl
{
	//----
	uint32_t Device::FindQueue(vk::QueueFlags queueFlag, vk::QueueFlags notFlag, const vk::SurfaceKHR& surface)
	{
		std::vector<vk::QueueFamilyProperties> queueProps = vkPhysicalDevice_.getQueueFamilyProperties();
		size_t queueCount = queueProps.size();
		for (uint32_t i = 0; i < queueCount; i++)
		{
			if (((queueProps[i].queueFlags & queueFlag) == queueFlag) && !(queueProps[i].queueFlags & notFlag))
			{
				if (surface && !vkPhysicalDevice_.getSurfaceSupportKHR(i, surface))
				{
					continue;
				}
				return i;
			}
		}
		return kQueueIndexNotFound;
	}

	//----
	uint32_t Device::GetMemoryTypeIndex(uint32_t bits, const vk::MemoryPropertyFlags& properties)
	{
		uint32_t result = 0;
		vk::PhysicalDeviceMemoryProperties deviceMemoryProperties = vkPhysicalDevice_.getMemoryProperties();
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

	//----
	// コンテキスト作成
	bool Device::InitializeContext(HINSTANCE hInst, HWND hWnd, uint16_t screenWidth, uint16_t screenHeight)
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
			vkInstance_ = vk::createInstance(createInfo);
			if (!vkInstance_)
			{
				return false;
			}
		}

		// 物理デバイス
		vkPhysicalDevice_ = vkInstance_.enumeratePhysicalDevices()[0];
		{
			struct Version {
				uint32_t patch : 12;
				uint32_t minor : 10;
				uint32_t major : 10;
			} _version;

			vk::PhysicalDeviceProperties deviceProperties = vkPhysicalDevice_.getProperties();
			memcpy(&_version, &deviceProperties.apiVersion, sizeof(uint32_t));
			vk::PhysicalDeviceFeatures deviceFeatures = vkPhysicalDevice_.getFeatures();
			vk::PhysicalDeviceMemoryProperties deviceMemoryProperties = vkPhysicalDevice_.getMemoryProperties();

			// プロパティやFeatureの確認はここで行う
		}

		// Vulkan device
		uint32_t graphicsQueueIndex = 0;
		uint32_t computeQueueIndex = 0;
		{
			// グラフィクス用のキューを検索する
			// NOTE: Computeも可能なキューを検索
			graphicsQueueIndex = FindQueue(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute);
			if (graphicsQueueIndex == kQueueIndexNotFound)
			{
				return false;
			}
			computeQueueIndex = FindQueue(vk::QueueFlagBits::eCompute, vk::QueueFlagBits::eGraphics);
			computeQueueIndex = (computeQueueIndex == kQueueIndexNotFound) ? graphicsQueueIndex : computeQueueIndex;

			float queuePriorities[] = { 0.5f, 0.3f };
			float computeQueuePriorities[] = { 0.3f };
			std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
			{
				vk::DeviceQueueCreateInfo info;
				info.queueFamilyIndex = graphicsQueueIndex;
				info.queueCount = (computeQueueIndex == graphicsQueueIndex) ? 2 : 1;
				info.pQueuePriorities = queuePriorities;
				queueCreateInfos.push_back(info);
			}
			if (computeQueueIndex != graphicsQueueIndex)
			{
				vk::DeviceQueueCreateInfo info;
				info.queueFamilyIndex = computeQueueIndex;
				info.queueCount = 1;
				info.pQueuePriorities = computeQueuePriorities;
				queueCreateInfos.push_back(info);
			}

			vk::PhysicalDeviceFeatures deviceFeatures = vkPhysicalDevice_.getFeatures();

			const char* enabledExtensions[] = {
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if defined(_DEBUG)
				//VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
#endif
			};
			vk::DeviceCreateInfo deviceCreateInfo;
			deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
			deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
			deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
			deviceCreateInfo.enabledExtensionCount = (uint32_t)ARRAYSIZE(enabledExtensions);
			deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions;
			deviceCreateInfo.enabledLayerCount = ARRAYSIZE(kDebugLayerNames);
			deviceCreateInfo.ppEnabledLayerNames = kDebugLayerNames;

			vkDevice_ = vkPhysicalDevice_.createDevice(deviceCreateInfo);
		}

#if defined(_DEBUG)
		// デバッグ用コールバック設定
		{
			g_fCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(vkInstance_, "vkCreateDebugReportCallbackEXT");
			g_fDestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(vkInstance_, "vkDestroyDebugReportCallbackEXT");
			g_fDebugBreakCallback = (PFN_vkDebugReportMessageEXT)vkGetInstanceProcAddr(vkInstance_, "vkDebugReportMessageEXT");

			VkDebugReportCallbackCreateInfoEXT dbgCreateInfo = {};
			vk::DebugReportFlagsEXT flags = vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning;
			dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
			dbgCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)DebugMessageCallback;
			dbgCreateInfo.flags = flags.operator VkSubpassDescriptionFlags();

			VkResult err = g_fCreateDebugReportCallback(vkInstance_, &dbgCreateInfo, nullptr, &g_fMsgCallback);
			assert(!err);
		}
#endif

		vkPipelineCache_ = vkDevice_.createPipelineCache(vk::PipelineCacheCreateInfo());
		vkQueue_ = vkDevice_.getQueue(graphicsQueueIndex, 0);
		vkComputeQueue_ = vkDevice_.getQueue(computeQueueIndex, (computeQueueIndex == graphicsQueueIndex) ? 1 : 0);

		// コマンドプール作成
		vk::CommandPoolCreateInfo cmdPoolInfo;
		cmdPoolInfo.queueFamilyIndex = graphicsQueueIndex;
		cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		vkCmdPool_ = vkDevice_.createCommandPool(cmdPoolInfo);

		cmdPoolInfo.queueFamilyIndex = computeQueueIndex;
		vkComputeCmdPool_ = vkDevice_.createCommandPool(cmdPoolInfo);

		// スワップチェイン初期化
		if (!vkSwapchain_.Initialize(*this, hInst, hWnd))
		{
			return false;
		}
		if (!vkSwapchain_.InitializeSwapchain(screenWidth, screenHeight, false))
		{
			return false;
		}

		// セマフォ設定
		{
			vk::SemaphoreCreateInfo semaphoreCreateInfo;

			// Presentの完了を確認するため
			vkPresentComplete_ = vkDevice_.createSemaphore(semaphoreCreateInfo);

			// 描画コマンドの処理完了を確認するため
			vkRenderComplete_ = vkDevice_.createSemaphore(semaphoreCreateInfo);

			if (!vkPresentComplete_ || !vkRenderComplete_)
			{
				return false;
			}
		}

		// コマンドバッファ作成
		{
			vk::CommandBufferAllocateInfo allocInfo;
			allocInfo.commandPool = vkCmdPool_;
			allocInfo.commandBufferCount = vkSwapchain_.GetImageCount();
			vkCmdBuffers_ = vkDevice_.allocateCommandBuffers(allocInfo);

			allocInfo.commandPool = vkComputeCmdPool_;
			vkComputeCmdBuffers_ = vkDevice_.allocateCommandBuffers(allocInfo);
		}

		return true;
	}

	//----
	// コンテキスト破棄
	void Device::DestroyContext()
	{
		// Idle状態になるのを待つ
		vkComputeQueue_.waitIdle();
		vkQueue_.waitIdle();
		vkDevice_.waitIdle();

		vkDevice_.freeCommandBuffers(vkComputeCmdPool_, vkComputeCmdBuffers_);
		vkDevice_.freeCommandBuffers(vkCmdPool_, vkCmdBuffers_);
		vkDevice_.destroySemaphore(vkPresentComplete_);
		vkDevice_.destroySemaphore(vkRenderComplete_);

		vkSwapchain_.Destroy();

		vkDevice_.destroyCommandPool(vkComputeCmdPool_);
		vkDevice_.destroyCommandPool(vkCmdPool_);
		vkDevice_.destroyPipelineCache(vkPipelineCache_);
		vkDevice_.destroy();
#if defined(_DEBUG)
		g_fDestroyDebugReportCallback(vkInstance_, g_fMsgCallback, nullptr);
#endif
		vkInstance_.destroy();
	}

	//----
	vk::CommandBuffer& Device::BeginMainCommandBuffer()
	{
		auto& cmdBuffer = GetCurrentCommandBuffer();

		cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
		vk::CommandBufferBeginInfo cmdBufInfo;
		cmdBuffer.begin(cmdBufInfo);

		return cmdBuffer;
	}

	//----
	void Device::ReadyPresentAndEndMainCommandBuffer()
	{
		auto& cmdBuffer = GetCurrentCommandBuffer();

		// スワップチェインの現在のイメージをPresent用のレイアウトに変更
		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		Image::SetImageLayout(
			cmdBuffer,
			GetCurrentSwapchainImage(),
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			subresourceRange);

		// コマンドバッファを終了
		cmdBuffer.end();
	}

	//----
	void Device::SubmitAndPresent(uint32_t waitSemaphoreCount, vk::Semaphore* pWaitSemaphores, vk::PipelineStageFlags* pWaitStages, uint32_t signalSemaphoreCount, vk::Semaphore* pSignalSemaphores)
	{
		// Submit
		{
			std::vector<vk::Semaphore> waitSem(1), signalSem(1);
			std::vector<vk::PipelineStageFlags> waitFlags(1);
			waitSem[0] = vkPresentComplete_;
			signalSem[0] = vkRenderComplete_;
			waitFlags[0] = vk::PipelineStageFlagBits::eBottomOfPipe;

			assert(!((waitSemaphoreCount > 0) && (pWaitSemaphores == nullptr)));
			assert(!((waitSemaphoreCount > 0) && (pWaitStages == nullptr)));
			for (uint32_t i = 0; i < waitSemaphoreCount; i++)
			{
				waitSem.push_back(pWaitSemaphores[i]);
				waitFlags.push_back(pWaitStages[i]);
			}
			assert(!((signalSemaphoreCount > 0) && (pSignalSemaphores == nullptr)));
			for (uint32_t i = 0; i < signalSemaphoreCount; i++)
			{
				signalSem.push_back(pSignalSemaphores[i]);
			}

			vk::SubmitInfo submitInfo;
			// 待つ必要があるセマフォの数とその配列を渡す
			submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSem.size());
			submitInfo.pWaitSemaphores = waitSem.data();
			submitInfo.pWaitDstStageMask = waitFlags.data();
			// Submitするコマンドバッファの配列を渡す
			// 複数のコマンドバッファをSubmitしたい場合は配列にする
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &GetCurrentCommandBuffer();
			// 描画完了を知らせるセマフォを登録する
			submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSem.size());
			submitInfo.pSignalSemaphores = signalSem.data();

			// Queueに対してSubmitする
			vk::Fence fence = vkSwapchain_.GetSubmitFence(true);
			vkQueue_.submit(submitInfo, fence);
			vk::Result fenceRes = vkDevice_.waitForFences(fence, VK_TRUE, kFenceTimeout);
			assert(fenceRes == vk::Result::eSuccess);
		}

		// Present
		vkSwapchain_.Present(vkRenderComplete_);
	}

}	// namespace vsl


//	EOF
