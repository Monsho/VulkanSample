#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vsl/application.h>
#include <vsl/device.h>
#include <vsl/image.h>
#include <vsl/buffer.h>


namespace
{
	static const uint16_t kScreenWidth = 1920;
	static const uint16_t kScreenHeight = 1080;
}	// namespace

bool Initialize(vsl::Device& device)
{
	return true;
}
class MySample
{
public:
	MySample()
	{}

	//----
	bool Initialize(vsl::Device& device)
	{
		// レンダーパスの初期化
		if (!InitializeRenderPass(device))
		{
			return false;
		}

		// 初期化用のコマンドバッファを開始
		vk::CommandBuffer& initCmdBuffer = device.GetCommandBuffers()[0];
		initCmdBuffer.begin(vk::CommandBufferBeginInfo());

		// 深度バッファの初期化
		if (!depthBuffer_.InitializeAsDepthStencilBuffer(
			device, initCmdBuffer,
			vk::Format::eD32SfloatS8Uint,
			kScreenWidth, kScreenHeight))
		{
			return false;
		}

		// フレームバッファ設定
		{
			std::array<vk::ImageView, 2> views;
			views[1] = depthBuffer_.GetView();

			vk::FramebufferCreateInfo framebufferCreateInfo;
			framebufferCreateInfo.renderPass = renderPass_;
			framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(views.size());
			framebufferCreateInfo.pAttachments = views.data();
			framebufferCreateInfo.width = kScreenWidth;
			framebufferCreateInfo.height = kScreenHeight;
			framebufferCreateInfo.layers = 1;

			// Swapchainからフレームバッファを生成する
			frameBuffers_ = device.GetSwapchain().CreateFramebuffers(framebufferCreateInfo);
		}

		// コマンド積みこみ終了
		initCmdBuffer.end();

		// コマンドをSubmitして終了を待つ
		vk::SubmitInfo copySubmitInfo;
		copySubmitInfo.commandBufferCount = 1;
		copySubmitInfo.pCommandBuffers = &initCmdBuffer;
		device.GetQueue().submit(copySubmitInfo, VK_NULL_HANDLE);
		device.GetQueue().waitIdle();

		return true;
	}

	//----
	bool Loop(vsl::Device& device)
	{
		auto currentIndex = device.AcquireNextImage();
		auto& cmdBuffer = device.BeginMainCommandBuffer();
		auto& currentImage = device.GetCurrentSwapchainImage();

		// バッファクリア
		{
			vk::ClearColorValue clearColor(std::array<float, 4>{ 0.0f, 0.0f, 0.5f, 1.0f });
			vk::ClearDepthStencilValue clearDepth(1.0f, 0);

			vk::ImageSubresourceRange colorSubRange;
			colorSubRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			colorSubRange.levelCount = 1;
			colorSubRange.layerCount = 1;

			vk::ImageSubresourceRange depthSubRange;
			depthSubRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
			depthSubRange.levelCount = 1;
			depthSubRange.layerCount = 1;

			// クリアするためにレイアウトを変更
			vsl::Image::SetImageLayout(
				cmdBuffer,
				currentImage,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal,
				colorSubRange);
			vsl::Image::SetImageLayout(
				cmdBuffer,
				depthBuffer_.GetImage(),
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				vk::ImageLayout::eTransferDstOptimal,
				depthSubRange);

			// クリア
			cmdBuffer.clearColorImage(currentImage, vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearDepthStencilImage(depthBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearDepth, depthSubRange);

			// 描画のためにレイアウトを変更
			vsl::Image::SetImageLayout(
				cmdBuffer,
				currentImage,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				colorSubRange);
			vsl::Image::SetImageLayout(
				cmdBuffer,
				depthBuffer_.GetImage(),
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				depthSubRange);
		}

		device.ReadyPresentAndEndMainCommandBuffer();
		device.SubmitAndPresent();

		return true;
	}

	//----
	bool Terminate(vsl::Device& device)
	{
		vk::Device& d = device.GetDevice();

		for (auto& fb : frameBuffers_)
		{
			d.destroyFramebuffer(fb);
		}
		depthBuffer_.Destroy();
		if (renderPass_) d.destroyRenderPass(renderPass_);

		return true;
	}

private:
	//----
	bool InitializeRenderPass(vsl::Device& device)
	{
		// RenderPass設定
		std::array<vk::AttachmentDescription, 2> attachmentDescs;
		std::array<vk::AttachmentReference, 2> attachmentRefs;

		// カラーバッファのアタッチメント設定
		attachmentDescs[0].format = device.GetSwapchain().GetFormat();
		attachmentDescs[0].loadOp = vk::AttachmentLoadOp::eDontCare;
		attachmentDescs[0].storeOp = vk::AttachmentStoreOp::eStore;
		attachmentDescs[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attachmentDescs[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		// 深度バッファのアタッチメント設定
		attachmentDescs[1].format = vk::Format::eD32SfloatS8Uint;
		attachmentDescs[1].loadOp = vk::AttachmentLoadOp::eDontCare;
		attachmentDescs[1].storeOp = vk::AttachmentStoreOp::eDontCare;
		attachmentDescs[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		attachmentDescs[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		// カラーバッファのリファレンス設定
		vk::AttachmentReference& colorReference = attachmentRefs[0];
		colorReference.attachment = 0;
		colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

		// 深度バッファのリファレンス設定
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

		// RenderPass生成
		{
			vk::RenderPassCreateInfo renderPassInfo;
			renderPassInfo.attachmentCount = (uint32_t)attachmentDescs.size();
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.subpassCount = (uint32_t)subpasses.size();
			renderPassInfo.pSubpasses = subpasses.data();
			renderPassInfo.dependencyCount = (uint32_t)subpassDepends.size();
			renderPassInfo.pDependencies = subpassDepends.data();
			renderPass_ = device.GetDevice().createRenderPass(renderPassInfo);
		}

		return renderPass_.operator bool();
	}

private:
	vk::RenderPass	renderPass_;
	vsl::Image		depthBuffer_;
	std::vector<vk::Framebuffer>	frameBuffers_;
};	// class MySample

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
	MySample mySample;

	vsl::Application app(hInstance,
		std::bind(&MySample::Initialize, std::ref(mySample), std::placeholders::_1),
		std::bind(&MySample::Loop, std::ref(mySample), std::placeholders::_1),
		std::bind(&MySample::Terminate, std::ref(mySample), std::placeholders::_1));
	app.Run(kScreenWidth, kScreenHeight);

	return 0;
}


//	EOF
