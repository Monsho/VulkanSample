#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>


namespace vsl
{
	class Device;
	class Buffer;

	//----
	class Image
	{
	public:
		Image()
		{}
		~Image()
		{
			Destroy();
		}

		bool InitializeAsDepthStencilBuffer(
			Device& owner,
			vk::CommandBuffer& cmdBuff,
			vk::Format format,
			uint16_t width, uint16_t height,
			uint16_t mipLevels = 1, uint16_t arrayLayers = 1);
		bool InitializeFromTgaImage(
			Device& owner,
			vk::CommandBuffer& cmdBuff,
			Buffer& staging,
			const std::string& filename);

		void Destroy();

		void SetImageLayout(vk::CommandBuffer cmdBuffer, vk::ImageLayout newImageLayout, vk::ImageSubresourceRange subresourceRange);

		// getter
		vk::Image&		GetImage()			{ return image_; }
		vk::ImageView&	GetView()			{ return view_; }
		vk::Format		GetFormat()	const	{ return format_; }
		uint16_t		GetWidth()	const	{ return width_; }
		uint16_t		GetHeight()	const	{ return height_; }

	private:
		Device*		pOwner_{ nullptr };

		vk::Image			image_;
		vk::DeviceMemory	devMem_;
		vk::ImageView		view_;
		vk::Format			format_{ vk::Format::eUndefined };
		uint16_t			width_{ 0 }, height_{ 0 };
		vk::ImageLayout		currentLayout_{ vk::ImageLayout::eUndefined };

	public:
		static void SetImageLayout(
			vk::CommandBuffer cmdbuffer,
			vk::Image image,
			vk::ImageLayout oldImageLayout,
			vk::ImageLayout newImageLayout,
			vk::ImageSubresourceRange subresourceRange);
	};	// class Image

}	// namespace vsl


//	EOF
