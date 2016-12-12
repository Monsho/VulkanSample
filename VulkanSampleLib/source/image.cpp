#include <vsl/image.h>
#include <vsl/device.h>
#include <vsl/buffer.h>
#include <vsl/targa.h>


namespace vsl
{
	//----
	bool Image::InitializeAsColorBuffer(
		Device& owner,
		vk::CommandBuffer& cmdBuff,
		vk::Format format,
		uint16_t width, uint16_t height,
		uint16_t mipLevels, uint16_t arrayLayers,
		bool useCompute)
	{
		pOwner_ = &owner;
		format_ = format;
		width_ = width;
		height_ = height;

		// 指定のフォーマットがサポートされているか調べる
		vk::FormatProperties formatProps = owner.GetPhysicalDevice().getFormatProperties(format);
		assert(formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eColorAttachment);

		vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;

		vk::Device& device = owner.GetDevice();

		// イメージを作成する
		vk::ImageCreateInfo imageCreateInfo;
		vk::ImageUsageFlags computeFlag = useCompute ? vk::ImageUsageFlags(vk::ImageUsageFlagBits::eStorage) : vk::ImageUsageFlags();
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.extent = vk::Extent3D(width, height, 1);
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = arrayLayers;
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | computeFlag;
		image_ = device.createImage(imageCreateInfo);
		if (!image_)
		{
			return false;
		}

		// メモリを確保
		vk::MemoryRequirements memReqs = device.getImageMemoryRequirements(image_);
		vk::MemoryAllocateInfo memAlloc;
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = owner.GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		devMem_ = device.allocateMemory(memAlloc);
		if (!devMem_)
		{
			return false;
		}
		device.bindImageMemory(image_, devMem_, 0);

		// Viewを作成
		vk::ImageViewCreateInfo viewCreateInfo;
		viewCreateInfo.viewType = vk::ImageViewType::e2D;
		viewCreateInfo.format = format;
		viewCreateInfo.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		viewCreateInfo.subresourceRange.aspectMask = aspect;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.layerCount = 1;
		viewCreateInfo.image = image_;
		view_ = device.createImageView(viewCreateInfo);
		if (!view_)
		{
			return false;
		}

		// レイアウト変更のコマンドを発行する
		{
			vk::CommandBufferBeginInfo cmdBufferBeginInfo;
			vk::BufferCopy copyRegion;

			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = aspect;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;
			Image::SetImageLayout(
				cmdBuff,
				image_,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal,
				subresourceRange);
			currentLayout_ = vk::ImageLayout::eColorAttachmentOptimal;
		}

		return true;
	}

	//----
	bool Image::InitializeAsDepthStencilBuffer(
		Device& owner,
		vk::CommandBuffer& cmdBuff,
		vk::Format format,
		uint16_t width, uint16_t height,
		uint16_t mipLevels, uint16_t arrayLayers,
		bool useCompute)
	{
		pOwner_ = &owner;
		format_ = format;
		width_ = width;
		height_ = height;

		// 指定のフォーマットがサポートされているか調べる
		vk::FormatProperties formatProps = owner.GetPhysicalDevice().getFormatProperties(format);
		assert(formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment);

		vk::ImageAspectFlags aspect;
		switch (format)
		{
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			aspect = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
			break;
		case vk::Format::eS8Uint:
			aspect = vk::ImageAspectFlagBits::eStencil;
			break;
		default:
			aspect = vk::ImageAspectFlagBits::eDepth;
			break;
		}

		vk::Device& device = owner.GetDevice();

		// 深度バッファのイメージを作成する
		vk::ImageCreateInfo imageCreateInfo;
		vk::ImageUsageFlags computeFlag = useCompute ? vk::ImageUsageFlags(vk::ImageUsageFlagBits::eStorage) : vk::ImageUsageFlags();
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.extent = vk::Extent3D(width, height, 1);
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = arrayLayers;
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | computeFlag;
		image_ = device.createImage(imageCreateInfo);
		if (!image_)
		{
			return false;
		}

		// 深度バッファ用のメモリを確保
		vk::MemoryRequirements memReqs = device.getImageMemoryRequirements(image_);
		vk::MemoryAllocateInfo memAlloc;
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = owner.GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		devMem_ = device.allocateMemory(memAlloc);
		if (!devMem_)
		{
			return false;
		}
		device.bindImageMemory(image_, devMem_, 0);

		// Viewを作成
		vk::ImageViewCreateInfo viewCreateInfo;
		viewCreateInfo.viewType = vk::ImageViewType::e2D;
		viewCreateInfo.format = format;
		viewCreateInfo.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		viewCreateInfo.subresourceRange.aspectMask = aspect;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.layerCount = 1;
		viewCreateInfo.image = image_;
		view_ = device.createImageView(viewCreateInfo);
		if (!view_)
		{
			return false;
		}

		// テクスチャとして使用する際のViewを作成
		if (aspect == vk::ImageAspectFlagBits::eDepth)
		{
			depthView_ = view_;
		}
		else if (aspect == vk::ImageAspectFlagBits::eStencil)
		{
			stencilView_ = view_;
		}
		else
		{
			viewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
			depthView_ = device.createImageView(viewCreateInfo);

			viewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
			stencilView_ = device.createImageView(viewCreateInfo);

			if (!depthView_ || !stencilView_)
			{
				return false;
			}
		}

		// レイアウト変更のコマンドを発行する
		{
			vk::CommandBufferBeginInfo cmdBufferBeginInfo;
			vk::BufferCopy copyRegion;

			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = aspect;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;
			Image::SetImageLayout(
				cmdBuff,
				image_,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				subresourceRange);
			currentLayout_ = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}

		return true;
	}

	//----
	bool Image::InitializeFromTgaImage(
		Device& owner,
		vk::CommandBuffer& cmdBuff,
		Buffer& staging,
		const std::string& filename)
	{
		tga_image tgaImage;
		if (tga_read(&tgaImage, filename.c_str()) != TGA_NOERR)
		{
			return false;
		}

		vk::Device& device = owner.GetDevice();
		bool ret = false;

		// Stagingバッファ作成
		size_t size = tgaImage.width * tgaImage.height * tgaImage.pixel_depth / 8;
		if (!staging.InitializeAsStaging(owner, size, tgaImage.image_data))
		{
			goto end;
		}

		if (!InitializeFromStaging(owner, cmdBuff, staging, vk::Format::eB8G8R8A8Unorm, tgaImage.width, tgaImage.height))
		{
			goto end;
		}

		ret = true;
end:
		tga_free_buffers(&tgaImage);

		return ret;
	}

	//----
	bool Image::InitializeFromStaging(
		Device& owner,
		vk::CommandBuffer& cmdBuff,
		Buffer& staging,
		vk::Format format,
		uint32_t width, uint32_t height,
		uint16_t mipLevels, uint16_t arrayLayers)
	{
		pOwner_ = &owner;

		format_ = format;
		width_ = width;
		height_ = height;

		vk::Device& device = owner.GetDevice();
		bool ret = false;

		// イメージ生成
		{
			vk::ImageCreateInfo imageCreateInfo;
			imageCreateInfo.imageType = vk::ImageType::e2D;
			imageCreateInfo.arrayLayers = arrayLayers;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.format = format_;
			imageCreateInfo.extent = vk::Extent3D(width, height, 1);
			imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
			imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
			image_ = device.createImage(imageCreateInfo);
			if (!image_)
			{
				return false;
			}

			vk::MemoryRequirements memReqs = device.getImageMemoryRequirements(image_);
			vk::MemoryAllocateInfo memAllocInfo;
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = owner.GetMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
			devMem_ = device.allocateMemory(memAllocInfo);
			if (!devMem_)
			{
				return false;
			}
			device.bindImageMemory(image_, devMem_, 0);
		}

		// Viewの作成
		{
			vk::ImageViewCreateInfo viewCreateInfo;
			viewCreateInfo.viewType = vk::ImageViewType::e2D;
			viewCreateInfo.format = format_;
			viewCreateInfo.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
			viewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			viewCreateInfo.subresourceRange.baseMipLevel = 0;
			viewCreateInfo.subresourceRange.baseArrayLayer = 0;
			viewCreateInfo.subresourceRange.layerCount = arrayLayers;
			viewCreateInfo.subresourceRange.levelCount = mipLevels;
			viewCreateInfo.image = image_;
			view_ = device.createImageView(viewCreateInfo);
			if (!view_)
			{
				return false;
			}
		}

		// コピーコマンドを生成、実行
		{
			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			subresourceRange.levelCount = arrayLayers;
			subresourceRange.layerCount = mipLevels;

			// レイアウト設定
			Image::SetImageLayout(
				cmdBuff,
				image_,
				vk::ImageLayout::ePreinitialized,
				vk::ImageLayout::eTransferDstOptimal,
				subresourceRange);

			// コピーコマンド
			vk::BufferImageCopy bufferCopyRegion;
			bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			bufferCopyRegion.imageSubresource.layerCount = arrayLayers;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.imageExtent.width = width;
			bufferCopyRegion.imageExtent.height = height;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.bufferOffset = 0;
			cmdBuff.copyBufferToImage(staging.GetBuffer(), image_, vk::ImageLayout::eTransferDstOptimal, 1, &bufferCopyRegion);

			// コピー完了後にレイアウト変更
			// シェーダから読み込める状態にしておく
			Image::SetImageLayout(
				cmdBuff,
				image_,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				subresourceRange);
			currentLayout_ = vk::ImageLayout::eShaderReadOnlyOptimal;
		}

		return true;
	}

	//----
	void Image::Destroy()
	{
		if (pOwner_)
		{
			vk::Device& device = pOwner_->GetDevice();
			if (stencilView_ && view_ != stencilView_) { device.destroyImageView(stencilView_); stencilView_ = vk::ImageView(); }
			if (depthView_ && view_ != depthView_) { device.destroyImageView(depthView_); depthView_ = vk::ImageView(); }
			if (view_) { device.destroyImageView(view_); view_ = vk::ImageView(); }
			if (image_) { device.destroyImage(image_); image_ = vk::Image(); }
			if (devMem_) { device.freeMemory(devMem_); devMem_ = vk::DeviceMemory(); }
		}
		pOwner_ = nullptr;
	}

	//----
	void Image::SetImageLayout(vk::CommandBuffer cmdBuffer, vk::ImageLayout newImageLayout, vk::ImageSubresourceRange subresourceRange)
	{
		Image::SetImageLayout(cmdBuffer, image_, currentLayout_, newImageLayout, subresourceRange);
		currentLayout_ = newImageLayout;
	}

	//----
	// イメージレイアウトを設定するため、バリアを貼る
	// Deviceなどに左右されないが、Applicationの静的メンバ関数として実装する
	void Image::SetImageLayout(
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

}	// namespace vsl


//	EOF
