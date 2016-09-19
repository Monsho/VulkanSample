#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>


namespace vsl
{
	class Device;

	//----
	class RenderPass
	{
	public:
		RenderPass()
		{}
		~RenderPass()
		{
			Destroy();
		}

		bool Initialize(
			Device& device,
			vk::ArrayProxy<const vk::AttachmentDescription> attachDescs,
			vk::ArrayProxy<const vk::SubpassDescription> subpasses,
			vk::ArrayProxy<const vk::SubpassDependency> dependencies);
		bool InitializeAsColorStandard(
			Device& device,
			vk::ArrayProxy<vk::Format> colorFormats,
			vk::Optional<vk::Format> depthFormat);
		void Destroy();

		// getter
		vk::RenderPass& GetPass() { return pass_; }

	private:
		Device*		pOwner_{ nullptr };

		vk::RenderPass		pass_;
	};	// class Buffer

}	// namespace vsl


//	EOF
