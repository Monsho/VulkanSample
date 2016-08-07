#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>


namespace vsl
{
	class Device;

	//----
	class Swapchain
	{
	public:
		struct Image
		{
			vk::Image		image;
			vk::ImageView	view;
			vk::Fence		fence;
		};	// struct Image

	public:
		Swapchain()
		{
			presentInfo_.swapchainCount = 1;
			presentInfo_.pSwapchains = &swapchain_;
			presentInfo_.pImageIndices = &currentImage_;
		}
		~Swapchain()
		{}

		bool Initialize(Device& owner, HINSTANCE hInst, HWND hWnd);
		bool InitializeSwapchain(uint16_t width, uint16_t height, bool enableVSync);

		std::vector<vk::Framebuffer> CreateFramebuffers(vk::FramebufferCreateInfo framebufferCreateInfo);

		void Destroy();

		uint32_t AcquireNextImage(vk::Semaphore presentCompleteSemaphore);

		vk::Result Present(vk::Semaphore waitSemaphore);

		vk::Fence GetSubmitFence(bool destroy = false);

		// getter
		vk::SurfaceKHR&		GetSurface()			{ return surface_; }
		vk::SwapchainKHR&	GetSwapchain()			{ return swapchain_; }
		vk::PresentInfoKHR&	GetPresentInfo()		{ return presentInfo_; }
		std::vector<Image>&	GetImages()				{ return images_; }
		vk::Format			GetFormat() const		{ return format_; }
		vk::ColorSpaceKHR	GetColorSpace() const	{ return colorSpace_; }
		uint32_t			GetImageCount() const	{ return imageCount_; }
		uint16_t			GetWidth() const		{ return width_; }
		uint16_t			GetHeight() const		{ return height_; }

	private:
		Device*				pOwner_{ nullptr };
		vk::SurfaceKHR		surface_;
		vk::SwapchainKHR	swapchain_;
		vk::PresentInfoKHR	presentInfo_;
		std::vector<Image>	images_;
		vk::Format			format_;
		vk::ColorSpaceKHR	colorSpace_;
		uint32_t			imageCount_{ 0 };
		uint32_t			currentImage_{ 0 };
		uint32_t			graphicsQueueIndex_;
		uint16_t			width_, height_;
	};	// class Swapchain

}	// namespace vsl


//	EOF
