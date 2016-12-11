#pragma once

#include <functional>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vsl/device.h>


namespace vsl
{
	class MouseButton
	{
	public:
		enum Type
		{
			LEFT = 0x01 << 0,
			RIGHT = 0x01 << 1,
			MIDDLE = 0x01 << 2,
		};
	};	// class MouseButton

	class InputData
	{
		friend class Application;

	public:
		int GetMouseX() const { return mouseX_; }
		int GetMouseY() const { return mouseY_; }
		bool IsMouseButtonPressed(MouseButton::Type btn) const { return (mouseButton_ & (int)btn) != 0; }

	public:
		int mouseX_{ 0 }, mouseY_{ 0 };
		int mouseButton_{ 0 };
	};	// class InputData

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
			std::function<void(Device&)> termF,
			std::function<void(const InputData&)> inputF)
			: hInstance_(hInst), hWnd_()
			, initFunc_(initF), loopFunc_(loopF), termFunc_(termF), inputFunc_(inputF)
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

		std::function<bool(Device&)>			initFunc_;
		std::function<bool(Device&)>			loopFunc_;
		std::function<void(Device&)>			termFunc_;
		std::function<void(const InputData&)>	inputFunc_;

		bool	closeRequest_;

		InputData inputData_;
	};	// class Application

}	// namespace vsl


//	EOF
