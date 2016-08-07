#pragma once

#include <functional>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vsl/device.h>


namespace vsl
{
	//----
	class Application
	{
	public:
		static const uint32_t	kQueueIndexNotFound = 0xffffffff;

	public:
		Application(
			HINSTANCE hInst,
			std::function<bool(Device&)> initF,
			std::function<bool(Device&)> loopF,
			std::function<bool(Device&)> termF)
			: hInstance_(hInst), hWnd_()
			, initFunc_(initF), loopFunc_(loopF), termFunc_(termF)
			, closeRequest_(false)
		{}
		~Application()
		{}

		void Run(uint16_t screenWidth, uint16_t screenHeight);

		// getter
		HINSTANCE	GetInstanceHandle() const	{ return hInstance_; }
		HWND		GetWndHandle() const		{ return hWnd_; }
		uint16_t	GetScreenWidth() const		{ return screenWidth_; }
		uint16_t	GetScreenHeight() const		{ return screenHeight_; }

		Device&		GetDevice() { return device_; }

	private:
		void PollEvents();
		bool InitializeWindow();

	private:
		HINSTANCE	hInstance_;
		HWND		hWnd_;
		uint16_t	screenWidth_;
		uint16_t	screenHeight_;

		Device		device_;

		std::function<bool(Device&)>	initFunc_;
		std::function<bool(Device&)>	loopFunc_;
		std::function<void(Device&)>	termFunc_;

		bool	closeRequest_;
	};	// class Application

}	// namespace vsl


//	EOF
