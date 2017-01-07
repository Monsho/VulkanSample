#pragma once

#include <functional>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vsl/swapchain.h>


namespace vsl
{
	//----
	class Device
	{
	public:
		static const uint32_t	kQueueIndexNotFound = 0xffffffff;

	public:
		Device()
		{}
		~Device()
		{}

		bool InitializeContext(HINSTANCE hInst, HWND hWnd, uint16_t screenWidth, uint16_t screenHeight);
		void DestroyContext();

		uint32_t AcquireNextImage() { return currentBufferIndex_ = vkSwapchain_.AcquireNextImage(vkPresentComplete_); }
		vk::CommandBuffer& BeginMainCommandBuffer();
		void ReadyPresentAndEndMainCommandBuffer();
		void SubmitAndPresent(uint32_t waitSemaphoreCount = 0, vk::Semaphore* pWaitSemaphores = nullptr, vk::PipelineStageFlags* pWaitStages = nullptr, uint32_t signalSemaphoreCount = 0, vk::Semaphore* pSignalSemaphores = nullptr);

		// utility
		uint32_t FindQueue(vk::QueueFlags queueFlag, vk::QueueFlags notFlag = vk::QueueFlags(), const vk::SurfaceKHR& surface = vk::SurfaceKHR());
		uint32_t GetMemoryTypeIndex(uint32_t bits, const vk::MemoryPropertyFlags& properties);

		// getter
		vk::Instance&		GetInstance()		{ return vkInstance_; }
		vk::PhysicalDevice&	GetPhysicalDevice()	{ return vkPhysicalDevice_; }
		vk::Device&			GetDevice()			{ return vkDevice_; }
		vk::PipelineCache&	GetPipelineCache()	{ return vkPipelineCache_; }
		vk::Queue&			GetQueue()			{ return vkQueue_; }
		vk::Queue&			GetComputeQueue()	{ return vkComputeQueue_; }
		vk::CommandPool&	GetCommandPool()	{ return vkCmdPool_; }

		std::vector<vk::CommandBuffer>&	GetCommandBuffers()				{ return vkCmdBuffers_; }
		vk::CommandBuffer&				GetCurrentCommandBuffer()		{ return vkCmdBuffers_[currentBufferIndex_]; }
		std::vector<vk::CommandBuffer>&	GetComputeCommandBuffers()		{ return vkComputeCmdBuffers_; }
		vk::CommandBuffer&				GetCurrentComputeCommandBuffer(){ return vkComputeCmdBuffers_[currentBufferIndex_]; }

		Swapchain&	GetSwapchain()					{ return vkSwapchain_; }
		uint32_t	GetCurrentBufferIndex() const	{ return currentBufferIndex_; }
		vk::Image&	GetCurrentSwapchainImage()		{ return vkSwapchain_.GetImages()[currentBufferIndex_].image; }

	private:
		vk::Instance			vkInstance_;
		vk::PhysicalDevice		vkPhysicalDevice_;
		vk::Device				vkDevice_;
		vk::PipelineCache		vkPipelineCache_;
		vk::Queue				vkQueue_, vkComputeQueue_;
		vk::CommandPool			vkCmdPool_, vkComputeCmdPool_;
		vk::Semaphore			vkPresentComplete_;
		vk::Semaphore			vkRenderComplete_;

		std::vector<vk::CommandBuffer>	vkCmdBuffers_;
		std::vector<vk::CommandBuffer>	vkComputeCmdBuffers_;

		Swapchain	vkSwapchain_;
		uint32_t	currentBufferIndex_{ 0 };
	};	// class Device

}	// namespace vsl


//	EOF
