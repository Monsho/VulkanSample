#include <vsl/swapchain.h>
#include <vsl/application.h>


namespace
{
	static const uint64_t	kFenceTimeout = 100000000000;
}

namespace vsl
{
	//----
	bool Swapchain::Initialize(Device& owner, HINSTANCE hInst, HWND hWnd)
	{
		pOwner_ = &owner;

		{
			vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo;
			surfaceCreateInfo.hinstance = hInst;
			surfaceCreateInfo.hwnd = hWnd;
			surface_ = owner.GetInstance().createWin32SurfaceKHR(surfaceCreateInfo);
			if (!surface_)
			{
				return false;
			}
		}

		// サポートされているフォーマットを取得
		auto surfaceFormats = owner.GetPhysicalDevice().getSurfaceFormatsKHR(surface_);
		if (surfaceFormats[0].format == vk::Format::eUndefined)
		{
			format_ = vk::Format::eR8G8B8A8Unorm;
		}
		else
		{
			format_ = surfaceFormats[0].format;
		}
		colorSpace_ = surfaceFormats[0].colorSpace;

		graphicsQueueIndex_ = owner.FindQueue(vk::QueueFlagBits::eGraphics, vk::QueueFlags(), surface_);
		if (graphicsQueueIndex_ == Application::kQueueIndexNotFound)
		{
			return false;
		}

		return true;
	}

	//----
	bool Swapchain::InitializeSwapchain(uint16_t width, uint16_t height, bool enableVSync)
	{
		vk::SwapchainKHR oldSwapchain = swapchain_;
		currentImage_ = 0;

		vk::Device& device = pOwner_->GetDevice();
		vk::PhysicalDevice& physical = pOwner_->GetPhysicalDevice();

		vk::SurfaceCapabilitiesKHR surfaceCaps = physical.getSurfaceCapabilitiesKHR(surface_);
		auto presentModes = physical.getSurfacePresentModesKHR(surface_);

		vk::Extent2D swapchainExtent(width, height);
		if (surfaceCaps.currentExtent.width > -1 && surfaceCaps.currentExtent.height > -1)
		{
			swapchainExtent = surfaceCaps.currentExtent;
		}
		width_ = swapchainExtent.width;
		height_ = swapchainExtent.height;

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
			swapchainCreateInfo.surface = surface_;
			swapchainCreateInfo.minImageCount = desiredNumSwapchainImages;
			swapchainCreateInfo.imageFormat = format_;
			swapchainCreateInfo.imageColorSpace = colorSpace_;
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

			swapchain_ = device.createSwapchainKHR(swapchainCreateInfo);
			if (!swapchain_)
			{
				return false;
			}
		}

		// 前回のSwapchainが残っている場合は削除
		if (oldSwapchain)
		{
			for (uint32_t i = 0; i < imageCount_; i++)
			{
				device.destroyImageView(images_[i].view);
			}
			device.destroySwapchainKHR(oldSwapchain);
		}

		vk::ImageViewCreateInfo viewCreateInfo;
		viewCreateInfo.format = format_;
		viewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.layerCount = 1;
		viewCreateInfo.viewType = vk::ImageViewType::e2D;

		auto swapChainImages = device.getSwapchainImagesKHR(swapchain_);
		imageCount_ = static_cast<uint32_t>(swapChainImages.size());

		// Swapchainの各イメージに対してViewとFenceを作成
		images_.resize(imageCount_);
		for (uint32_t i = 0; i < imageCount_; i++)
		{
			images_[i].image = swapChainImages[i];
			viewCreateInfo.image = swapChainImages[i];
			images_[i].view = device.createImageView(viewCreateInfo);
			images_[i].fence = vk::Fence();
		}

		return true;
	}

	//----
	std::vector<vk::Framebuffer> Swapchain::CreateFramebuffers(vk::FramebufferCreateInfo framebufferCreateInfo)
	{
		std::vector<vk::ImageView> views;
		views.resize(framebufferCreateInfo.attachmentCount);
		for (size_t i = 0; i < framebufferCreateInfo.attachmentCount; i++)
		{
			views[i] = framebufferCreateInfo.pAttachments[i];
		}
		framebufferCreateInfo.pAttachments = views.data();

		std::vector<vk::Framebuffer> framebuffers;
		framebuffers.resize(imageCount_);
		for (uint32_t i = 0; i < imageCount_; i++)
		{
			views[0] = images_[i].view;
			framebuffers[i] = pOwner_->GetDevice().createFramebuffer(framebufferCreateInfo);
		}
		return framebuffers;
	}

	//----
	void Swapchain::Destroy()
	{
		vk::Device& device = pOwner_->GetDevice();
		vk::Instance& inst = pOwner_->GetInstance();

		for (auto& image : images_)
		{
			if (image.view) device.destroyImageView(image.view);
			if (image.fence) device.destroyFence(image.fence);
		}
		device.destroySwapchainKHR(swapchain_);
		inst.destroySurfaceKHR(surface_);
	}

	//----
	uint32_t Swapchain::AcquireNextImage(vk::Semaphore presentCompleteSemaphore)
	{
		auto resultValue = pOwner_->GetDevice().acquireNextImageKHR(swapchain_, UINT64_MAX, presentCompleteSemaphore, vk::Fence());
		assert(resultValue.result == vk::Result::eSuccess);

		currentImage_ = resultValue.value;
		return currentImage_;
	}

	//----
	vk::Result Swapchain::Present(vk::Semaphore waitSemaphore)
	{
		presentInfo_.waitSemaphoreCount = waitSemaphore ? 1 : 0;
		presentInfo_.pWaitSemaphores = &waitSemaphore;
		return pOwner_->GetQueue().presentKHR(presentInfo_);
	}

	//----
	vk::Fence Swapchain::GetSubmitFence(bool destroy)
	{
		vk::Device& device = pOwner_->GetDevice();
		auto& image = images_[currentImage_];
		while (image.fence)
		{
			// Fenceが有効な間は完了するまで待つ
			vk::Result fenceRes = device.waitForFences(image.fence, VK_TRUE, kFenceTimeout);
			if (fenceRes == vk::Result::eSuccess)
			{
				if (destroy)
				{
					device.destroyFence(image.fence);
				}
				image.fence = vk::Fence();
			}
		}

		image.fence = device.createFence(vk::FenceCreateFlags());
		return image.fence;
	}

}	// namespace vsl


//	EOF
