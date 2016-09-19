#include <vsl/render_pass.h>
#include <vsl/device.h>


namespace vsl
{
	//----
	bool RenderPass::Initialize(
		Device& device,
		vk::ArrayProxy<const vk::AttachmentDescription> attachDescs,
		vk::ArrayProxy<const vk::SubpassDescription> subpasses,
		vk::ArrayProxy<const vk::SubpassDependency> dependencies)
	{
		pOwner_ = &device;

		vk::RenderPassCreateInfo renderPassInfo;
		renderPassInfo.attachmentCount = (uint32_t)attachDescs.size();
		renderPassInfo.pAttachments = attachDescs.data();
		renderPassInfo.subpassCount = (uint32_t)subpasses.size();
		renderPassInfo.pSubpasses = subpasses.data();
		renderPassInfo.dependencyCount = (uint32_t)dependencies.size();
		renderPassInfo.pDependencies = dependencies.data();
		pass_ = device.GetDevice().createRenderPass(renderPassInfo);

		return pass_.operator bool();
	}

	//----
	bool RenderPass::InitializeAsColorStandard(
		Device& device,
		vk::ArrayProxy<vk::Format> colorFormats,
		vk::Optional<vk::Format> depthFormat)
	{
		bool enableDepth = depthFormat != nullptr;

		// デスクリプション設定
		std::vector<vk::AttachmentDescription> attachDescs;
		attachDescs.resize(colorFormats.size() + (enableDepth ? 1 : 0));

		for (size_t i = 0; i < colorFormats.size(); ++i)
		{
			attachDescs[i].format = colorFormats.data()[i];
			attachDescs[i].loadOp = vk::AttachmentLoadOp::eDontCare;
			attachDescs[i].storeOp = vk::AttachmentStoreOp::eStore;
			attachDescs[i].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
			attachDescs[i].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
		}
		if (enableDepth)
		{
			vk::AttachmentDescription& depthDesc = attachDescs[attachDescs.size() - 1];
			depthDesc.format = *depthFormat;
			depthDesc.loadOp = vk::AttachmentLoadOp::eDontCare;
			depthDesc.storeOp = vk::AttachmentStoreOp::eDontCare;
			depthDesc.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			depthDesc.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}

		// カラーバッファのリファレンス設定
		std::vector<vk::AttachmentReference> colorRefs;
		colorRefs.resize(colorFormats.size());
		for (size_t i = 0; i < colorRefs.size(); ++i)
		{
			colorRefs[i].attachment = static_cast<uint32_t>(i);
			colorRefs[i].layout = vk::ImageLayout::eColorAttachmentOptimal;
		}
		// 深度バッファのリファレンス設定
		vk::AttachmentReference depthRef(static_cast<uint32_t>(colorRefs.size()), vk::ImageLayout::eDepthStencilAttachmentOptimal);

		std::array<vk::SubpassDescription, 1> subpasses;
		{
			vk::SubpassDescription& subpass = subpasses[0];
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
			subpass.pColorAttachments = colorRefs.data();
			subpass.pDepthStencilAttachment = enableDepth ? &depthRef : nullptr;
		}

		std::array<vk::SubpassDependency, 1> dependencies;
		{
			vk::SubpassDependency& dependency = dependencies[0];
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;
			dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}

		return Initialize(device, attachDescs, subpasses, dependencies);
	}

	//----
	void RenderPass::Destroy()
	{
		if (pOwner_)
		{
			vk::Device& device = pOwner_->GetDevice();
			if (pass_) { device.destroyRenderPass(pass_); pass_ = vk::RenderPass(); }
		}
		pOwner_ = nullptr;
	}

}	// namespace vsl


//	EOF
