#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vsl/application.h>
#include <vsl/device.h>
#include <vsl/image.h>
#include <vsl/buffer.h>
#include <vsl/shader.h>


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
		// �����_�[�p�X�̏�����
		if (!InitializeRenderPass(device))
		{
			return false;
		}

		// �������p�̃R�}���h�o�b�t�@���J�n
		vk::CommandBuffer& initCmdBuffer = device.GetCommandBuffers()[0];
		initCmdBuffer.begin(vk::CommandBufferBeginInfo());

		// �[�x�o�b�t�@�̏�����
		if (!depthBuffer_.InitializeAsDepthStencilBuffer(
			device, initCmdBuffer,
			vk::Format::eD32SfloatS8Uint,
			kScreenWidth, kScreenHeight))
		{
			return false;
		}

		// �t���[���o�b�t�@�ݒ�
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

			// Swapchain����t���[���o�b�t�@�𐶐�����
			frameBuffers_ = device.GetSwapchain().CreateFramebuffers(framebufferCreateInfo);
		}

		// �`�惊�\�[�X�̏�����
		if (!InitializeRenderResource(device, initCmdBuffer))
		{
			return false;
		}

		// �R�}���h�ς݂��ݏI��
		initCmdBuffer.end();

		// �R�}���h��Submit���ďI����҂�
		vk::SubmitInfo copySubmitInfo;
		copySubmitInfo.commandBufferCount = 1;
		copySubmitInfo.pCommandBuffers = &initCmdBuffer;
		device.GetQueue().submit(copySubmitInfo, VK_NULL_HANDLE);
		device.GetQueue().waitIdle();

		// �ꎞ���\�[�X�̔j��
		texStaging_.Destroy();
		vbStaging_.Destroy();
		ibStaging_.Destroy();

		return true;
	}

	//----
	bool Loop(vsl::Device& device)
	{
		auto currentIndex = device.AcquireNextImage();
		auto& cmdBuffer = device.BeginMainCommandBuffer();
		auto& currentImage = device.GetCurrentSwapchainImage();

		// �o�b�t�@�N���A
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

			// �N���A���邽�߂Ƀ��C�A�E�g��ύX
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

			// �N���A
			cmdBuffer.clearColorImage(currentImage, vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearDepthStencilImage(depthBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearDepth, depthSubRange);

			// �`��̂��߂Ƀ��C�A�E�g��ύX
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

		vsTest_.Destroy();
		psTest_.Destroy();

		texture_.Destroy();

		vbuffer_.Destroy();
		ibuffer_.Destroy();

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
		// RenderPass�ݒ�
		std::array<vk::AttachmentDescription, 2> attachmentDescs;
		std::array<vk::AttachmentReference, 2> attachmentRefs;

		// �J���[�o�b�t�@�̃A�^�b�`�����g�ݒ�
		attachmentDescs[0].format = device.GetSwapchain().GetFormat();
		attachmentDescs[0].loadOp = vk::AttachmentLoadOp::eDontCare;
		attachmentDescs[0].storeOp = vk::AttachmentStoreOp::eStore;
		attachmentDescs[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attachmentDescs[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		// �[�x�o�b�t�@�̃A�^�b�`�����g�ݒ�
		attachmentDescs[1].format = vk::Format::eD32SfloatS8Uint;
		attachmentDescs[1].loadOp = vk::AttachmentLoadOp::eDontCare;
		attachmentDescs[1].storeOp = vk::AttachmentStoreOp::eDontCare;
		attachmentDescs[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		attachmentDescs[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		// �J���[�o�b�t�@�̃��t�@�����X�ݒ�
		vk::AttachmentReference& colorReference = attachmentRefs[0];
		colorReference.attachment = 0;
		colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

		// �[�x�o�b�t�@�̃��t�@�����X�ݒ�
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

		// RenderPass����
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

	//----
	bool InitializeRenderResource(vsl::Device& device, vk::CommandBuffer& initCmdBuffer)
	{
		// �V�F�[�_������
		if (!vsTest_.CreateFromFile(device, "data/test.vert.spv"))
		{
			return false;
		}
		if (!psTest_.CreateFromFile(device, "data/test.frag.spv"))
		{
			return false;
		}

		// �e�N�X�`���ǂݍ���
		if (!texture_.InitializeFromTgaImage(device, initCmdBuffer, texStaging_, "data/icon.tga"))
		{
			return false;
		}

		// ���_�o�b�t�@
		{
			struct Vertex
			{
				glm::vec3	pos;
				glm::vec4	color;
				glm::vec2	uv;
			};
			Vertex vertexData[] = {
				{ { -0.5f,  0.5f, 0.0f },{ 1.0f, 1.0f, 1.0f, 1.0f },{ 0.0f, 1.0f } },
				{ { 0.5f,  0.5f, 0.0f },{ 1.0f, 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f } },
				{ { -0.5f, -0.5f, 0.0f },{ 1.0f, 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f } },
				{ { 0.5f, -0.5f, 0.0f },{ 1.0f, 1.0f, 1.0f, 1.0f },{ 1.0f, 0.0f } }
			};
			if (!vbStaging_.InitializeAsStaging(device, sizeof(vertexData), vertexData))
			{
				return false;
			}
			if (!vbuffer_.InitializeAsVertexBuffer(device, sizeof(vertexData)))
			{
				return false;
			}
			vbuffer_.CopyFrom(initCmdBuffer, vbStaging_);
		}
		// �C���f�b�N�X�o�b�t�@
		{
			uint32_t indexData[] = { 0, 1, 2, 1, 3, 2 };
			if (!ibStaging_.InitializeAsStaging(device, sizeof(indexData), indexData))
			{
				return false;
			}
			if (!ibuffer_.InitializeAsIndexBuffer(device, sizeof(indexData)))
			{
				return false;
			}
			ibuffer_.CopyFrom(initCmdBuffer, ibStaging_);
		}

		return true;
	}

private:
	vk::RenderPass	renderPass_;
	vsl::Image		depthBuffer_;
	std::vector<vk::Framebuffer>	frameBuffers_;

	vsl::Shader		vsTest_, psTest_;
	vsl::Buffer		vbuffer_, ibuffer_;
	vsl::Image		texture_;

	vsl::Buffer		vbStaging_, ibStaging_, texStaging_;
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
