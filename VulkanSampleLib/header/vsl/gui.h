#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <imgui.h>
#include <vsl/render_pass.h>
#include <vsl/shader.h>
#include <vsl/image.h>
#include <vsl/buffer.h>


namespace vsl
{
	class Device;
	class Buffer;
	class InputData;

	class Gui
	{
	public:
		Gui()
		{}
		~Gui()
		{
			Destroy();
		}

		// 初期化
		bool Initialize(Device& owner, Image& renderTarget, Image* pDepthTarget);
		bool Initialize(Device& owner, vk::ArrayProxy<vk::Format> colorFormats, vk::Optional<vk::Format> depthFormat);
		// 破棄
		void Destroy();

		// フォントイメージ生成
		bool CreateFontImage(vk::CommandBuffer& cmdBuff, Buffer& staging);

		// 新しいフレームの開始
		void BeginNewFrame(uint32_t frameWidth, uint32_t frameHeight, const InputData& input, float frameScale = 1.0f, float timeStep = 1.0f / 60.0f);

		// レンダーパス開始情報を設定する
		void SetPassBeginInfo(const vk::RenderPassBeginInfo& info)
		{
			passBeginInfo_ = info;
			passBeginInfo_.renderPass = renderPass_.GetPass();
		}

	private:
		Device*		pOwner_{ nullptr };

		RenderPass	renderPass_;
		Shader		vshader_, pshader_;
		Image		fontTexture_;

		Buffer*			vertexBuffers_;
		Buffer*			indexBuffers_;
		vk::DeviceSize	nonCoherentAtomSize_;

		vk::Sampler				fontSampler_;
		vk::DescriptorSetLayout	descSetLayout_;
		vk::DescriptorPool		descPool_;
		vk::DescriptorSet		descSet_;
		vk::PipelineLayout		pipelineLayout_;
		vk::Pipeline			pipeline_;
		vk::RenderPassBeginInfo	passBeginInfo_;

	public:
		// 描画命令
		static void RenderDrawList(ImDrawData* draw_data);

	private:
		static Gui* guiHandle_;
	};	// class Gui

}	// namespace vsl


//	EOF
