#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>


namespace vsl
{
	class Device;

	//----
	class Shader
	{
	public:
		Shader()
		{}
		~Shader()
		{
			Destroy();
		}

		bool CreateFromFile(Device& owner, const std::string& filename);
		bool CreateFromMemory(Device& owner, const void* pBin, size_t size);

		void Destroy();

		// getter
		vk::ShaderModule& GetModule() { return module_; }

	private:
		Device*				pOwner_{ nullptr };
		vk::ShaderModule	module_;
	};	// class Shader

}	// namespace vsl


//	EOF
