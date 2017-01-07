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
		// レンダーパスの初期化
		if (!InitializeMeshPass(device))
		{
			return false;
		}
		if (!InitializePostPass(device))
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

		// オフスクリーンバッファの初期化
		if (!offscreenBuffer_.InitializeAsColorBuffer(
			device, initCmdBuffer,
			vk::Format::eB10G11R11UfloatPack32,
			kScreenWidth, kScreenHeight, 1, 1, true))
		{
			return false;
		}

		// ComputeShader出力用バッファの初期化
		if (!computeBuffer_.InitializeAsColorBuffer(
			device, initCmdBuffer,
			vk::Format::eB10G11R11UfloatPack32,
			kScreenWidth, kScreenHeight, 1, 1, true))
		{
			return false;
		}

		// FFT用バッファの初期化
		for (auto& image : fftTargets_)
		{
			if (!image.InitializeAsColorBuffer(
				device, initCmdBuffer,
				vk::Format::eR16G16B16A16Sfloat,
				256, 256, 1, 1, true))
			{
				return false;
			}
			image.SetImageLayout(initCmdBuffer, vk::ImageLayout::eGeneral, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		}

		// フレームバッファ設定
		{
			std::array<vk::ImageView, 1> views;

			vk::FramebufferCreateInfo framebufferCreateInfo;
			framebufferCreateInfo.renderPass = postPass_.GetPass();
			framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(views.size());
			framebufferCreateInfo.pAttachments = views.data();
			framebufferCreateInfo.width = kScreenWidth;
			framebufferCreateInfo.height = kScreenHeight;
			framebufferCreateInfo.layers = 1;

			// Swapchainからフレームバッファを生成する
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

			// Swapchainからフレームバッファを生成する
			offscreenFrame_ = device.GetDevice().createFramebuffer(framebufferCreateInfo);
		}

		{
			vk::Format format = device.GetSwapchain().GetFormat();
			if (!gui_.Initialize(device, format, nullptr))
			{
				return false;
			}
		}

		// 描画リソースの初期化
		if (!InitializeRenderResource(device, initCmdBuffer))
		{
			return false;
		}

		// フォントイメージの初期化
		if (!gui_.CreateFontImage(initCmdBuffer, fontStaging_))
		{
			return false;
		}

		// コマンド積みこみ終了
		initCmdBuffer.end();

		// コマンドをSubmitして終了を待つ
		vk::SubmitInfo copySubmitInfo;
		copySubmitInfo.commandBufferCount = 1;
		copySubmitInfo.pCommandBuffers = &initCmdBuffer;
		device.GetQueue().submit(copySubmitInfo, VK_NULL_HANDLE);
		device.GetQueue().waitIdle();

		// 一時リソースの破棄
		fontStaging_.Destroy();
		texStaging_.Destroy();
		vbStaging_.Destroy();
		ibStaging_.Destroy();

		// パイプラインの初期化
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
		if (!InitializeFFTPipeline(device))
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

			// デスクリプタセットの情報を更新する
			std::array<vk::WriteDescriptorSet, 1> descSetInfos{
				vk::WriteDescriptorSet(descSets_[1], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &postDescInfo, nullptr, nullptr),
			};
			device.GetDevice().updateDescriptorSets(descSetInfos, nullptr);
		}
		ImGui::Checkbox("Sync FFT", &isSyncFFT_);
		if (ImGui::Button("Compute FFT"))
		{
			RunFFT(device);
		}
		EndCalcFFT(device);

		static const char* kViewTypeStrs[] = {"Texture", "FFT", "InvFFT"};
		if (ImGui::Combo("View Type", &viewType_, kViewTypeStrs, ARRAYSIZE(kViewTypeStrs)) || isForceTypeChange_)
		{
			if (isFFTComplete_)
			{
				if (viewType_ == 0)
				{
					vk::DescriptorImageInfo texDescInfo(
						sampler_, texture_.GetView(), vk::ImageLayout::eGeneral);

					// デスクリプタセットの情報を更新する
					std::array<vk::WriteDescriptorSet, 1> descSetInfos{
						vk::WriteDescriptorSet(descSets_[0], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescInfo, nullptr, nullptr),
					};
					device.GetDevice().updateDescriptorSets(descSetInfos, nullptr);
				}
				else if (viewType_ == 2)
				{
					vk::DescriptorImageInfo texDescInfo(
						sampler_, fftTargets_[4].GetView(), vk::ImageLayout::eGeneral);

					// デスクリプタセットの情報を更新する
					std::array<vk::WriteDescriptorSet, 1> descSetInfos{
						vk::WriteDescriptorSet(descSets_[0], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescInfo, nullptr, nullptr),
					};
					device.GetDevice().updateDescriptorSets(descSetInfos, nullptr);
				}
			}
		}

		// UniformBufferをアップデートする
		{
			SceneData scene;
			scene.mtxView_ = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(), glm::vec3(0.0f, 1.0f, 0.0f));
			scene.mtxProj_ = glm::perspectiveRH(glm::radians(60.0f), static_cast<float>(kScreenWidth) / static_cast<float>(kScreenHeight), 1.0f, 100.0f);

			cmdBuffer.updateBuffer(sceneBuffer_.GetBuffer(), 0, sizeof(scene), reinterpret_cast<uint32_t*>(&scene));
		}

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
			depthBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, depthSubRange);
			offscreenBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, colorSubRange);
			computeBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, colorSubRange);

			// クリア
			cmdBuffer.clearColorImage(currentImage, vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearColorImage(offscreenBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearColorImage(computeBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearColor, colorSubRange);
			cmdBuffer.clearDepthStencilImage(depthBuffer_.GetImage(), vk::ImageLayout::eTransferDstOptimal, clearDepth, depthSubRange);

			// 描画のためにレイアウトを変更
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

		// メッシュパス開始
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

			// 各リソース等のバインド
			vk::DeviceSize offsets = 0;
			if (!isFFTComplete_ || (viewType_ != 1))
			{
				cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeLayout_, 0, descSets_[0], nullptr);
				cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
			}
			else
			{
				cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, fftViewPipeLayout_, 0, descSets_[3], nullptr);
				cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, fftViewPipeline_);
			}
			cmdBuffer.bindVertexBuffers(0, vbuffer_.GetBuffer(), offsets);
			cmdBuffer.bindIndexBuffer(ibuffer_.GetBuffer(), 0, vk::IndexType::eUint32);

			static float sAngle = 0.0f;
			MeshData mesh1;
			mesh1.mtxModel_ = glm::scale(glm::mat4x4(), glm::vec3(4.0f, 4.0f, 1.0f));
			mesh1.mtxModel_ = glm::rotate(mesh1.mtxModel_, glm::radians(sAngle), glm::vec3(0.0f, 1.0f, 0.0f));
			sAngle += 0.1f; if (sAngle > 360.0f) sAngle -= 360.0f;
			mesh1.mtxModel_[3].z = 0.0f;
			cmdBuffer.pushConstants(pipeLayout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(mesh1), &mesh1);
			cmdBuffer.drawIndexed(6, 1, 0, 0, 1);
		}
		cmdBuffer.endRenderPass();

		// オフスクリーンバッファのレイアウト変更
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

		// Compute Shader起動
		if (isComputeOn_)
		{
			{
				cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline_);
				cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipeLayout_, 0, descSets_[2], nullptr);
				cmdBuffer.dispatch(kScreenWidth / 16, kScreenHeight / 16, 1);
			}

			// Compute出力バッファのレイアウト変更
			{
				vk::ImageSubresourceRange colorSubRange;
				colorSubRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				colorSubRange.levelCount = 1;
				colorSubRange.layerCount = 1;

				computeBuffer_.SetImageLayout(cmdBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, colorSubRange);
			}
		}

		// ポストパス開始
		renderPassBeginInfo.renderPass = postPass_.GetPass();
		renderPassBeginInfo.renderArea.extent = vk::Extent2D(kScreenWidth, kScreenHeight);
		renderPassBeginInfo.clearValueCount = 0;
		renderPassBeginInfo.pClearValues = nullptr;
		renderPassBeginInfo.framebuffer = frameBuffers_[currentIndex];
		cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
		{
			vk::Viewport viewport = vk::Viewport(0.0f, 0.0f, static_cast<float>(kScreenWidth), static_cast<float>(kScreenHeight), 0.0f, 1.0f);
			cmdBuffer.setViewport(0, viewport);

			vk::Rect2D scissor = vk::Rect2D(vk::Offset2D(), vk::Extent2D(kScreenWidth, kScreenHeight));
			cmdBuffer.setScissor(0, scissor);

			// 各リソース等のバインド
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

		if (isSyncFFT_ && computeSemaphore_)
		{
			vk::PipelineStageFlags flag = vk::PipelineStageFlagBits::eBottomOfPipe;
			device.SubmitAndPresent(1, &computeSemaphore_, &flag);
		}
		else
		{
			device.SubmitAndPresent();
		}

		return true;
	}

	//----
	void Terminate(vsl::Device& device)
	{
		vk::Device& d = device.GetDevice();

		if (computeSemaphore_)
		{
			d.destroySemaphore(computeSemaphore_);
		}

		if (computeFence_)
		{
			d.destroyFence(computeFence_);
		}

		gui_.Destroy();

		for (auto& pipe : fftPipelines_)
		{
			d.destroyPipeline(pipe);
		}
		d.destroyPipelineLayout(fftPipeLayout_);
		d.destroyPipeline(computePipeline_);
		d.destroyPipelineLayout(computePipeLayout_);
		d.destroyPipeline(postPipeline_);
		d.destroyPipelineLayout(postPipeLayout_);
		d.destroyPipeline(fftViewPipeline_);
		d.destroyPipelineLayout(fftViewPipeLayout_);
		d.destroyPipeline(pipeline_);
		d.destroyPipelineLayout(pipeLayout_);

		vsTest_.Destroy();
		psTest_.Destroy();
		psView_.Destroy();
		vsPost_.Destroy();
		psPost_.Destroy();
		csTest_.Destroy();
		for (auto& s : csFFTs_)
		{
			s.Destroy();
		}

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
		for (auto& image : fftTargets_)
		{
			image.Destroy();
		}
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
		// シェーダ初期化
		if (!vsTest_.CreateFromFile(device, "data/test.vert.spv"))
		{
			return false;
		}
		if (!psTest_.CreateFromFile(device, "data/test.frag.spv"))
		{
			return false;
		}
		if (!psView_.CreateFromFile(device, "data/fft_view.frag.spv"))
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
		if (!csFFTs_[0].CreateFromFile(device, "data/fft_r.comp.spv")) { return false; }
		if (!csFFTs_[1].CreateFromFile(device, "data/fft_c.comp.spv")) { return false; }
		if (!csFFTs_[2].CreateFromFile(device, "data/ifft_r.comp.spv")) { return false; }
		if (!csFFTs_[3].CreateFromFile(device, "data/ifft_c.comp.spv")) { return false; }

		// テクスチャ読み込み
		if (!texture_.InitializeFromTgaImage(device, initCmdBuffer, texStaging_, "data/default.tga"))
		{
			return false;
		}

		// サンプラ
		{
			vk::SamplerCreateInfo samplerCreateInfo;
			samplerCreateInfo.magFilter = vk::Filter::eNearest;
			samplerCreateInfo.minFilter = vk::Filter::eNearest;
			samplerCreateInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
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

		// 頂点バッファ
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
		// インデックスバッファ
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

		// シーン用の定数バッファ
		{
			SceneData data;
			if (!sceneBuffer_.InitializeAsUniformBuffer(device, sizeof(SceneData), &data))
			{
				return false;
			}
		}

		// デスクリプタセットを生成
		{
			// デスクリプタプールを作成する
			{
				std::array<vk::DescriptorPoolSize, 3> typeCounts;
				typeCounts[0].type = vk::DescriptorType::eUniformBuffer;
				typeCounts[0].descriptorCount = 3;
				typeCounts[1].type = vk::DescriptorType::eCombinedImageSampler;
				typeCounts[1].descriptorCount = 5;
				typeCounts[2].type = vk::DescriptorType::eStorageImage;
				typeCounts[2].descriptorCount = 18;

				// デスクリプタプールを生成
				vk::DescriptorPoolCreateInfo descriptorPoolInfo;
				descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(typeCounts.size());
				descriptorPoolInfo.pPoolSizes = typeCounts.data();
				descriptorPoolInfo.maxSets = 8;
				descPool_ = device.GetDevice().createDescriptorPool(descriptorPoolInfo);
			}

			// デスクリプタセットレイアウトを作成する
			std::vector<vk::DescriptorSetLayout> refLayouts;
			{
				// 描画時のシェーダセットに対するデスクリプタセットのレイアウトを指定する
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

				// レイアウトを生成
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				vk::DescriptorSetLayout sl = device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr);
				descLayouts_.push_back(sl);
				refLayouts.push_back(sl);
			}
			{
				// 描画時のシェーダセットに対するデスクリプタセットのレイアウトを指定する
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

				// レイアウトを生成
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				vk::DescriptorSetLayout sl = device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr);
				descLayouts_.push_back(sl);
				refLayouts.push_back(sl);
			}
			{
				// 描画時のシェーダセットに対するデスクリプタセットのレイアウトを指定する
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

				// レイアウトを生成
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				vk::DescriptorSetLayout sl = device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr);
				descLayouts_.push_back(sl);
				refLayouts.push_back(sl);
			}
			{
				// 描画時のシェーダセットに対するデスクリプタセットのレイアウトを指定する
				std::array<vk::DescriptorSetLayoutBinding, 3> layoutBindings{
					vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex),
					vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
					vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
				};

				// レイアウトを生成
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				vk::DescriptorSetLayout sl = device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr);
				descLayouts_.push_back(sl);
				refLayouts.push_back(sl);
			}
			{
				// 描画時のシェーダセットに対するデスクリプタセットのレイアウトを指定する
				std::array<vk::DescriptorSetLayoutBinding, 4> layoutBindings{
					vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
					vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
					vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
					vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
				};

				// レイアウトを生成
				vk::DescriptorSetLayoutCreateInfo descriptorLayout;
				descriptorLayout.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				descriptorLayout.pBindings = layoutBindings.data();
				vk::DescriptorSetLayout sl = device.GetDevice().createDescriptorSetLayout(descriptorLayout, nullptr);
				descLayouts_.push_back(sl);
				refLayouts.push_back(sl);
				refLayouts.push_back(sl);
				refLayouts.push_back(sl);
				refLayouts.push_back(sl);
			}

			// デスクリプタセットを作成する
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

				vk::DescriptorImageInfo fftvRDescInfo(
					sampler_, fftTargets_[2].GetView(), vk::ImageLayout::eGeneral);
				vk::DescriptorImageInfo fftvIDescInfo(
					sampler_, fftTargets_[3].GetView(), vk::ImageLayout::eGeneral);

				vk::DescriptorImageInfo fftSrcDescInfo(
					vk::Sampler(), texture_.GetView(), vk::ImageLayout::eGeneral);
				vk::DescriptorImageInfo fft0DescInfo(
					vk::Sampler(), fftTargets_[0].GetView(), vk::ImageLayout::eGeneral);
				vk::DescriptorImageInfo fft1DescInfo(
					vk::Sampler(), fftTargets_[1].GetView(), vk::ImageLayout::eGeneral);
				vk::DescriptorImageInfo fft2DescInfo(
					vk::Sampler(), fftTargets_[2].GetView(), vk::ImageLayout::eGeneral);
				vk::DescriptorImageInfo fft3DescInfo(
					vk::Sampler(), fftTargets_[3].GetView(), vk::ImageLayout::eGeneral);
				vk::DescriptorImageInfo fft4DescInfo(
					vk::Sampler(), fftTargets_[4].GetView(), vk::ImageLayout::eGeneral);
				vk::DescriptorImageInfo fft5DescInfo(
					vk::Sampler(), fftTargets_[5].GetView(), vk::ImageLayout::eGeneral);

				vk::DescriptorBufferInfo dbInfo = sceneBuffer_.GetDescInfo();

				// デスクリプタセットは作成済みのデスクリプタプールから確保する
				vk::DescriptorSetAllocateInfo allocInfo;
				allocInfo.descriptorPool = descPool_;
				allocInfo.descriptorSetCount = static_cast<uint32_t>(refLayouts.size());
				allocInfo.pSetLayouts = refLayouts.data();
				descSets_ = device.GetDevice().allocateDescriptorSets(allocInfo);

				// デスクリプタセットの情報を更新する
				std::array<vk::WriteDescriptorSet, 24> descSetInfos{
					vk::WriteDescriptorSet(descSets_[0], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dbInfo, nullptr),
					vk::WriteDescriptorSet(descSets_[0], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[1], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &postDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[1], 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &postDepthDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[2], 0, 0, 1, vk::DescriptorType::eStorageImage, &computeInDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[2], 1, 0, 1, vk::DescriptorType::eStorageImage, &computeOutDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[3], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dbInfo, nullptr),
					vk::WriteDescriptorSet(descSets_[3], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &fftvRDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[3], 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &fftvIDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[4], 0, 0, 1, vk::DescriptorType::eStorageImage, &fftSrcDescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[4], 2, 0, 1, vk::DescriptorType::eStorageImage, &fft0DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[4], 3, 0, 1, vk::DescriptorType::eStorageImage, &fft1DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[5], 0, 0, 1, vk::DescriptorType::eStorageImage, &fft0DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[5], 1, 0, 1, vk::DescriptorType::eStorageImage, &fft1DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[5], 2, 0, 1, vk::DescriptorType::eStorageImage, &fft2DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[5], 3, 0, 1, vk::DescriptorType::eStorageImage, &fft3DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[6], 0, 0, 1, vk::DescriptorType::eStorageImage, &fft2DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[6], 1, 0, 1, vk::DescriptorType::eStorageImage, &fft3DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[6], 2, 0, 1, vk::DescriptorType::eStorageImage, &fft0DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[6], 3, 0, 1, vk::DescriptorType::eStorageImage, &fft1DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[7], 0, 0, 1, vk::DescriptorType::eStorageImage, &fft0DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[7], 1, 0, 1, vk::DescriptorType::eStorageImage, &fft1DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[7], 2, 0, 1, vk::DescriptorType::eStorageImage, &fft4DescInfo, nullptr, nullptr),
					vk::WriteDescriptorSet(descSets_[7], 3, 0, 1, vk::DescriptorType::eStorageImage, &fft5DescInfo, nullptr, nullptr),
				};
				device.GetDevice().updateDescriptorSets(descSetInfos, nullptr);
			}
		}

		return true;
	}

	bool InitializePipeline(vsl::Device& device)
	{
		{
			// 今回はPushConstantを利用する
			// 小さなサイズの定数バッファはコマンドバッファに乗せてシェーダに送ることが可能
			vk::PushConstantRange pushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(MeshData));

			// デスクリプタセットレイアウトに対応したパイプラインレイアウトを生成する
			// 通常は1対1で生成するのかな？
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[0];
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
			pipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}
		{
			// 今回はPushConstantを利用する
			// 小さなサイズの定数バッファはコマンドバッファに乗せてシェーダに送ることが可能
			vk::PushConstantRange pushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(MeshData));

			// デスクリプタセットレイアウトに対応したパイプラインレイアウトを生成する
			// 通常は1対1で生成するのかな？
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[3];
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
			fftViewPipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}

		// 描画トポロジの設定
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
		inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

		// ラスタライズステートの設定
		vk::PipelineRasterizationStateCreateInfo rasterizationState;
		rasterizationState.polygonMode = vk::PolygonMode::eFill;
		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
		rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizationState.depthClampEnable = VK_FALSE;
		rasterizationState.rasterizerDiscardEnable = VK_FALSE;
		rasterizationState.depthBiasEnable = VK_FALSE;
		rasterizationState.lineWidth = 1.0f;

		// ブレンドモードの設定
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

		// Viewportステートの設定
		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// DynamicStateを利用してViewportとScissorBoxを変更できるようにしておく
		vk::PipelineDynamicStateCreateInfo dynamicState;
		std::vector<vk::DynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(vk::DynamicState::eViewport);
		dynamicStateEnables.push_back(vk::DynamicState::eScissor);
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// DepthStensilステートの設定
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

		// マルチサンプルステート
		vk::PipelineMultisampleStateCreateInfo multisampleState;
		multisampleState.pSampleMask = NULL;
		multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// シェーダ設定
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
		shaderStages[0].module = vsTest_.GetModule();
		shaderStages[0].pName = "main";
		shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
		shaderStages[1].module = psTest_.GetModule();
		shaderStages[1].pName = "main";

		// 頂点入力
		std::array<vk::VertexInputBindingDescription, 1> bindDescs;
		bindDescs[0].binding = 0;		// 0番へバインド
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

		// パイプライン情報に各種ステートを設定して生成
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

		shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
		shaderStages[1].module = psView_.GetModule();
		shaderStages[1].pName = "main";

		pipelineCreateInfo.layout = fftViewPipeLayout_;

		fftViewPipeline_ = device.GetDevice().createGraphicsPipelines(device.GetPipelineCache(), pipelineCreateInfo, nullptr)[0];
		if (!fftViewPipeline_)
		{
			return false;
		}

		return true;
	}

	bool InitializePostPipeline(vsl::Device& device)
	{
		{
			// デスクリプタセットレイアウトに対応したパイプラインレイアウトを生成する
			// 通常は1対1で生成するのかな？
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[1];
			postPipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}

		// 描画トポロジの設定
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
		inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleStrip;

		// ラスタライズステートの設定
		vk::PipelineRasterizationStateCreateInfo rasterizationState;
		rasterizationState.polygonMode = vk::PolygonMode::eFill;
		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
		rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizationState.depthClampEnable = VK_FALSE;
		rasterizationState.rasterizerDiscardEnable = VK_FALSE;
		rasterizationState.depthBiasEnable = VK_FALSE;
		rasterizationState.lineWidth = 1.0f;

		// ブレンドモードの設定
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

		// Viewportステートの設定
		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// DynamicStateを利用してViewportとScissorBoxを変更できるようにしておく
		vk::PipelineDynamicStateCreateInfo dynamicState;
		std::vector<vk::DynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(vk::DynamicState::eViewport);
		dynamicStateEnables.push_back(vk::DynamicState::eScissor);
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// DepthStensilステートの設定
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

		// マルチサンプルステート
		vk::PipelineMultisampleStateCreateInfo multisampleState;
		multisampleState.pSampleMask = NULL;
		multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// シェーダ設定
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
		shaderStages[0].module = vsPost_.GetModule();
		shaderStages[0].pName = "main";
		shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
		shaderStages[1].module = psPost_.GetModule();
		shaderStages[1].pName = "main";

		// 頂点入力
		vk::PipelineVertexInputStateCreateInfo vinputState;

		// パイプライン情報に各種ステートを設定して生成
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
			// デスクリプタセットレイアウトに対応したパイプラインレイアウトを生成する
			// 通常は1対1で生成するのかな？
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[2];
			computePipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}

		// シェーダステージの設定
		vk::PipelineShaderStageCreateInfo shaderInfo(vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eCompute, csTest_.GetModule(), "main");

		// パイプライン生成
		vk::ComputePipelineCreateInfo pipelineCreateInfo(vk::PipelineCreateFlags(), shaderInfo, computePipeLayout_);

		computePipeline_ = device.GetDevice().createComputePipeline(device.GetPipelineCache(), pipelineCreateInfo);
		if (!computePipeline_)
		{
			return false;
		}

		return true;
	}

	bool InitializeFFTPipeline(vsl::Device& device)
	{
		{
			// デスクリプタセットレイアウトに対応したパイプラインレイアウトを生成する
			// 通常は1対1で生成するのかな？
			vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &descLayouts_[4];
			fftPipeLayout_ = device.GetDevice().createPipelineLayout(pPipelineLayoutCreateInfo);
		}

		for (int i = 0; i < ARRAYSIZE(fftPipelines_); i++)
		{
			// シェーダステージの設定
			vk::PipelineShaderStageCreateInfo shaderInfo(vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eCompute, csFFTs_[i].GetModule(), "main");

			// パイプライン生成
			vk::ComputePipelineCreateInfo pipelineCreateInfo(vk::PipelineCreateFlags(), shaderInfo, fftPipeLayout_);

			fftPipelines_[i] = device.GetDevice().createComputePipeline(device.GetPipelineCache(), pipelineCreateInfo);
			if (!fftPipelines_[i])
			{
				return false;
			}
		}

		return true;
	}

	void RunFFT(vsl::Device& device)
	{
		if (computeFence_)
		{
			return;
		}

		isForceTypeChange_ = true;
		viewType_ = 0;

		auto& cmdBuffer = device.GetComputeCommandBuffers()[0];

		if (!isFFTCommandLoaded_)
		{
			cmdBuffer.reset(vk::CommandBufferResetFlags());

			// コマンド積み込み開始
			vk::CommandBufferBeginInfo beginInfo;
			cmdBuffer.begin(&beginInfo);

			// FFT計算処理のコマンド積み込み
			for (int i = 0; i < 5000; i++)
			{
				vk::ImageSubresourceRange colorSubRange;
				colorSubRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				colorSubRange.levelCount = 1;
				colorSubRange.layerCount = 1;
				fftTargets_[0].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[1].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);

				// row pass を処理する
				{
					// dispatch
					cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, fftPipelines_[0]);
					cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, fftPipeLayout_, 0, descSets_[4], nullptr);
					cmdBuffer.dispatch(1, texture_.GetHeight(), 1);
				}

				fftTargets_[0].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[1].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[2].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[3].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);

				// collums pass を処理する
				{
					// dispatch
					cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, fftPipelines_[1]);
					cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, fftPipeLayout_, 0, descSets_[5], nullptr);
					cmdBuffer.dispatch(1, texture_.GetWidth(), 1);
				}

				fftTargets_[2].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[3].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[0].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[1].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);

				// invert row pass を処理する
				{
					// dispatch
					cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, fftPipelines_[2]);
					cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, fftPipeLayout_, 0, descSets_[6], nullptr);
					cmdBuffer.dispatch(1, texture_.GetHeight(), 1);
				}

				fftTargets_[0].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[1].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[4].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[5].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);

				// invert collums pass を処理する
				{
					// dispatch
					cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, fftPipelines_[3]);
					cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, fftPipeLayout_, 0, descSets_[7], nullptr);
					cmdBuffer.dispatch(1, texture_.GetWidth(), 1);
				}

				fftTargets_[4].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
				fftTargets_[5].SetImageLayout(cmdBuffer, vk::ImageLayout::eGeneral, colorSubRange);
			}

			// コマンド積み込み完了
			cmdBuffer.end();

			isFFTCommandLoaded_ = true;
		}

		// フェンスの作成
		computeFence_ = device.GetDevice().createFence(vk::FenceCreateInfo());

		// フェンスを使ってSubmit
		vk::SubmitInfo submitInfo;
		submitInfo.pCommandBuffers = &cmdBuffer;
		submitInfo.commandBufferCount = 1;

		// 同期をとる場合はセマフォを作成
		if (isSyncFFT_)
		{
			computeSemaphore_ = device.GetDevice().createSemaphore(vk::SemaphoreCreateInfo());
			submitInfo.pSignalSemaphores = &computeSemaphore_;
			submitInfo.signalSemaphoreCount = 1;
		}

		device.GetComputeQueue().submit(submitInfo, computeFence_);
	}

	void EndCalcFFT(vsl::Device& device)
	{
		if (computeFence_ && (device.GetDevice().getFenceStatus(computeFence_) == vk::Result::eSuccess))
		{
			device.GetDevice().destroyFence(computeFence_);
			computeFence_ = vk::Fence();
			if (computeSemaphore_)
			{
				device.GetDevice().destroySemaphore(computeSemaphore_);
				computeSemaphore_ = vk::Semaphore();
			}

			isFFTComplete_ = true;
			viewType_ = 1;
			OutputDebugString(L"\n");
		}
		else if (computeFence_ && (device.GetDevice().getFenceStatus(computeFence_) != vk::Result::eSuccess))
		{
			OutputDebugString(L".");
		}
	}

private:
	vsl::RenderPass	meshPass_, postPass_;
	vsl::Image		depthBuffer_;
	vsl::Image		offscreenBuffer_, computeBuffer_;
	vsl::Image		fftTargets_[6];
	std::vector<vk::Framebuffer>	frameBuffers_;
	vk::Framebuffer	offscreenFrame_;

	vsl::Shader		vsTest_, psTest_, psView_;
	vsl::Shader		vsPost_, psPost_;
	vsl::Shader		csTest_;
	vsl::Shader		csFFTs_[4];
	vsl::Buffer		vbuffer_, ibuffer_;
	vsl::Buffer		sceneBuffer_;
	vsl::Image		texture_;
	vk::Sampler		sampler_;

	vk::DescriptorPool						descPool_;
	std::vector<vk::DescriptorSetLayout>	descLayouts_;
	std::vector<vk::DescriptorSet>			descSets_;

	vk::PipelineLayout	pipeLayout_;
	vk::Pipeline		pipeline_;
	vk::PipelineLayout	fftViewPipeLayout_;
	vk::Pipeline		fftViewPipeline_;

	vk::PipelineLayout	postPipeLayout_;
	vk::Pipeline		postPipeline_;

	vk::PipelineLayout	computePipeLayout_;
	vk::Pipeline		computePipeline_;

	vk::PipelineLayout	fftPipeLayout_;
	vk::Pipeline		fftPipelines_[4];

	vsl::Gui		gui_;

	vsl::Buffer		vbStaging_, ibStaging_, texStaging_, fontStaging_;

	vk::Fence			computeFence_{};
	vk::Semaphore		computeSemaphore_{};

	bool isComputeOn_{ true };
	bool isFFTComplete_{ false };
	bool isForceTypeChange_{ false };
	bool isSyncFFT_{ false };
	bool isFFTCommandLoaded_{ false };
	int viewType_{ 0 };
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
