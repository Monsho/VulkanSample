#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <sstream>
#include <cmath>
#include "targa.h"


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
	vk::RenderPass					g_VkRenderPass;
	std::vector<vk::Framebuffer>	g_VkFramebuffers;
	vk::Semaphore					g_VkPresentComplete;
	vk::Semaphore					g_VkRenderComplete;
	std::vector<vk::CommandBuffer>	g_VkCmdBuffers;

	bool							g_CloseRequest = false;

	struct DepthStencilBuffer
	{
		vk::Image			image;
		vk::DeviceMemory	devMem;
		vk::ImageView		view;
	};
	struct BufferResource
	{
		vk::Buffer			buffer;
		vk::DeviceMemory	devMem;
	};
	struct ImageResource
	{
		vk::Image			image;
		vk::DeviceMemory	devMem;
		vk::Sampler			sampler;
		vk::ImageView		view;
		vk::Format			format;
	};
	DepthStencilBuffer									g_VkDepthBuffer;
	BufferResource										g_VkVBuffer, g_VkIBuffer;
	vk::PipelineVertexInputStateCreateInfo				g_VkVertexInputState;
	std::vector<vk::VertexInputBindingDescription>		g_VkBindDescs;
	std::vector<vk::VertexInputAttributeDescription>	g_VkAttribDescs;
	ImageResource										g_VkTexture;

	struct SceneData
	{
		glm::mat4x4	mtxView;
		glm::mat4x4	mtxProj;
	};
	struct MeshData
	{
		glm::mat4x4	mtxWorld;
	};
	BufferResource						g_VkUniformBuffer;
	vk::DescriptorBufferInfo			g_VkUniformInfo;

	vk::DescriptorPool					g_VkDescPool;
	vk::DescriptorSetLayout				g_VkDescSetLayout;
	std::vector<vk::DescriptorSet>		g_VkDescSets;

	vk::ShaderModule					g_VkVShader, g_VkPShader;

	vk::PipelineLayout					g_VkPipeLayout;
	vk::Pipeline						g_VkSamplePipeline;

#if defined(_DEBUG)
	PFN_vkCreateDebugReportCallbackEXT	g_fCreateDebugReportCallback = VK_NULL_HANDLE;
	PFN_vkDestroyDebugReportCallbackEXT	g_fDestroyDebugReportCallback = VK_NULL_HANDLE;
	PFN_vkDebugReportMessageEXT			g_fDebugBreakCallback = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT			g_fMsgCallback;
#endif

	// �Ώۃt���O�̃L���[�C���f�b�N�X���擾����
	static const uint32_t	kQueueIndexNotFound = 0xffffffff;
	uint32_t FindQueue(vk::QueueFlags queueFlag, const vk::SurfaceKHR& surface = vk::SurfaceKHR())
	{
		std::vector<vk::QueueFamilyProperties> queueProps = g_VkPhysicalDevice.getQueueFamilyProperties();
		size_t queueCount = queueProps.size();
		for (uint32_t i = 0; i < queueCount; i++)
		{
			if (queueProps[i].queueFlags & queueFlag)
			{
				if (surface && !g_VkPhysicalDevice.getSurfaceSupportKHR(i, surface))
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

	vk::ShaderModule CreateShaderModule(const std::string& filename)
	{
		// �t�@�C�����[�h
		std::vector<uint8_t> bin;
		FILE* fp = nullptr;
		if (fopen_s(&fp, filename.c_str(), "rb") != 0)
		{
			assert(!"Do NOT read shader file.\n");
			return vk::ShaderModule();
		}
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		bin.resize(size);
		fread(bin.data(), size, 1, fp);
		fclose(fp);

		// Shader���W���[���쐬
		vk::ShaderModuleCreateInfo moduleCreateInfo;
		moduleCreateInfo.codeSize = size;
		moduleCreateInfo.pCode = reinterpret_cast<uint32_t*>(bin.data());
		return g_VkDevice.createShaderModule(moduleCreateInfo);
	}

	// �C���[�W���C�A�E�g��ݒ肷�邽�߁A�o���A��\��
	void SetImageLayout(
		vk::CommandBuffer cmdbuffer,
		vk::Image image,
		vk::ImageLayout oldImageLayout,
		vk::ImageLayout newImageLayout,
		vk::ImageSubresourceRange subresourceRange)
	{
		// �C���[�W�o���A�I�u�W�F�N�g�ݒ�
		vk::ImageMemoryBarrier imageMemoryBarrier;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;

		// ���݂̃��C�A�E�g

		// �C���[�W�������̏��
		// �ȍ~�A���̏�Ԃɖ߂邱�Ƃ͂Ȃ�
		if (oldImageLayout == vk::ImageLayout::ePreinitialized)						imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
		else if (oldImageLayout == vk::ImageLayout::eTransferDstOptimal)			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		// �J���[
		else if (oldImageLayout == vk::ImageLayout::eColorAttachmentOptimal)		imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		// �[�x�E�X�e���V��
		else if (oldImageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)	imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		// �]����
		else if (oldImageLayout == vk::ImageLayout::eTransferSrcOptimal)			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		// �V�F�[�_���\�[�X
		else if (oldImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal)			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;

		// ���̃��C�A�E�g

		// �]����
		if (newImageLayout == vk::ImageLayout::eTransferDstOptimal)					imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		// �]����
		else if (newImageLayout == vk::ImageLayout::eTransferSrcOptimal)			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;// | imageMemoryBarrier.srcAccessMask;
		// �J���[
		else if (newImageLayout == vk::ImageLayout::eColorAttachmentOptimal)		imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		// �[�x�E�X�e���V��
		else if (newImageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)	imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		// �V�F�[�_���\�[�X
		else if (newImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal)			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		// Present
		else if (newImageLayout == vk::ImageLayout::ePresentSrcKHR)					imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;

		// Put barrier on top
		// Put barrier inside setup command buffer
		cmdbuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::DependencyFlags(),
			nullptr, nullptr, imageMemoryBarrier);
	}

	// �X���b�v�`�F�C��
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

			// �T�|�[�g����Ă���t�H�[�}�b�g���擾
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

			// Present���[�h��I������
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

			// �C���[�W�������肷��
			uint32_t desiredNumSwapchainImages = surfaceCaps.minImageCount + 1;
			if ((surfaceCaps.maxImageCount > 0) && (desiredNumSwapchainImages > surfaceCaps.maxImageCount))
			{
				desiredNumSwapchainImages = surfaceCaps.maxImageCount;
			}

			// �T�[�t�F�C�X�̃g�����X�t�H�[��������
			// �ʏ��Identity�ł����͂������A�X�}�z�Ƃ����Ƃ���ȊO������̂����H
			vk::SurfaceTransformFlagBitsKHR preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
			if (!(surfaceCaps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity))
			{
				preTransform = surfaceCaps.currentTransform;
			}

			// Swapchain�̐���
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

			// �O���Swapchain���c���Ă���ꍇ�͍폜
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

			// Swapchain�̊e�C���[�W�ɑ΂���View��Fence���쐬
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
				// Fence���L���ȊԂ͊�������܂ő҂�
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


// �f�o�b�O���̃��b�Z�[�W�R�[���o�b�N
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

	// �f�o�b�O�E�B���h�E�ɂ��o��
	OutputDebugStringA(message.c_str());
	OutputDebugStringA("\n");

	return false;
}

// �R���e�L�X�g�쐬
bool InitializeContext()
{
	// Vulkan�C���X�^���X���쐬
	{
		vk::ApplicationInfo appInfo;
		appInfo.pApplicationName = "VulkanSample";
		appInfo.pEngineName = "VulkanSample";
		appInfo.apiVersion = VK_API_VERSION_1_0;

		// Extension
		const char* extensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,			// Windows�pExtension
#if defined(_DEBUG)
			VK_EXT_DEBUG_REPORT_EXTENSION_NAME,				// �f�o�b�O���|�[�g�pExtension
#endif
		};

		// �C���X�^���X�������
		vk::InstanceCreateInfo createInfo;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = ARRAYSIZE(extensions);
		createInfo.ppEnabledExtensionNames = extensions;
#if defined(_DEBUG)
		// �f�o�b�O�֘A
		//createInfo.enabledLayerCount = ARRAYSIZE(kDebugLayerNames);
		//createInfo.ppEnabledLayerNames = kDebugLayerNames;
#endif

		// �C���X�^���X����
		g_VkInstance = vk::createInstance(createInfo);
		if (!g_VkInstance)
		{
			return false;
		}
	}

	// �����f�o�C�X
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

		// �v���p�e�B��Feature�̊m�F�͂����ōs��
	}

	// Vulkan device
	uint32_t graphicsQueueIndex = 0;
	{
		// �O���t�B�N�X�p�̃L���[����������
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
	// �f�o�b�O�p�R�[���o�b�N�ݒ�
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

	// �R�}���h�v�[���쐬
	vk::CommandPoolCreateInfo cmdPoolInfo;
	cmdPoolInfo.queueFamilyIndex = graphicsQueueIndex;
	cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	g_VkCmdPool = g_VkDevice.createCommandPool(cmdPoolInfo);

	return true;
}

// �R���e�L�X�g�j��
void DestroyContext()
{
	// Idle��ԂɂȂ�̂�҂�
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
		// TODO: �L�[���͂ɑ΂��鏈��
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
		// TODO: �}�E�X����ɑ΂��鏈��
		return 0;
	}

	case WM_MOUSEMOVE:
	{
		// TODO: �}�E�X�ړ��ɑ΂��鏈��
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
		// TODO: �T�C�Y�ύX���̏���
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
			// TODO: �I�����N�G�X�g
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
	// RenderPass�ݒ�
	std::array<vk::AttachmentDescription, 2> attachmentDescs;
	std::array<vk::AttachmentReference, 2> attachmentRefs;

	// �J���[�o�b�t�@�̃A�^�b�`�����g�ݒ�
	attachmentDescs[0].format = g_VkSwapchain.GetFormat();
	attachmentDescs[0].loadOp = vk::AttachmentLoadOp::eDontCare;
	attachmentDescs[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachmentDescs[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
	attachmentDescs[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	// �[�x�o�b�t�@�̃A�^�b�`�����g�ݒ�
	attachmentDescs[1].format = vk::Format::eD32SfloatS8Uint;
	attachmentDescs[1].loadOp = vk::AttachmentLoadOp::eDontCare;
	attachmentDescs[1].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachmentDescs[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	attachmentDescs[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	// �J���[�o�b�t�@�̃��t�@�����X�ݒ�
	vk::AttachmentReference& colorReference = attachmentRefs[0];
	colorReference.attachment = 0;
	colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

	// �[�x�o�b�t�@�̃��t�@�����X�ݒ�
	vk::AttachmentReference& depthReference = attachmentRefs[1];
	depthReference.attachment = 1;
	depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	std::array<vk::SubpassDescription, 1> subpasses;
	{
		vk::SubpassDescription& subpass = subpasses[0];
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &attachmentRefs[0];
		subpass.pDepthStencilAttachment = &attachmentRefs[1];
	}

	std::array<vk::SubpassDependency, 1> subpassDepends;
	{
		vk::SubpassDependency& dependency = subpassDepends[0];
		dependency.srcSubpass = 0;
		dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
		dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;
		dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	}

	// RenderPass����
	{
		vk::RenderPassCreateInfo renderPassInfo;
		renderPassInfo.attachmentCount = (uint32_t)attachmentDescs.size();
		renderPassInfo.pAttachments = attachmentDescs.data();
		renderPassInfo.subpassCount = (uint32_t)subpasses.size();
		renderPassInfo.pSubpasses = subpasses.data();
		renderPassInfo.dependencyCount = (uint32_t)subpassDepends.size();
		renderPassInfo.pDependencies = subpassDepends.data();
		g_VkRenderPass = g_VkDevice.createRenderPass(renderPassInfo);
		if (!g_VkRenderPass)
		{
			return false;
		}
	}

	// �Z�}�t�H�ݒ�
	{
		vk::SemaphoreCreateInfo semaphoreCreateInfo;

		// Present�̊������m�F���邽��
		g_VkPresentComplete = g_VkDevice.createSemaphore(semaphoreCreateInfo);

		// �`��R�}���h�̏����������m�F���邽��
		g_VkRenderComplete = g_VkDevice.createSemaphore(semaphoreCreateInfo);

		if (!g_VkPresentComplete || !g_VkRenderComplete)
		{
			return false;
		}
	}

	// �R�}���h�o�b�t�@�쐬
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

	g_VkDevice.destroyRenderPass(g_VkRenderPass);
}

bool InitializeRenderResource()
{
	// �[�x�o�b�t�@����
	{
		// �w��̃t�H�[�}�b�g���T�|�[�g����Ă��邩���ׂ�
		vk::Format depthFormat = vk::Format::eD32SfloatS8Uint;
		vk::FormatProperties formatProps = g_VkPhysicalDevice.getFormatProperties(depthFormat);
		assert(formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment);

		vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

		// �[�x�o�b�t�@�̃C���[�W���쐬����
		vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.extent = vk::Extent3D(kScreenWidth, kScreenHeight, 1);
		imageCreateInfo.format = depthFormat;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
		g_VkDepthBuffer.image = g_VkDevice.createImage(imageCreateInfo);

		// �[�x�o�b�t�@�p�̃��������m��
		vk::MemoryRequirements memReqs = g_VkDevice.getImageMemoryRequirements(g_VkDepthBuffer.image);
		vk::MemoryAllocateInfo memAlloc;
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		g_VkDepthBuffer.devMem = g_VkDevice.allocateMemory(memAlloc);
		g_VkDevice.bindImageMemory(g_VkDepthBuffer.image, g_VkDepthBuffer.devMem, 0);

		// View���쐬
		vk::ImageViewCreateInfo viewCreateInfo;
		viewCreateInfo.viewType = vk::ImageViewType::e2D;
		viewCreateInfo.format = depthFormat;
		viewCreateInfo.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		viewCreateInfo.subresourceRange.aspectMask = aspect;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.layerCount = 1;
		viewCreateInfo.image = g_VkDepthBuffer.image;
		g_VkDepthBuffer.view = g_VkDevice.createImageView(viewCreateInfo);

		// ���C�A�E�g�ύX�̃R�}���h�𔭍s����
		{
			vk::CommandBufferBeginInfo cmdBufferBeginInfo;
			vk::BufferCopy copyRegion;

			// �R�}���h�ςݍ��݊J�n
			g_VkCmdBuffers[0].begin(cmdBufferBeginInfo);

			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = aspect;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;
			SetImageLayout(
				g_VkCmdBuffers[0],
				g_VkDepthBuffer.image,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				subresourceRange);

			// �R�}���h�ς݂��ݏI��
			g_VkCmdBuffers[0].end();

			// �R�}���h��Submit���ďI����҂�
			vk::SubmitInfo copySubmitInfo;
			copySubmitInfo.commandBufferCount = 1;
			copySubmitInfo.pCommandBuffers = &g_VkCmdBuffers[0];

			g_VkQueue.submit(copySubmitInfo, VK_NULL_HANDLE);
			g_VkQueue.waitIdle();
		}
	}

	// �t���[���o�b�t�@�ݒ�
	{
		std::array<vk::ImageView, 2> views;
		views[1] = g_VkDepthBuffer.view;

		vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo.renderPass = g_VkRenderPass;
		framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(views.size());
		framebufferCreateInfo.pAttachments = views.data();
		framebufferCreateInfo.width = kScreenWidth;
		framebufferCreateInfo.height = kScreenHeight;
		framebufferCreateInfo.layers = 1;

		// Swapchain����t���[���o�b�t�@�𐶐�����
		g_VkFramebuffers = g_VkSwapchain.CreateFramebuffers(framebufferCreateInfo);
	}

	// ���_�E�C���f�b�N�X�o�b�t�@����
	{
		struct Vertex
		{
			float	pos[3];
			float	color[4];
			float	uv[2];
		};
		Vertex vertexData[] = {
			{ { -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
			{ {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
			{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
			{ {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } }
		};
		uint32_t indexData[] = { 0, 1, 2, 1, 3, 2 };

		// �o�b�t�@�R�s�[�p�̃R�}���h�o�b�t�@�𐶐�����
		vk::CommandBufferAllocateInfo cmdBufInfo;
		cmdBufInfo.commandPool = g_VkCmdPool;
		cmdBufInfo.level = vk::CommandBufferLevel::ePrimary;
		cmdBufInfo.commandBufferCount = 1;
		vk::CommandBuffer copyCommandBuffer = g_VkDevice.allocateCommandBuffers(cmdBufInfo)[0];

		BufferResource stagingVBuffer;
		{
			// �V�X�e����������ɃR�s�[���̒��_�o�b�t�@�𐶐�����
			vk::BufferCreateInfo vertexBufferInfo;
			vertexBufferInfo.size = sizeof(vertexData);
			vertexBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;	// ���̃o�b�t�@�̓R�s�[���Ƃ��ė��p����
			stagingVBuffer.buffer = g_VkDevice.createBuffer(vertexBufferInfo);

			vk::MemoryRequirements memReqs = g_VkDevice.getBufferMemoryRequirements(stagingVBuffer.buffer);
			vk::MemoryAllocateInfo memAlloc;
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
			stagingVBuffer.devMem = g_VkDevice.allocateMemory(memAlloc);

			void* data = g_VkDevice.mapMemory(stagingVBuffer.devMem, 0, sizeof(vertexData), vk::MemoryMapFlags());
			memcpy(data, vertexData, sizeof(vertexData));
			g_VkDevice.unmapMemory(stagingVBuffer.devMem);
			g_VkDevice.bindBufferMemory(stagingVBuffer.buffer, stagingVBuffer.devMem, 0);

			// VRAM��ɃR�s�[��̒��_�o�b�t�@�𐶐�����
			vertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;	// �R�s�[��A���_�o�b�t�@�Ƃ��ė��p����
			g_VkVBuffer.buffer = g_VkDevice.createBuffer(vertexBufferInfo);
			memReqs = g_VkDevice.getBufferMemoryRequirements(g_VkVBuffer.buffer);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
			g_VkVBuffer.devMem = g_VkDevice.allocateMemory(memAlloc);
			g_VkDevice.bindBufferMemory(g_VkVBuffer.buffer, g_VkVBuffer.devMem, 0);
		}

		BufferResource stagingIBuffer;
		{
			// �V�X�e����������ɃR�s�[���̃C���f�b�N�X�o�b�t�@�𐶐�����
			vk::BufferCreateInfo indexbufferInfo;
			indexbufferInfo.size = sizeof(indexData);
			indexbufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			stagingIBuffer.buffer = g_VkDevice.createBuffer(indexbufferInfo);

			vk::MemoryRequirements memReqs = g_VkDevice.getBufferMemoryRequirements(stagingIBuffer.buffer);
			vk::MemoryAllocateInfo memAlloc;
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
			stagingIBuffer.devMem = g_VkDevice.allocateMemory(memAlloc);

			void* data = g_VkDevice.mapMemory(stagingIBuffer.devMem, 0, sizeof(indexData), vk::MemoryMapFlags());
			memcpy(data, indexData, sizeof(indexData));
			g_VkDevice.unmapMemory(stagingIBuffer.devMem);
			g_VkDevice.bindBufferMemory(stagingIBuffer.buffer, stagingIBuffer.devMem, 0);

			// VRAM��ɃR�s�[��̃C���f�b�N�X�o�b�t�@�𐶐�����
			indexbufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
			g_VkIBuffer.buffer = g_VkDevice.createBuffer(indexbufferInfo);
			memReqs = g_VkDevice.getBufferMemoryRequirements(g_VkIBuffer.buffer);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
			g_VkIBuffer.devMem = g_VkDevice.allocateMemory(memAlloc);
			g_VkDevice.bindBufferMemory(g_VkIBuffer.buffer, g_VkIBuffer.devMem, 0);
		}

		// �������R�s�[�̃R�}���h�𔭍s���A�I����҂�
		{
			vk::CommandBufferBeginInfo cmdBufferBeginInfo;
			vk::BufferCopy copyRegion;

			// �R�}���h�ςݍ��݊J�n
			copyCommandBuffer.begin(cmdBufferBeginInfo);

			// ���_�o�b�t�@
			copyRegion.size = sizeof(vertexData);
			copyCommandBuffer.copyBuffer(stagingVBuffer.buffer, g_VkVBuffer.buffer, copyRegion);
			// �C���f�b�N�X�o�b�t�@
			copyRegion.size = sizeof(indexData);
			copyCommandBuffer.copyBuffer(stagingIBuffer.buffer, g_VkIBuffer.buffer, copyRegion);

			// �R�}���h�ς݂��ݏI��
			copyCommandBuffer.end();

			// �R�}���h��Submit���ďI����҂�
			vk::SubmitInfo copySubmitInfo;
			copySubmitInfo.commandBufferCount = 1;
			copySubmitInfo.pCommandBuffers = &copyCommandBuffer;

			g_VkQueue.submit(copySubmitInfo, VK_NULL_HANDLE);
			g_VkQueue.waitIdle();

			// �s�v�ɂȂ����o�b�t�@���폜����
			g_VkDevice.freeCommandBuffers(g_VkCmdPool, copyCommandBuffer);
			g_VkDevice.destroyBuffer(stagingVBuffer.buffer);
			g_VkDevice.freeMemory(stagingVBuffer.devMem);
			g_VkDevice.destroyBuffer(stagingIBuffer.buffer);
			g_VkDevice.freeMemory(stagingIBuffer.devMem);
		}

		// ���_�o�b�t�@�̃o�C���h�f�X�N���v�V������ݒ�
		g_VkBindDescs.resize(1);
		g_VkBindDescs[0].binding = 0;		// 0�Ԃփo�C���h
		g_VkBindDescs[0].stride = sizeof(Vertex);
		g_VkBindDescs[0].inputRate = vk::VertexInputRate::eVertex;

		// ���_�A�g���r���[�g�̃f�X�N���v�V������ݒ�
		// ���_�̃��������C�A�E�g���w�肷��
		g_VkAttribDescs.resize(3);
		// Position
		g_VkAttribDescs[0].binding = 0;
		g_VkAttribDescs[0].location = 0;
		g_VkAttribDescs[0].format = vk::Format::eR32G32B32Sfloat;
		g_VkAttribDescs[0].offset = 0;
		// Color
		g_VkAttribDescs[1].binding = 0;
		g_VkAttribDescs[1].location = 1;
		g_VkAttribDescs[1].format = vk::Format::eR32G32B32A32Sfloat;
		g_VkAttribDescs[1].offset = sizeof(float) * 3;
		// UV
		g_VkAttribDescs[2].binding = 0;
		g_VkAttribDescs[2].location = 2;
		g_VkAttribDescs[2].format = vk::Format::eR32G32Sfloat;
		g_VkAttribDescs[2].offset = sizeof(float) * (3 + 4);

		// ���̓X�e�[�g�Ɋe����o�^
		g_VkVertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(g_VkBindDescs.size());
		g_VkVertexInputState.pVertexBindingDescriptions = g_VkBindDescs.data();
		g_VkVertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(g_VkAttribDescs.size());
		g_VkVertexInputState.pVertexAttributeDescriptions = g_VkAttribDescs.data();
	}

	// UniformBuffer�𐶐�����
	{
		// UniformBuffer�𐶐�
		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = sizeof(SceneData);
		bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst;
		g_VkUniformBuffer.buffer = g_VkDevice.createBuffer(bufferInfo);

		// UniformBuffer�p�̃��������m�ۂ��A�o�C���h
		vk::MemoryRequirements memReqs = g_VkDevice.getBufferMemoryRequirements(g_VkUniformBuffer.buffer);
		vk::MemoryAllocateInfo allocInfo;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
		g_VkUniformBuffer.devMem = g_VkDevice.allocateMemory(allocInfo);
		g_VkDevice.bindBufferMemory(g_VkUniformBuffer.buffer, g_VkUniformBuffer.devMem, 0);

		// UniformBuffer�̏����i�[����
		g_VkUniformInfo.buffer = g_VkUniformBuffer.buffer;
		g_VkUniformInfo.offset = 0;
		g_VkUniformInfo.range = sizeof(SceneData);

		// �s��͒P�ʍs���˂�����ł���
		SceneData data;
		data.mtxView = glm::mat4x4();
		data.mtxProj = glm::mat4x4();

		// �o�b�t�@�ɏ����R�s�[
		void *pData = g_VkDevice.mapMemory(g_VkUniformBuffer.devMem, 0, sizeof(SceneData), vk::MemoryMapFlags());
		memcpy(pData, &data, sizeof(data));
		g_VkDevice.unmapMemory(g_VkUniformBuffer.devMem);
	}

	// �e�N�X�`����ǂݍ���
	{
		tga_image image;
		if (tga_read(&image, "data/icon.tga") != TGA_NOERR)
		{
			return false;
		}

		// Staging�o�b�t�@�쐬
		BufferResource staging;
		{
			vk::BufferCreateInfo stagingCreateInfo;
			stagingCreateInfo.size = image.width * image.height * image.pixel_depth / 8;
			stagingCreateInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;	// ���̃o�b�t�@�̓R�s�[���Ƃ��ė��p����
			staging.buffer = g_VkDevice.createBuffer(stagingCreateInfo);

			// Staging�o�b�t�@�p�������쐬
			vk::MemoryRequirements memReqs = g_VkDevice.getBufferMemoryRequirements(staging.buffer);
			vk::MemoryAllocateInfo memAlloc;
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
			staging.devMem = g_VkDevice.allocateMemory(memAlloc);
			void* data = g_VkDevice.mapMemory(staging.devMem, 0, stagingCreateInfo.size, vk::MemoryMapFlags());
			memcpy(data, image.image_data, stagingCreateInfo.size);
			g_VkDevice.unmapMemory(staging.devMem);
			g_VkDevice.bindBufferMemory(staging.buffer, staging.devMem, 0);
		}

		// �C���[�W����
		{
			g_VkTexture.format = vk::Format::eB8G8R8A8Unorm;
			//g_VkTexture.format = vk::Format::eR8G8B8A8Unorm;

			vk::ImageCreateInfo imageCreateInfo;
			imageCreateInfo.imageType = vk::ImageType::e2D;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.format = g_VkTexture.format;
			imageCreateInfo.extent = vk::Extent3D(image.width, image.height, 1);
			imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
			imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
			g_VkTexture.image = g_VkDevice.createImage(imageCreateInfo);
			assert(g_VkTexture.image);
			vk::MemoryRequirements memReqs = g_VkDevice.getImageMemoryRequirements(g_VkTexture.image);
			vk::MemoryAllocateInfo memAllocInfo;
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
			g_VkTexture.devMem = g_VkDevice.allocateMemory(memAllocInfo);
			g_VkDevice.bindImageMemory(g_VkTexture.image, g_VkTexture.devMem, 0);
		}

		// �R�s�[�R�}���h�𐶐��A���s
		{
			vk::CommandBufferAllocateInfo cmdBufInfo;
			cmdBufInfo.commandPool = g_VkCmdPool;
			cmdBufInfo.level = vk::CommandBufferLevel::ePrimary;
			cmdBufInfo.commandBufferCount = 1;
			vk::CommandBuffer copyCommandBuffer = g_VkDevice.allocateCommandBuffers(cmdBufInfo)[0];

			vk::CommandBufferBeginInfo cmdBufferBeginInfo;

			// �R�}���h�ςݍ��݊J�n
			copyCommandBuffer.begin(cmdBufferBeginInfo);

			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;

			// ���C�A�E�g�ݒ�
			SetImageLayout(
				copyCommandBuffer,
				g_VkTexture.image,
				vk::ImageLayout::ePreinitialized,
				vk::ImageLayout::eTransferDstOptimal,
				subresourceRange);

			// �R�s�[�R�}���h
			vk::BufferImageCopy bufferCopyRegion;
			bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.imageExtent.width = image.width;
			bufferCopyRegion.imageExtent.height = image.height;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.bufferOffset = 0;
			copyCommandBuffer.copyBufferToImage(staging.buffer, g_VkTexture.image, vk::ImageLayout::eTransferDstOptimal, 1, &bufferCopyRegion);

			// �R�s�[������Ƀ��C�A�E�g�ύX
			// �V�F�[�_����ǂݍ��߂��Ԃɂ��Ă���
			SetImageLayout(
				copyCommandBuffer,
				g_VkTexture.image,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				subresourceRange);

			// �R�}���h�ς݂��ݏI��
			copyCommandBuffer.end();

			// �R�}���h��Submit���ďI����҂�
			vk::SubmitInfo copySubmitInfo;
			copySubmitInfo.commandBufferCount = 1;
			copySubmitInfo.pCommandBuffers = &copyCommandBuffer;

			g_VkQueue.submit(copySubmitInfo, VK_NULL_HANDLE);
			g_VkQueue.waitIdle();

			// �s�v�ɂȂ����o�b�t�@���폜����
			g_VkDevice.freeCommandBuffers(g_VkCmdPool, copyCommandBuffer);
			g_VkDevice.destroyBuffer(staging.buffer);
			g_VkDevice.freeMemory(staging.devMem);
		}

		tga_free_buffers(&image);

		// �T���v���̍쐬
		{
			vk::SamplerCreateInfo samplerCreateInfo;
			samplerCreateInfo.magFilter = vk::Filter::eLinear;
			samplerCreateInfo.minFilter = vk::Filter::eLinear;
			samplerCreateInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
			samplerCreateInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
			samplerCreateInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
			samplerCreateInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
			samplerCreateInfo.mipLodBias = 0.0f;
			samplerCreateInfo.compareOp = vk::CompareOp::eNever;
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = 0.0f;
			samplerCreateInfo.maxAnisotropy = 8;
			samplerCreateInfo.anisotropyEnable = VK_TRUE;
			samplerCreateInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
			g_VkTexture.sampler = g_VkDevice.createSampler(samplerCreateInfo);
		}

		// View�̍쐬
		{
			vk::ImageViewCreateInfo viewCreateInfo;
			viewCreateInfo.viewType = vk::ImageViewType::e2D;
			viewCreateInfo.format = g_VkTexture.format;
			viewCreateInfo.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
			viewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			viewCreateInfo.subresourceRange.baseMipLevel = 0;
			viewCreateInfo.subresourceRange.baseArrayLayer = 0;
			viewCreateInfo.subresourceRange.layerCount = 1;
			viewCreateInfo.subresourceRange.levelCount = 1;
			viewCreateInfo.image = g_VkTexture.image;
			g_VkTexture.view = g_VkDevice.createImageView(viewCreateInfo);
		}
	}

	// �f�X�N���v�^�Z�b�g�𐶐�
	{
		// �f�X�N���v�^�v�[�����쐬����
		{
			// �����UniformBuffer1�̃f�X�N���v�^���K�v�Ȃ̂�PoolSize��1��OK
			// UniformBuffer�������ɂȂ��Ă�PoolSize��1�ł������A�^�C�v�̈Ⴄ�f�X�N���v�^���K�v�ȏꍇ��PoolSize�̐���������̂Œ���
			std::array<vk::DescriptorPoolSize, 2> typeCounts;
			typeCounts[0].type = vk::DescriptorType::eUniformBuffer;
			typeCounts[0].descriptorCount = 1;
			typeCounts[1].type = vk::DescriptorType::eCombinedImageSampler;
			typeCounts[1].descriptorCount = 1;

			// �f�X�N���v�^�v�[���𐶐�
			vk::DescriptorPoolCreateInfo descriptorPoolInfo;
			descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(typeCounts.size());
			descriptorPoolInfo.pPoolSizes = typeCounts.data();
			descriptorPoolInfo.maxSets = 1;
			g_VkDescPool = g_VkDevice.createDescriptorPool(descriptorPoolInfo);
		}

		// �f�X�N���v�^�Z�b�g���C�A�E�g���쐬����
		{
			// �`�掞�̃V�F�[�_�Z�b�g�ɑ΂���f�X�N���v�^�Z�b�g�̃��C�A�E�g���w�肷��
			// ���_�V�F�[�_�p��UniformBuffer
			vk::DescriptorSetLayoutBinding layoutBindings[2];
			// UniformBuffer for VertexShader
			layoutBindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
			layoutBindings[0].descriptorCount = 1;
			layoutBindings[0].binding = 0;
			layoutBindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
			layoutBindings[0].pImmutableSamplers = nullptr;
			// TextureSampler for PixelShader
			layoutBindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
			layoutBindings[1].descriptorCount = 1;
			layoutBindings[1].binding = 1;
			layoutBindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
			layoutBindings[1].pImmutableSamplers = nullptr;

			// ���C�A�E�g�𐶐�
			vk::DescriptorSetLayoutCreateInfo descriptorLayout;
			descriptorLayout.bindingCount = ARRAYSIZE(layoutBindings);
			descriptorLayout.pBindings = layoutBindings;
			g_VkDescSetLayout = g_VkDevice.createDescriptorSetLayout(descriptorLayout, nullptr);
		}

		// �f�X�N���v�^�Z�b�g���쐬����
		{
			vk::DescriptorImageInfo texDescInfo;
			texDescInfo.sampler = g_VkTexture.sampler;
			texDescInfo.imageView = g_VkTexture.view;
			texDescInfo.imageLayout = vk::ImageLayout::eGeneral;

			// �f�X�N���v�^�Z�b�g�͍쐬�ς݂̃f�X�N���v�^�v�[������m�ۂ���
			vk::DescriptorSetAllocateInfo allocInfo;
			allocInfo.descriptorPool = g_VkDescPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &g_VkDescSetLayout;
			g_VkDescSets = g_VkDevice.allocateDescriptorSets(allocInfo);

			// �f�X�N���v�^�Z�b�g�̏����X�V����
			std::array<vk::WriteDescriptorSet, 2> descSetInfos;
			descSetInfos[0].dstSet = g_VkDescSets[0];
			descSetInfos[0].descriptorCount = 1;
			descSetInfos[0].descriptorType = vk::DescriptorType::eUniformBuffer;
			descSetInfos[0].pBufferInfo = &g_VkUniformInfo;
			descSetInfos[0].dstBinding = 0;
			descSetInfos[1].dstSet = g_VkDescSets[0];
			descSetInfos[1].descriptorCount = 1;
			descSetInfos[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
			descSetInfos[1].pImageInfo = &texDescInfo;
			descSetInfos[1].dstBinding = 1;
			g_VkDevice.updateDescriptorSets(descSetInfos, nullptr);
		}
	}

	// �V�F�[�_��ǂݍ���
	{
		g_VkVShader = CreateShaderModule("data/test.vert.spv");
		g_VkPShader = CreateShaderModule("data/test.frag.spv");
		if (!g_VkVShader || !g_VkPShader)
		{
			return false;
		}
	}

	return true;
}

void DestroyRenderResource()
{
	g_VkDevice.destroyShaderModule(g_VkVShader);
	g_VkDevice.destroyShaderModule(g_VkPShader);

	g_VkDevice.destroyDescriptorSetLayout(g_VkDescSetLayout);
	g_VkDevice.destroyDescriptorPool(g_VkDescPool);

	g_VkDevice.destroyImageView(g_VkTexture.view);
	g_VkDevice.destroySampler(g_VkTexture.sampler);
	g_VkDevice.destroyImage(g_VkTexture.image);
	g_VkDevice.freeMemory(g_VkTexture.devMem);

	g_VkDevice.destroyBuffer(g_VkUniformBuffer.buffer);
	g_VkDevice.freeMemory(g_VkUniformBuffer.devMem);

	g_VkDevice.destroyBuffer(g_VkVBuffer.buffer);
	g_VkDevice.freeMemory(g_VkVBuffer.devMem);
	g_VkDevice.destroyBuffer(g_VkIBuffer.buffer);
	g_VkDevice.freeMemory(g_VkIBuffer.devMem);

	for (auto& fb : g_VkFramebuffers)
	{
		g_VkDevice.destroyFramebuffer(fb);
	}

	g_VkDevice.destroyImageView(g_VkDepthBuffer.view);
	g_VkDevice.destroyImage(g_VkDepthBuffer.image);
	g_VkDevice.freeMemory(g_VkDepthBuffer.devMem);
}

bool InitializePipeline()
{
	{
		// �����PushConstant�𗘗p����
		// �����ȃT�C�Y�̒萔�o�b�t�@�̓R�}���h�o�b�t�@�ɏ悹�ăV�F�[�_�ɑ��邱�Ƃ��\
		vk::PushConstantRange pushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(MeshData));

		// �f�X�N���v�^�Z�b�g���C�A�E�g�ɑΉ������p�C�v���C�����C�A�E�g�𐶐�����
		// �ʏ��1��1�Ő�������̂��ȁH
		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
		pPipelineLayoutCreateInfo.setLayoutCount = 1;
		pPipelineLayoutCreateInfo.pSetLayouts = &g_VkDescSetLayout;
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		g_VkPipeLayout = g_VkDevice.createPipelineLayout(pPipelineLayoutCreateInfo);
	}

	// �O���t�B�N�X�p�C�v���C���𐶐�����
	// OpenGL��DX11��1�̃X�e�[�g�}�V�������ׂẴO���t�B�N�X�p�C�v���C���Ŏg���񂷌`���̗p���Ă���
	// �`��ɕK�v�Ȑݒ���s���p�C�v���C���̊Ǘ����e���C�u�����ɔC����Ă������
	// Vulkan��DX12�͂�������[�U�����Ǘ�����悤�ɂȂ���
	// �܂�A�`��ɕK�v�Ȋe��ݒ�͌Œ�̃p�C�v���C���I�u�W�F�N�g�Ƃ��Đ������A�����؂�ւ��邱�Ƃō����ɃX�e�[�g�ύX���\�ɂ��Ă���

	// �`��g�|���W�̐ݒ�
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
	inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

	// ���X�^���C�Y�X�e�[�g�̐ݒ�
	vk::PipelineRasterizationStateCreateInfo rasterizationState;
	rasterizationState.polygonMode = vk::PolygonMode::eFill;
	rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
	rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
	rasterizationState.depthClampEnable = VK_FALSE;
	rasterizationState.rasterizerDiscardEnable = VK_FALSE;
	rasterizationState.depthBiasEnable = VK_FALSE;
	rasterizationState.lineWidth = 1.0f;

	// �u�����h���[�h�̐ݒ�
	vk::PipelineColorBlendStateCreateInfo colorBlendState;
	vk::PipelineColorBlendAttachmentState blendAttachmentState[1] = {};
	blendAttachmentState[0].colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	blendAttachmentState[0].blendEnable = VK_TRUE;
	blendAttachmentState[0].colorBlendOp = vk::BlendOp::eAdd;
	blendAttachmentState[0].srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	blendAttachmentState[0].dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	blendAttachmentState[0].alphaBlendOp = vk::BlendOp::eAdd;
	blendAttachmentState[0].srcAlphaBlendFactor = vk::BlendFactor::eOne;
	blendAttachmentState[0].dstAlphaBlendFactor = vk::BlendFactor::eZero;
	colorBlendState.attachmentCount = ARRAYSIZE(blendAttachmentState);
	colorBlendState.pAttachments = blendAttachmentState;

	// Viewport�X�e�[�g�̐ݒ�
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// DynamicState�𗘗p����Viewport��ScissorBox��ύX�ł���悤�ɂ��Ă���
	vk::PipelineDynamicStateCreateInfo dynamicState;
	std::vector<vk::DynamicState> dynamicStateEnables;
	dynamicStateEnables.push_back(vk::DynamicState::eViewport);
	dynamicStateEnables.push_back(vk::DynamicState::eScissor);
	dynamicState.pDynamicStates = dynamicStateEnables.data();
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

	// DepthStensil�X�e�[�g�̐ݒ�
	vk::PipelineDepthStencilStateCreateInfo depthStencilState;
	depthStencilState.depthTestEnable = VK_TRUE;
	depthStencilState.depthWriteEnable = VK_TRUE;
	depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
	depthStencilState.depthBoundsTestEnable = VK_FALSE;
	depthStencilState.back.failOp = vk::StencilOp::eKeep;
	depthStencilState.back.passOp = vk::StencilOp::eKeep;
	depthStencilState.back.compareOp = vk::CompareOp::eAlways;
	depthStencilState.stencilTestEnable = VK_FALSE;
	depthStencilState.front = depthStencilState.back;

	// �}���`�T���v���X�e�[�g
	vk::PipelineMultisampleStateCreateInfo multisampleState;
	multisampleState.pSampleMask = NULL;
	multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

	// �V�F�[�_�ݒ�
	std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
	shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
	shaderStages[0].module = g_VkVShader;
	shaderStages[0].pName = "main";
	shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
	shaderStages[1].module = g_VkPShader;
	shaderStages[1].pName = "main";

	// �p�C�v���C�����Ɋe��X�e�[�g��ݒ肵�Đ���
	vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
	pipelineCreateInfo.layout = g_VkPipeLayout;
	pipelineCreateInfo.renderPass = g_VkRenderPass;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();
	pipelineCreateInfo.pVertexInputState = &g_VkVertexInputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pDynamicState = &dynamicState;

	g_VkSamplePipeline = g_VkDevice.createGraphicsPipelines(g_VkPipelineCache, pipelineCreateInfo, nullptr)[0];
	if (!g_VkSamplePipeline)
	{
		return false;
	}

	return true;
}

void DestroyPipeline()
{
	g_VkDevice.destroyPipelineLayout(g_VkPipeLayout);
	g_VkDevice.destroyPipeline(g_VkSamplePipeline);
}

void DrawScene()
{
	// ���̃o�b�N�o�b�t�@���擾����
	uint32_t currentBuffer = g_VkSwapchain.AcquireNextImage(g_VkPresentComplete);

	// �R�}���h�o�b�t�@�̐ςݍ���
	{
		auto& cmdBuffer = g_VkCmdBuffers[currentBuffer];

		cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
		vk::CommandBufferBeginInfo cmdBufInfo;
		cmdBuffer.begin(cmdBufInfo);

		// UniformBuffer���A�b�v�f�[�g����
		// RenderPass���Ŕ��s���Ă͂����Ȃ��炵���̂ŁAbeginRenderPass�`endRenderPass�̊O���ōs��
		static float sRotY = 1.0f;

		SceneData scene;
		scene.mtxView = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(), glm::vec3(0.0f, 1.0f, 0.0f));
		scene.mtxProj = glm::perspectiveRH(glm::radians(60.0f), 1920.0f / 1080.0f, 1.0f, 100.0f);

		MeshData mesh1, mesh2;
		mesh1.mtxWorld = glm::rotate(glm::mat4x4(), glm::radians(sRotY), glm::vec3(0.0f, 1.0f, 0.0f));
		sRotY += 0.1f; if (sRotY > 360.0f) sRotY -= 360.0f;
		mesh2 = mesh1;
		mesh2.mtxWorld[3].x = 0.5f;
		mesh2.mtxWorld[3].z = 7.0f;

		cmdBuffer.updateBuffer(g_VkUniformBuffer.buffer, 0, sizeof(scene), reinterpret_cast<uint32_t*>(&scene));

		vk::ClearColorValue clearColor(std::array<float, 4>{ 0.0f, 0.0f, 0.5f, 1.0f });
		vk::ClearDepthStencilValue clearDepth(1.0f, 0);

		// �o�b�t�@�N���A
		{
			MySwapchain::Image& colorImage = g_VkSwapchain.GetImages()[currentBuffer];
			vk::ImageSubresourceRange colorSubRange;
			colorSubRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			colorSubRange.levelCount = 1;
			colorSubRange.layerCount = 1;

			vk::ImageSubresourceRange depthSubRange;
			depthSubRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
			depthSubRange.levelCount = 1;
			depthSubRange.layerCount = 1;

			// �N���A���邽�߂Ƀ��C�A�E�g��ύX
			SetImageLayout(
				cmdBuffer,
				colorImage.image,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal,
				colorSubRange);
			SetImageLayout(
				cmdBuffer,
				g_VkDepthBuffer.image,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				vk::ImageLayout::eTransferDstOptimal,
				depthSubRange);

			// �N���A
			cmdBuffer.clearColorImage(colorImage.image, vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearDepthStencilImage(g_VkDepthBuffer.image, vk::ImageLayout::eTransferDstOptimal, clearDepth, depthSubRange);

			// �`��̂��߂Ƀ��C�A�E�g��ύX
			SetImageLayout(
				cmdBuffer,
				colorImage.image,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				colorSubRange);
			SetImageLayout(
				cmdBuffer,
				g_VkDepthBuffer.image,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				depthSubRange);
		}

		// �`��p�X�J�n
		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = g_VkRenderPass;
		renderPassBeginInfo.renderArea.extent = vk::Extent2D(kScreenWidth, kScreenHeight);
		renderPassBeginInfo.clearValueCount = 0;
		renderPassBeginInfo.pClearValues = nullptr;
		renderPassBeginInfo.framebuffer = g_VkFramebuffers[currentBuffer];
		cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vk::Viewport(0.0f, 0.0f, static_cast<float>(kScreenWidth), static_cast<float>(kScreenHeight), 0.0f, 1.0f);
			cmdBuffer.setViewport(0, viewport);

			vk::Rect2D scissor = vk::Rect2D(vk::Offset2D(), vk::Extent2D(kScreenWidth, kScreenHeight));
			cmdBuffer.setScissor(0, scissor);

			// �e���\�[�X���̃o�C���h
			vk::DeviceSize offsets = 0;
			cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, g_VkPipeLayout, 0, g_VkDescSets, nullptr);
			cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, g_VkSamplePipeline);
			cmdBuffer.bindVertexBuffers(0, g_VkVBuffer.buffer, offsets);
			cmdBuffer.bindIndexBuffer(g_VkIBuffer.buffer, 0, vk::IndexType::eUint32);
			cmdBuffer.pushConstants(g_VkPipeLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(mesh1), &mesh1);
			cmdBuffer.drawIndexed(6, 1, 0, 0, 1);

			cmdBuffer.pushConstants(g_VkPipeLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(mesh2), &mesh2);
			cmdBuffer.drawIndexed(6, 1, 0, 0, 1);

		cmdBuffer.endRenderPass();

		//cmdBuffer.updateBuffer(g_VkUniformBuffer.buffer, 0, sizeof(scene2), reinterpret_cast<uint32_t*>(&scene2));

		//cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		//	// �e���\�[�X���̃o�C���h
		//	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, g_VkPipeLayout, 0, g_VkDescSets, nullptr);
		//	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, g_VkSamplePipeline);
		//	cmdBuffer.bindVertexBuffers(0, g_VkVBuffer.buffer, offsets);
		//	cmdBuffer.bindIndexBuffer(g_VkIBuffer.buffer, 0, vk::IndexType::eUint32);
		//	cmdBuffer.drawIndexed(6, 1, 0, 0, 1);

		//cmdBuffer.endRenderPass();

		// Present�̂��߁A�J���[�o�b�t�@�̃C���[�W���C�A�E�g��ύX
		{
			MySwapchain::Image& colorImage = g_VkSwapchain.GetImages()[currentBuffer];
			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;
			SetImageLayout(
				cmdBuffer,
				colorImage.image,
				vk::ImageLayout::eColorAttachmentOptimal,
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
		// �҂K�v������Z�}�t�H�̐��Ƃ��̔z���n��
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &g_VkPresentComplete;
		// Submit����R�}���h�o�b�t�@�̔z���n��
		// �����̃R�}���h�o�b�t�@��Submit�������ꍇ�͔z��ɂ���
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &g_VkCmdBuffers[currentBuffer];
		// �`�抮����m�点��Z�}�t�H��o�^����
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &g_VkRenderComplete;

		// Queue�ɑ΂���Submit����
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
	InitializeRenderResource();
	InitializePipeline();

	while (true)
	{
		PollEvents();

		if (g_CloseRequest)
		{
			break;
		}

		DrawScene();
	}

	DestroyPipeline();
	DestroyRenderResource();
	DestroyRenderSettings();
	g_VkSwapchain.Destroy();
	DestroyWindow();
	DestroyContext();
	return 0;
}


//	EOF
