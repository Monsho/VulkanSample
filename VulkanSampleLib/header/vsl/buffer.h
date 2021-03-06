﻿#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>


namespace vsl
{
	class Device;

	//----
	class Buffer
	{
	public:
		Buffer()
		{}
		~Buffer()
		{
			Destroy();
		}

		bool InitializeAsStaging(Device& owner, size_t size, const void* pData = nullptr);
		bool InitializeAsVertexBuffer(Device& owner, size_t size);
		bool InitializeAsMappableVertexBuffer(Device& owner, size_t size);
		bool InitializeAsIndexBuffer(Device& owner, size_t size);
		bool InitializeAsMappableIndexBuffer(Device& owner, size_t size);
		bool InitializeAsUniformBuffer(Device& owner, size_t size, const void* pData = nullptr);

		void Destroy();

		void CopyFrom(vk::CommandBuffer& cmdBuffer, Buffer& srcBuffer, size_t srcOffset = 0, size_t dstOffset = 0, size_t size = 0);

		vk::DescriptorBufferInfo GetDescInfo()
		{
			return vk::DescriptorBufferInfo(buffer_, 0, size_);
		}

		// getter
		vk::Buffer& GetBuffer()			{ return buffer_; }
		vk::DeviceMemory& GetDevMem()	{ return devMem_; }
		vk::BufferView& GetView()		{ return view_; }
		size_t GetSize()				{ return size_; }

	private:
		bool InitializeCommon(Device& owner, size_t size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memProp, const void* pData = nullptr);

	private:
		Device*		pOwner_{ nullptr };

		vk::Buffer			buffer_;
		vk::DeviceMemory	devMem_;
		vk::BufferView		view_;
		size_t				size_{ 0 };
	};	// class Buffer

}	// namespace vsl


//	EOF
