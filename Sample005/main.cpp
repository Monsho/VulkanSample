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
#include <vsl/render_pass.h>
#include <vsl/gui.h>
#include <imgui.h>


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
	struct Vertex
	{
		glm::vec3	pos;
		glm::vec4	color;
		glm::vec2	uv;
	};

	struct PostVertex
	{
		glm::vec4	pos;
		glm::vec2	uv;
	};

	struct SceneData
	{
		glm::mat4x4		mtxView_;
		glm::mat4x4		mtxProj_;

		SceneData()
			: mtxView_(), mtxProj_()
		{}
	};

	struct MeshData
	{
		glm::mat4x4		mtxModel_;

		MeshData()
			: mtxModel_()
		{}
	};

public:
	MySample()
	{}

	//----
	bool Initialize(vsl::Device& device)
	{
		// �����_�[�p�X�̏�����
		if (!InitializeMeshPass(device))
		{
			return false;
		}
		if (!InitializePostPass(device))
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

		// �I�t�X�N���[���o�b�t�@�̏�����
		if (!offscreenBuffer_.InitializeAsColorBuffer(
			device, initCmdBuffer,
			vk::Format::eB10G11R11UfloatPack32,
			kScreenWidth, kScreenHeight, 1, 1, true))
		{
			return false;
		}

		// ComputeShader�o�͗p�o�b�t�@�̏�����
		if (!computeBuffer_.InitializeAsColorBuffer(
			device, initCmdBuffer,
			vk::Format::eB10G11R11UfloatPack32,
			kScreenWidth, kScreenHeight, 1, 1, true))
		{
			return false;
		}

		// �t���[���o�b�t�@�ݒ�
		{
			std::array<vk::ImageView, 1> views;

			vk::FramebufferCreateInfo framebufferCreateInfo;
			framebufferCreateInfo.renderPass = postPass_.GetPass();
			framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(views.size());
			framebufferCreateInfo.pAttachments = views.data();
			framebufferCreateInfo.width = kScreenWidth;
			framebufferCreateInfo.height = kScreenHeight;
			framebufferCreateInfo.layers = 1;

			// Swapchain����t���[���o�b�t�@�𐶐�����
			frameBuffers_ = device.GetSwapchain().CreateFramebuffers(framebufferCreateInfo);
		}
		{
			std::array<vk::ImageView, 2> views;
			views[0] = offscreenBuffer_.GetView();
			views[1] = depthBuffer_.GetView();

			vk::FramebufferCreateInfo framebufferCreateInfo;
			framebufferCreateInfo.renderPass = meshPass_.GetPass();
			framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(views.size());
			framebufferCreateInfo.pAttachments = views.data();
			framebufferCreateInfo.width = kScreenWidth;
			framebufferCreateInfo.height = kScreenHeight;
			framebufferCreateInfo.layers = 1;

			// Swapchain����t���[���o�b�t�@�𐶐�����
			offscreenFrame_ = device.GetDevice().createFramebuffer(framebufferCreateInfo);
		}

		{
			vk::Format format = device.GetSwapchain().GetFormat();
			if (!gui_.Initialize(device, format, nullptr))
			{
				return false;
			}
		}

		// �`�惊�\�[�X�̏�����
		if (!InitializeRenderResource(device, initCmdBuffer))
		{
			return false;
		}

		// �t�H���g�C���[�W�̏�����
		if (!gui_.CreateFontImage(initCmdBuffer, fontStaging_))
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
		fontStaging_.Destroy();
		texStaging_.Destroy();
		vbStaging_.Destroy();
		ibStaging_.Destroy();

		// �p�C�v���C���̏�����
		if (!InitializePipeline(device))
		{
			return false;
		}
		if (!InitializePostPipeline(device))
		{
			return false;
		}
		if (!InitializeComputePipeline(device))
		{
			return false;
		}

		return true;
	}

	//----
	bool Loop(vsl::Device& device, const vsl::InputData& input)
	{
		auto currentIndex = device.AcquireNextImage();
		auto& cmdBuffer = device.BeginMainCommandBuffer();
		auto& currentImage = device.GetCurrentSwapchainImage();
		
		static float sRotY = 1.0f;

		// GUI
		gui_.BeginNewFrame(kScreenWidth, kScreenHeight, input);
		if (ImGui::Button(isComputeOn_ ? "Compute Enable" : "Compute Disable"))
		{
			isComputeOn_ = !isComputeOn_;

			vk::DescriptorImageInfo postDescInfo(
				sampler_, isComputeOn_ ? computeBuffer_.GetView() : offscreenBuffer_.GetView(), vk::ImageLayout::eGeneral);

			// �f�X�N���v�^�Z�b�g�̏����X�V����
			std::array<vk::WriteDescriptorSet, 1> descSetInfos{
				vk::WriteDescriptorSet(descSets_[1], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &postDescInfo, nullptr, nullptr),
			};
			device.GetDevice().updateDescriptorSets(descSetInfos, nullptr);
		}

		// UniformBuffer���A�b�v�f�[�g����
		{
			SceneData scene;
			scene.mtxView_ = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(), glm::vec3(0.0f, 1.0f, 0.0f));
			scene.mtxProj_ = glm::perspectiveRH(glm::radians(60.0f), static_cast<float>(kScreenWidth) / static_cast<float>(kScreenHeight), 1.0f, 100.0f);

			cmdBuffer.updateBuffer(sceneBuffer_.GetBuffer(), 0, sizeof(scene), reinterpret_cast<uint32_t*>(&scene));
		}

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
			depthBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, depthSubRange);
			offscreenBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, colorSubRange);
			computeBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, colorSubRange);

			// �N���A
			cmdBuffer.clearColorImage(currentImage, vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearColorImage(offscreenBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearColorImage(computeBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearDepthStencilImage(depthBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearDepth, depthSubRange);

			// �`��̂��߂Ƀ��C�A�E�g��ύX
			vsl::Image::SetImageLayout(
				cmdBuffer,
				currentImage,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				colorSubRange);
			depthBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eDepthStencilAttachmentOptimal, depthSubRange);
			offscreenBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eColorAttachmentOptimal, colorSubRange);
			computeBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
		}

		// ���b�V���p�X�J�n
		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = meshPass_.GetPass();
		renderPassBeginInfo.renderArea.extent = vk::Extent2D(kScreenWidth, kScreenHeight);
		renderPassBeginInfo.clearValueCount = 0;
		renderPassBeginInfo.pClearValues = nullptr;
		renderPassBeginInfo.framebuffer = offscreenFrame_;
		cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
		{
			vk::Viewport viewport = vk::Viewport(0.0f, 0.0f, static_cast<float>(kScreenWidth), static_cast<float>(kScreenHeight), 0.0f, 1.0f);
			cmdBuffer.setViewport(0, viewport);

			vk::Rect2D scissor = vk::Rect2D(vk::Offset2D(), vk::Extent2D(kScreenWidth, kScreenHeight));
			cmdBuffer.setScissor(0, scissor);

			// �e���\�[�X���̃o�C���h
			vk::DeviceSize offsets = 0;
			cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeLayout_, 0, descSets_[0], nullptr);
			cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
			cmdBuffer.bindVertexBuffers(0, vbuffer_.GetBuffer(), offsets);
			cmdBuffer.bindIndexBuffer(ibuffer_.GetBuffer(), 0, vk::IndexType::eUint32);

			MeshData mesh1;
			mesh1.mtxModel_ = glm::rotate(glm::mat4x4(), glm::radians(sRotY), glm::vec3(0.0f, 1.0f, 0.0f));
			sRotY += 0.1f; if (sRotY > 360.0f) sRotY -= 360.0f;
			cmdBuffer.pushConstants(pipeLayout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(mesh1), &mesh1);
			cmdBuffer.drawIndexed(6, 1, 0, 0, 1);

			MeshData mesh2 = mesh1;
			mesh2.mtxModel_[3].x = 0.5f;
			mesh2.mtxModel_[3].z = 7.0f;
			cmdBuffer.pushConstants(pipeLayout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(mesh2), &mesh2);
			cmdBuffer.drawIndexed(6, 1, 0, 0, 1);
		}
		cmdBuffer.endRenderPass();

		// �I�t�X�N���[���o�b�t�@�̃��C�A�E�g�ύX
		{
			vk::ImageSubresourceRange colorSubRange;
			colorSubRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			colorSubRange.levelCount = 1;
			colorSubRange.layerCount = 1;

			if (isComputeOn_)
			{
				offscreenBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
			}
			else
			{
				offscreenBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, colorSubRange);
			}
		}
		{
			vk::ImageSubresourceRange depthSubRange;
			depthSubRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
			depthSubRange.levelCount = 1;
			depthSubRange.layerCount = 1;

			depthBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, depthSubRange);
		}

		// Compute Shader�N��
		if (isComputeOn_)
		{
			{
				cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline_);
				cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipeLayout_, 0, descSets_[2], nullptr);
				cmdBuffer.dispatch(kScreenWidth / 16, kScreenHeight / 16, 1);
			}

			// Compute�o�̓o�b�t�@�̃��C�A�E�g�ύX
			{
				vk::ImageSubresourceRange colorSubRange;
				colorSubRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				colorSubRange.levelCount = 1;
				colorSubRange.layerCount = 1;

				computeBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, colorSubRange);
			}
		}

		// �|�X�g�p�X�J�n
		renderPassBeginInfo.renderPass = postPass_.GetPass();
		renderPassBeginInfo.renderArea.extent = vk::Extent2D(kScreenWidth, kScreenHeight);
		renderPassBeginInfo.clearValueCount = 0;
		renderPassBeginInfo.pClearValues = nullptr;
		renderPassBeginInfo.framebuffer = frameBuffers_[currentIndex];
		cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
		{
			// �e���\�[�X���̃o�C���h
			vk::DeviceSize offsets = 0;
			cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, postPipeLayout_, 0, descSets_[1], nullptr);
			cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, postPipeline_);

			cmdBuffer.draw(4, 1, 0, 0);
		}
		cmdBuffer.endRenderPass();

		// imgui render.
		gui_.SetPassBeginInfo(renderPassBeginInfo);
		ImGui::Render();

		device.ReadyPresentAndEndMainCommandBuffer();
		device.SubmitAndPresent();

		return true;
	}

	//----
	void Terminate(vsl::Device& device)
	{
		vk::Device& d = device.GetDevice();

		gui_.Destroy();

		d.destroyPipeline(postPipeline_);
		d.destroyPipelineLayout(postPipeLayout_);
		d.destroyPipeline(pipeline_);
		d.destroyPipelineLayout(pipeLayout_);

		vsTest_.Destroy();
		psTest_.Destroy();
		vsPost_.Destroy();
		psPost_.Destroy();
		csTest_.Destroy();

		for (auto& dl : descLayouts_)
		{
			d.destroyDescriptorSetLayout(dl);
		}
		d.destroyDescriptorPool(descPool_);

		d.destroySampler(sampler_);
		texture_.Destroy();

		vbuffer_.Destroy();
		ibuffer_.Destroy();
		sceneBuffer_.Destroy();

		d.destroyFramebuffer(offscreenFrame_);
		for (auto& fb : frameBuffers_)
		{
			d.destroyFramebuffer(fb);
		}
		offscreenBuffer_.Destroy();
		computeBuffer_.Destroy();
		depthBuffer_.Destroy();
		meshPass_.Destroy();
		postPass_.Destroy();
	}

private:
	//----
	bool InitializeMeshPass(vsl::Device& device)
	{
		vk::Format colorFormat = vk::Format::eB10G11R11UfloatPack32;
		vk::Format depthFormat = vk::Format::eD32SfloatS8Uint;

		return meshPass_.InitializeAsColorStandard(device, colorFormat, depthFormat);
	}
	//----
	bool InitializePostPass(vsl::Device& device)
	{
		vk::Format colorFormat = device.GetSwapchain().GetFormat();

		return postPass_.InitializeAsColorStandard(device, colorFormat, nullptr);
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
		if (!vsPost_.CreateFromFile(device, "data/post.vert.spv"))
		{
			return false;
		}
		if (!psPost_.CreateFromFile(device, "data/post.frag.spv"))
		{
			return false;
		}
		if (!csTest_.CreateFromFile(device, "data/test.comp.spv"))
		{
			return false;
		}

		// �e�N�X�`���ǂݍ���
		if (!texture_.InitializeFromTgaImage(device, initCmdBuffer, texStaging_, "data/icon.tga"))
		{
			return false;
		}

		// �T���v��
		{
			vk::SamplerCreateInfo samplerCreateInfo;
			samplerCreateInfo.magFilter = vk::Filter::eLinear;
			samplerCreateInfo.minFilter = vk::Filter::eLinear;
			samplerCreateInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
			samplerCreateInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
			samplerCreateInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
			samplerCreateInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
			samplerCreateInfo.mipLodBias = 0.0f;
			samplerCreateInfo.compareOp = vk::CompareOp::eNever;
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = 0.0f;
			samplerCreateInfo.maxAnisotropy = 8;
			samplerCreateInfo.anisotropyEnable = VK_TRUE;
			samplerCreateInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
			sampler_ = device.GetDevice().createSampler(samplerCreateInfo);
		}

		// ���_�o�b�t�@
		{
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

		// �V�[���p�̒萔�o�b�t�@
		{
			SceneData data;
			if (!sceneBuffer_.InitializeAsUniformBuffer(device, sizeof(SceneData), &data))
			{
				return false;
			}
		}

		// �f�X�N���v�^�Z�b�g�𐶐�
		{
			// �f�X�N���v�^�v�[�����쐬����
			{
				std::array<vk::DescriptorPoolSize, 3> typeCounts;
				typeCounts[0].type = vk::DescriptorType::eUniformBuffer;
				typeCounts[0].descriptorCount = 2;
				typeCounts[1].type = vk::DescriptorType::eCombinedImageSampler;
				typeCounts[1].descriptorCount = 3;
				typeCounts[2].type = vk::DescriptorType::eStorageImage;
				typeCounts[2].descriptorCount = 2;

				// �f�X�N���v�^�v�[���𐶐�
				vk::DescriptorPoolCreateInfo descriptorPoolInfo;
				descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(typeCounts.size());
				descriptorPoolInfo.pPoolSizes = typeCounts.data();
				descriptorPoolInfo.maxSets = 3;
				descPool_ = device.GetDevice().createDescriptorPool(descriptorPoolInfo);
			}

			// �f�X�N���v�^�Z�b�g���C�A�E�g���쐬����
			{
				// �`�掞�̃V�F�[�_�Z�b�g�ɑ΂���f�X�N���v�^�Z�b�g�̃��C�A�E�g���w�肷��
				std::array<vk::DescriptorSetLayoutBinding, 2> layoutBindings;
				// UniformBuffer for VertexShader
				layoutBindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
				layoutBindings[0].descriptorCount = 1;
				layoutBindings[0].binding = 0;
				layoutBindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
				layoutBindings[0].pImmutableSamplers = nullptr;
				// CombinedImageSampler for PixelShader
				layoutBindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
				layoutBindings[1].descriptorCount = 1;
				layoutBindings[1].binding = 1;
				layoutBindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
				layoutBindings[1].pImmutableSamplers = nullptr;

				// ���C�A�E�g�𐶐�
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				descLayouts_.push_back(device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr));
			}
			{
				// �`�掞�̃V�F�[�_�Z�b�g�ɑ΂���f�X�N���v�^�Z�b�g�̃��C�A�E�g���w�肷��
				std::array<vk::DescriptorSetLayoutBinding, 2> layoutBindings;
				// CombinedImageSampler (Color buffer) for PixelShader
				layoutBindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
				layoutBindings[0].descriptorCount = 1;
				layoutBindings[0].binding = 1;
				layoutBindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;
				layoutBindings[0].pImmutableSamplers = nullptr;
				// CombinedImageSampler (Depth buffer) for PixelShader
				layoutBindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
				layoutBindings[1].descriptorCount = 1;
				layoutBindings[1].binding = 2;
				layoutBindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
				layoutBindings[1].pImmutableSamplers = nullptr;

				// ���C�A�E�g�𐶐�
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				descLayouts_.push_back(device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr));
			}
			{
				// �`�掞�̃V�F�[�_�Z�b�g�ɑ΂���f�X�N���v�^�Z�b�g�̃��C�A�E�g���w�肷��
				std::array<vk::DescriptorSetLayoutBinding, 2> layoutBindings;
				// read only image for ComputeShader
				layoutBindings[0].descriptorType = vk::DescriptorType::eStorageImage;
				layoutBindings[0].descriptorCount = 1;
				layoutBindings[0].binding = 0;
				layoutBindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
				layoutBindings[0].pImmutableSamplers = nullptr;
				// write only image for ComputeShader
				layoutBindings[1].descriptorType = vk::DescriptorType::eStorageImage;
				layoutBindings[1].descriptorCount = 1;
				layoutBindings[1].binding = 1;
				layoutBindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;
				layoutBindings[1].pImmutableSamplers = nullptr;

				// ���C�A�E�g�𐶐�
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				descLayouts_.push_back(device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr));
			}

			// �f�X�N���v�^�Z�b�g���쐬����
			{
				vk::DescriptorImageInfo texDescInfo(
					sampler_, texture_.GetView(), vk::ImageLayout::eGeneral);

				vk::DescriptorImageInfo postDescInfo(
					sampler_, computeBuffer_.GetView(), vk::ImageLayout::eGeneral);

				vk::DescriptorImageInfo postDepthDescInfo(
					sampler_, depthBuffer_.GetDepthView(), vk::ImageLayout::eGeneral);

				vk::DescriptorImageInfo computeInDescInfo(
					vk::Sampler(), offscreenBuffer_.GetView(), vk::ImageLayout::eGeneral);

				vk::DescriptorImageInfo computeOutDescInfo(
					vk::Sampler(), computeBuffer_.GetView(), vk::ImageLayout::eGeneral);

				vk::DescriptorBufferInfo dbInfo = sceneBuffer_.GetDescInfo();

				// �f�X�N���v�^�Z�b�g�͍쐬�ς݂̃f�X�N���v�^�v�[������m�ۂ���
				vk::DescriptorSetAllocateInfo allocInfo;
				allocInfo.descriptorPool = descPool_;
				allocInfo.descriptorSetCount = static_cast<uint32_t>(descLayouts_.size());
				allocInfo.pSetLayouts = descLayouts_.data();
				descSets_ = device.GetDevice().allocateDescriptorSets(allocInfo);

				// �f�X�N���v�^�Z�b�g�̏����X�V����
				std::array<vk::WriteDescriptorSet, 6> descSetInfos{
					vk::WriteDescriptorSet(descSets_[0], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dbInfo, nullptr),
					vk::WriteDescriptorSet(descSets_[0], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[1], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &postDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[1], 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &postDepthDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[2], 0, 0, 1, vk::DescriptorType::eStorageImage, &computeInDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[2], 1, 0, 1, vk::DescriptorType::eStorageImage, &computeOutDescInfo, nullptr, nullptr),
				};
				device.GetDevice().updateDescriptorSets(descSetInfos, nullptr);
			}
		}

		return true;
	}

	bool InitializePipeline(vsl::Device& device)
	{
		{
			// �����PushConstant�𗘗p����
			// �����ȃT�C�Y�̒萔�o�b�t�@�̓R�}���h�o�b�t�@�ɏ悹�ăV�F�[�_�ɑ��邱�Ƃ��\
			vk::PushConstantRange pushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(MeshData));

			// �f�X�N���v�^�Z�b�g���C�A�E�g�ɑΉ������p�C�v���C�����C�A�E�g�𐶐�����
			// �ʏ��1��1�Ő�������̂��ȁH
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[0];
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
			pipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}

		// �`��g�|���W�̐ݒ�
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
		inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

		// ���X�^���C�Y�X�e�[�g�̐ݒ�
		vk::PipelineRasterizationStateCreateInfo rasterizationState;
		rasterizationState.polygonMode = vk::PolygonMode::eFill;
		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
		rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizationState.depthClampEnable = VK_FALSE;
		rasterizationState.rasterizerDiscardEnable = VK_FALSE;
		rasterizationState.depthBiasEnable = VK_FALSE;
		rasterizationState.lineWidth = 1.0f;

		// �u�����h���[�h�̐ݒ�
		vk::PipelineColorBlendStateCreateInfo colorBlendState;
		vk::PipelineColorBlendAttachmentState blendAttachmentState[1] = {};
		blendAttachmentState[0].colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		blendAttachmentState[0].blendEnable = VK_TRUE;
		blendAttachmentState[0].colorBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState[0].srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		blendAttachmentState[0].dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		blendAttachmentState[0].alphaBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState[0].srcAlphaBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState[0].dstAlphaBlendFactor = vk::BlendFactor::eZero;
		colorBlendState.attachmentCount = ARRAYSIZE(blendAttachmentState);
		colorBlendState.pAttachments = blendAttachmentState;

		// Viewport�X�e�[�g�̐ݒ�
		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// DynamicState�𗘗p����Viewport��ScissorBox��ύX�ł���悤�ɂ��Ă���
		vk::PipelineDynamicStateCreateInfo dynamicState;
		std::vector<vk::DynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(vk::DynamicState::eViewport);
		dynamicStateEnables.push_back(vk::DynamicState::eScissor);
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// DepthStensil�X�e�[�g�̐ݒ�
		vk::PipelineDepthStencilStateCreateInfo depthStencilState;
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
		depthStencilState.depthBoundsTestEnable = VK_FALSE;
		depthStencilState.back.failOp = vk::StencilOp::eKeep;
		depthStencilState.back.passOp = vk::StencilOp::eKeep;
		depthStencilState.back.compareOp = vk::CompareOp::eAlways;
		depthStencilState.stencilTestEnable = VK_FALSE;
		depthStencilState.front = depthStencilState.back;

		// �}���`�T���v���X�e�[�g
		vk::PipelineMultisampleStateCreateInfo multisampleState;
		multisampleState.pSampleMask = NULL;
		multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// �V�F�[�_�ݒ�
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
		shaderStages[0].module = vsTest_.GetModule();
		shaderStages[0].pName = "main";
		shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
		shaderStages[1].module = psTest_.GetModule();
		shaderStages[1].pName = "main";

		// ���_����
		std::array<vk::VertexInputBindingDescription, 1> bindDescs;
		bindDescs[0].binding = 0;		// 0�Ԃփo�C���h
		bindDescs[0].stride = sizeof(Vertex);
		bindDescs[0].inputRate = vk::VertexInputRate::eVertex;

		std::array<vk::VertexInputAttributeDescription, 3> attribDescs;
		// Position
		attribDescs[0].binding = 0;
		attribDescs[0].location = 0;
		attribDescs[0].format = vk::Format::eR32G32B32Sfloat;
		attribDescs[0].offset = 0;
		// Color
		attribDescs[1].binding = 0;
		attribDescs[1].location = 1;
		attribDescs[1].format = vk::Format::eR32G32B32A32Sfloat;
		attribDescs[1].offset = sizeof(glm::vec3);
		// UV
		attribDescs[2].binding = 0;
		attribDescs[2].location = 2;
		attribDescs[2].format = vk::Format::eR32G32Sfloat;
		attribDescs[2].offset = attribDescs[1].offset + sizeof(glm::vec4);

		vk::PipelineVertexInputStateCreateInfo vinputState;
		vinputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindDescs.size());
		vinputState.pVertexBindingDescriptions = bindDescs.data();
		vinputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDescs.size());
		vinputState.pVertexAttributeDescriptions = attribDescs.data();

		// �p�C�v���C�����Ɋe��X�e�[�g��ݒ肵�Đ���
		vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
		pipelineCreateInfo.layout = pipeLayout_;
		pipelineCreateInfo.renderPass = meshPass_.GetPass();
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.pVertexInputState = &vinputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;

		pipeline_ = device.GetDevice().createGraphicsPipelines(device.GetPipelineCache(), pipelineCreateInfo, nullptr)[0];
		if (!pipeline_)
		{
			return false;
		}

		return true;
	}

	bool InitializePostPipeline(vsl::Device& device)
	{
		{
			// �f�X�N���v�^�Z�b�g���C�A�E�g�ɑΉ������p�C�v���C�����C�A�E�g�𐶐�����
			// �ʏ��1��1�Ő�������̂��ȁH
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[1];
			postPipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}

		// �`��g�|���W�̐ݒ�
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
		inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleStrip;

		// ���X�^���C�Y�X�e�[�g�̐ݒ�
		vk::PipelineRasterizationStateCreateInfo rasterizationState;
		rasterizationState.polygonMode = vk::PolygonMode::eFill;
		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
		rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizationState.depthClampEnable = VK_FALSE;
		rasterizationState.rasterizerDiscardEnable = VK_FALSE;
		rasterizationState.depthBiasEnable = VK_FALSE;
		rasterizationState.lineWidth = 1.0f;

		// �u�����h���[�h�̐ݒ�
		vk::PipelineColorBlendStateCreateInfo colorBlendState;
		vk::PipelineColorBlendAttachmentState blendAttachmentState[1] = {};
		blendAttachmentState[0].colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		blendAttachmentState[0].blendEnable = VK_TRUE;
		blendAttachmentState[0].colorBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState[0].srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		blendAttachmentState[0].dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		blendAttachmentState[0].alphaBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState[0].srcAlphaBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState[0].dstAlphaBlendFactor = vk::BlendFactor::eZero;
		colorBlendState.attachmentCount = ARRAYSIZE(blendAttachmentState);
		colorBlendState.pAttachments = blendAttachmentState;

		// Viewport�X�e�[�g�̐ݒ�
		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// DynamicState�𗘗p����Viewport��ScissorBox��ύX�ł���悤�ɂ��Ă���
		vk::PipelineDynamicStateCreateInfo dynamicState;
		std::vector<vk::DynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(vk::DynamicState::eViewport);
		dynamicStateEnables.push_back(vk::DynamicState::eScissor);
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// DepthStensil�X�e�[�g�̐ݒ�
		vk::PipelineDepthStencilStateCreateInfo depthStencilState;
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
		depthStencilState.depthBoundsTestEnable = VK_FALSE;
		depthStencilState.back.failOp = vk::StencilOp::eKeep;
		depthStencilState.back.passOp = vk::StencilOp::eKeep;
		depthStencilState.back.compareOp = vk::CompareOp::eAlways;
		depthStencilState.stencilTestEnable = VK_FALSE;
		depthStencilState.front = depthStencilState.back;

		// �}���`�T���v���X�e�[�g
		vk::PipelineMultisampleStateCreateInfo multisampleState;
		multisampleState.pSampleMask = NULL;
		multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// �V�F�[�_�ݒ�
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
		shaderStages[0].module = vsPost_.GetModule();
		shaderStages[0].pName = "main";
		shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
		shaderStages[1].module = psPost_.GetModule();
		shaderStages[1].pName = "main";

		// ���_����
		vk::PipelineVertexInputStateCreateInfo vinputState;

		// �p�C�v���C�����Ɋe��X�e�[�g��ݒ肵�Đ���
		vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
		pipelineCreateInfo.layout = postPipeLayout_;
		pipelineCreateInfo.renderPass = postPass_.GetPass();
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.pVertexInputState = &vinputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;

		postPipeline_ = device.GetDevice().createGraphicsPipelines(device.GetPipelineCache(), pipelineCreateInfo, nullptr)[0];
		if (!postPipeline_)
		{
			return false;
		}

		return true;
	}

	bool InitializeComputePipeline(vsl::Device& device)
	{
		{
			// �f�X�N���v�^�Z�b�g���C�A�E�g�ɑΉ������p�C�v���C�����C�A�E�g�𐶐�����
			// �ʏ��1��1�Ő�������̂��ȁH
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[2];
			computePipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}

		// �V�F�[�_�X�e�[�W�̐ݒ�
		vk::PipelineShaderStageCreateInfo shaderInfo(vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eCompute, csTest_.GetModule(), "main");

		// �p�C�v���C������
		vk::ComputePipelineCreateInfo pipelineCreateInfo(vk::PipelineCreateFlags(), shaderInfo, computePipeLayout_);

		computePipeline_ = device.GetDevice().createComputePipeline(device.GetPipelineCache(), pipelineCreateInfo);
		if (!computePipeline_)
		{
			return false;
		}

		return true;
	}

private:
	vsl::RenderPass	meshPass_, postPass_;
	vsl::Image		depthBuffer_;
	vsl::Image		offscreenBuffer_, computeBuffer_;
	std::vector<vk::Framebuffer>	frameBuffers_;
	vk::Framebuffer	offscreenFrame_;

	vsl::Shader		vsTest_, psTest_;
	vsl::Shader		vsPost_, psPost_;
	vsl::Shader		csTest_;
	vsl::Buffer		vbuffer_, ibuffer_;
	vsl::Buffer		sceneBuffer_;
	vsl::Image		texture_;
	vk::Sampler		sampler_;

	vk::DescriptorPool						descPool_;
	std::vector<vk::DescriptorSetLayout>	descLayouts_;
	std::vector<vk::DescriptorSet>			descSets_;

	vk::PipelineLayout	pipeLayout_;
	vk::Pipeline		pipeline_;

	vk::PipelineLayout	postPipeLayout_;
	vk::Pipeline		postPipeline_;

	vk::PipelineLayout	computePipeLayout_;
	vk::Pipeline		computePipeline_;

	vsl::Gui		gui_;

	vsl::Buffer		vbStaging_, ibStaging_, texStaging_, fontStaging_;

	bool isComputeOn_{ true };
};	// class MySample

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
	MySample mySample;

	using namespace std::placeholders;
	vsl::Application app(hInstance,
		std::bind(&MySample::Initialize, std::ref(mySample), _1),
		std::bind(&MySample::Loop, std::ref(mySample), _1, _2),
		std::bind(&MySample::Terminate, std::ref(mySample), _1));
	app.Run(kScreenWidth, kScreenHeight);

	return 0;
}


//	EOF
