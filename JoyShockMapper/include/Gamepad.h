#pragma once

#include "JoyShockMapper.h"

// Forward Declare and typedefs
typedef struct _VIGEM_TARGET_T *PVIGEM_TARGET;
typedef struct _VIGEM_CLIENT_T *PVIGEM_CLIENT;
typedef struct _XUSB_REPORT XUSB_REPORT;
typedef struct _DS4_REPORT DS4_REPORT;
typedef enum _VIGEM_ERRORS VIGEM_ERRORS;

#if defined(WIN32)
#define VIGEM_CALLBACK _stdcall
#else
#define VIGEM_CALLBACK
#endif

union Indicator
{
	uint8_t led;
	uint8_t rgb[3];
	uint32_t colorCode;
};

class Gamepad
{
public:
	typedef function<void(uint8_t largeMotor, uint8_t smallMotor, Indicator indicator)> Callback;
	Gamepad(ControllerScheme scheme);
	Gamepad(ControllerScheme scheme, Callback notification);
	virtual ~Gamepad();

	bool isInitialized(std::string *errorMsg = nullptr);
	inline string getError() const
	{
		return _errorMsg;
	}

	void setButton(KeyCode btn, bool pressed);
	void setLeftStick(float x, float y);
	void setRightStick(float x, float y);
	void setLeftTrigger(float);
	void setRightTrigger(float);
	void update();

	ControllerScheme getType() const;

private:
	static void VIGEM_CALLBACK x360Notification(
	  PVIGEM_CLIENT client,
	  PVIGEM_TARGET target,
	  uint8_t largeMotor,
	  uint8_t smallMotor,
	  uint8_t ledNumber,
	  void *userData);

	static void VIGEM_CALLBACK ds4Notification(
	  PVIGEM_CLIENT client,
	  PVIGEM_TARGET target,
	  uint8_t largeMotor,
	  uint8_t smallMotor,
	  Indicator lightbarColor,
	  void *userData);

	void init_x360();
	void init_ds4();

	void setButtonX360(KeyCode btn, bool pressed);
	void setButtonDS4(KeyCode btn, bool pressed);

	Callback _notification = nullptr;
	std::string _errorMsg;
	PVIGEM_TARGET _gamepad = nullptr;
	unique_ptr<XUSB_REPORT> _stateX360;
	unique_ptr<DS4_REPORT> _stateDS4;
};
