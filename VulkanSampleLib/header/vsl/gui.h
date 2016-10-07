#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <imgui.h>
#include <vsl/render_pass.h>
#include <vsl/shader.h>
#include <vsl/image.h>


namespace vsl
{
	class Device;
	class Buffer;

	class Gui
	{
		friend class GuiDetail;

	public:
		Gui()
		{}
		~Gui()
		{
			Destroy();
		}

		// 初期化
		bool Initialize(Device& owner, Image& renderTarget, Image* pDepthTarget);
		// 破棄
		void Destroy();

		// フォントイメージ生成
		bool CreateFontImage(vk::CommandBuffer& cmdBuff, Buffer& staging);

	private:
		Device*		pOwner_{ nullptr };

		RenderPass	renderPass_;
		Shader		vshader_, pshader_;
		Image		fontTexture_;

		vk::Sampler				fontSampler_;
		vk::DescriptorSetLayout	descSetLayout_;
		vk::DescriptorPool		descPool_;
		vk::DescriptorSet		descSet_;
		vk::PipelineLayout		pipelineLayout_;
		vk::Pipeline			pipeline_;
	};	// class Gui

}	// namespace vsl


//	EOF
