#include <vsl/buffer.h>
#include <vsl/device.h>


namespace vsl
{
	//----
	bool Buffer::InitializeCommon(Device& owner, size_t size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memProp, const void* pData)
	{
		pOwner_ = &owner;
		size_ = size;

		vk::Device& device = owner.GetDevice();

		// Buffer生成
		vk::BufferCreateInfo createInfo;
		createInfo.size = size;
		createInfo.usage = usage;
		buffer_ = device.createBuffer(createInfo);
		if (!buffer_)
		{
			return false;
		}

		// バッファ用メモリ作成
		vk::MemoryRequirements memReqs = device.getBufferMemoryRequirements(buffer_);
		vk::MemoryAllocateInfo memAlloc;
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = owner.GetMemoryTypeIndex(memReqs.memoryTypeBits, memProp);
		devMem_ = device.allocateMemory(memAlloc);
		if (!devMem_)
		{
			return false;
		}

		device.bindBufferMemory(buffer_, devMem_, 0);

		// メモリコピー
		if (pData)
		{
			void* dst = device.mapMemory(devMem_, 0, size, vk::MemoryMapFlags());
			memcpy(dst, pData, size);
			device.unmapMemory(devMem_);
		}

		return true;
	}

	//----
	bool Buffer::InitializeAsStaging(Device& owner, size_t size, const void* pData)
	{
		return InitializeCommon(owner, size, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible, pData);
	}

	//----
	bool Buffer::InitializeAsVertexBuffer(Device& owner, size_t size)
	{
		return InitializeCommon(owner, size, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
	}
	bool Buffer::InitializeAsMappableVertexBuffer(Device& owner, size_t size)
	{
		return InitializeCommon(owner, size, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	}

	//----
	bool Buffer::InitializeAsIndexBuffer(Device& owner, size_t size)
	{
		return InitializeCommon(owner, size, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
	}
	bool Buffer::InitializeAsMappableIndexBuffer(Device& owner, size_t size)
	{
		return InitializeCommon(owner, size, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	}

	//----
	bool Buffer::InitializeAsUniformBuffer(Device& owner, size_t size, const void* pData)
	{
		return InitializeCommon(owner, size, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible, pData);
	}

	//----
	void Buffer::Destroy()
	{
		if (pOwner_)
		{
			vk::Device& device = pOwner_->GetDevice();
			if (view_) { device.destroyBufferView(view_); view_ = vk::BufferView(); }
			if (buffer_) { device.destroyBuffer(buffer_); buffer_ = vk::Buffer(); }
			if (devMem_) { device.freeMemory(devMem_); devMem_ = vk::DeviceMemory(); }
		}
		pOwner_ = nullptr;
		size_ = 0;
	}

	//----
	void Buffer::CopyFrom(vk::CommandBuffer& cmdBuffer, Buffer& srcBuffer, size_t srcOffset, size_t dstOffset, size_t size)
	{
		if (buffer_.operator bool())
		{
			vk::BufferCopy copyRegion(srcOffset, dstOffset, (size == 0) ? size_ : size);
			cmdBuffer.copyBuffer(srcBuffer.buffer_, buffer_, copyRegion);
		}
	}

}	// namespace vsl


//	EOF
