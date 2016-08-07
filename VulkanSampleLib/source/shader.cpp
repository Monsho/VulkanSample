#include <vsl/shader.h>
#include <vsl/device.h>


namespace vsl
{
	//----
	bool Shader::CreateFromFile(Device& owner, const std::string& filename)
	{
		// ファイル読み込み
		std::vector<uint8_t> bin;
		FILE* fp = nullptr;
		if (fopen_s(&fp, filename.c_str(), "rb") != 0)
		{
			assert(!"Do NOT read shader file.\n");
			return false;
		}
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		bin.resize(size);
		fread(bin.data(), size, 1, fp);
		fclose(fp);

		return CreateFromMemory(owner, bin.data(), size);
	}

	//----
	bool Shader::CreateFromMemory(Device& owner, const void* pBin, size_t size)
	{
		// 作成済みの場合は失敗
		if (module_)
		{
			return false;
		}

		// Shaderモジュール作成
		vk::ShaderModuleCreateInfo moduleCreateInfo;
		moduleCreateInfo.codeSize = size;
		moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(pBin);
		module_ = owner.GetDevice().createShaderModule(moduleCreateInfo);

		pOwner_ = &owner;
		return module_.operator bool();
	}

	//----
	void Shader::Destroy()
	{
		if (pOwner_ && module_)
		{
			pOwner_->GetDevice().destroyShaderModule(module_);

			module_ = vk::ShaderModule();
			pOwner_ = nullptr;
		}
	}

}	// namespace vsl


//	EOF
