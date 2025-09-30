#pragma once

#include <string>
#ifndef _WIN32
#include <cstdint>
#endif


// https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
// Only use undefined keys from the above list for JSM custom commands
constexpr uint16_t V_WHEEL_UP = 0x03;     // Here I intentionally overwride VK_CANCEL because breaking a process with a keybind is not something we want to do
constexpr uint16_t V_WHEEL_DOWN = 0x07;   // I want all mouse bindings to be contiguous IDs for ease to distinguish
constexpr uint16_t NO_HOLD_MAPPED = 0x0A; // Empty mapping, which is different form no mapping for combo presses
constexpr uint16_t CALIBRATE = 0x0B;
constexpr uint16_t GYRO_INV_X = 0x88;
constexpr uint16_t GYRO_INV_Y = 0x89;
constexpr uint16_t GYRO_INVERT = 0x8A;
constexpr uint16_t GYRO_OFF_BIND = 0x8B; // Not to be confused with settings GYRO_ON and GYRO_OFF
constexpr uint16_t GYRO_ON_BIND = 0x8C;  // Those here are bindings
constexpr uint16_t GYRO_TRACK_X = 0x8D;
constexpr uint16_t GYRO_TRACK_Y = 0x8E;
constexpr uint16_t GYRO_TRACKBALL = 0x8F;
constexpr uint16_t COMMAND_ACTION = 0x97; // Run command
constexpr uint16_t RUMBLE = 0xE6;

constexpr const char *SMALL_RUMBLE = "R0080";
constexpr const char *BIG_RUMBLE = "RFF00";

// XInput buttons
constexpr uint16_t X_UP = 0xE8;
constexpr uint16_t X_DOWN = 0xE9;
constexpr uint16_t X_LEFT = 0xEA;
constexpr uint16_t X_RIGHT = 0xEB;
constexpr uint16_t X_LB = 0xEC;
constexpr uint16_t X_RB = 0xED;
constexpr uint16_t X_X = 0xEE;
constexpr uint16_t X_A = 0xEF;
constexpr uint16_t X_Y = 0xF0;
constexpr uint16_t X_B = 0xF1;
constexpr uint16_t X_LS = 0xF2;
constexpr uint16_t X_RS = 0xF3;
constexpr uint16_t X_BACK = 0xF4;
constexpr uint16_t X_START = 0xF5;
constexpr uint16_t X_GUIDE = 0xB8;
constexpr uint16_t X_LT = 0xD8;
constexpr uint16_t X_RT = 0xD9;

// DS4 buttons
constexpr uint16_t PS_UP = 0xE8;
constexpr uint16_t PS_DOWN = 0xE9;
constexpr uint16_t PS_LEFT = 0xEA;
constexpr uint16_t PS_RIGHT = 0xEB;
constexpr uint16_t PS_L1 = 0xEC;
constexpr uint16_t PS_R1 = 0xED;
constexpr uint16_t PS_SQUARE = 0xEE;
constexpr uint16_t PS_CROSS = 0xEF;
constexpr uint16_t PS_TRIANGLE = 0xF0;
constexpr uint16_t PS_CIRCLE = 0xF1;
constexpr uint16_t PS_L3 = 0xF2;
constexpr uint16_t PS_R3 = 0xF3;
constexpr uint16_t PS_SHARE = 0xF4;
constexpr uint16_t PS_OPTIONS = 0xF5;
constexpr uint16_t PS_HOME = 0xB8;
constexpr uint16_t PS_PAD_CLICK = 0xB9;
constexpr uint16_t PS_L2 = 0xD8;
constexpr uint16_t PS_R2 = 0xD9;

constexpr bool isControllerKey(uint16_t code)
{
	return (code >= X_UP && code <= X_START) || code == PS_HOME || code == PS_PAD_CLICK || code == X_LT || code == X_RT;
}


// Needs to be accessed publicly
uint16_t nameToKey(std::string_view name);

struct KeyCode
{
	uint16_t code = NO_HOLD_MAPPED;
	std::string name = "None";

	KeyCode() = default;

	KeyCode(std::string_view keyName)
	  : code(nameToKey(keyName))
	  , name()
	{
		if (code == COMMAND_ACTION)
			name = keyName.substr(1, keyName.size() - 2); // Remove opening and closing quotation marks
		else if (keyName.compare("SMALL_RUMBLE") == 0)
		{
			name = SMALL_RUMBLE;
			code = RUMBLE;
		}
		else if (keyName.compare("BIG_RUMBLE") == 0)
		{
			name = BIG_RUMBLE;
			code = RUMBLE;
		}
		else if (code != 0)
			name = keyName;
	}

	inline bool isValid() const
	{
		return code != 0;
	}

	inline bool operator==(const KeyCode &rhs) const
	{
		return code == rhs.code && name == rhs.name;
	}

	inline bool operator!=(const KeyCode &rhs) const
	{
		return !operator==(rhs);
	}
};


// The following operators enable reading and writing JSM's custom
// types to and from string, or handles exceptions
std::ostream &operator<<(std::ostream &out, const KeyCode &code);
// operator >>() is nameToKey()?!?


#ifdef _WIN32

#include <Windows.h>
#include <iostream>
#include <sstream>
#include <mutex>

static std::mutex print_mutex;

#define U(string) L##string

using TrayIconData = HINSTANCE;
using UnicodeString = std::wstring;

// Current Working Directory can now be changed: these need to be dynamic
extern const char *AUTOLOAD_FOLDER();
extern const char *GYRO_CONFIGS_FOLDER();
extern const char *BASE_JSM_CONFIG_FOLDER();
extern std::string NONAME;

#elif defined(__linux__)

#include <cassert>
#include <sstream>

#define WINAPI
#define VK_OEM_PLUS 0xBB
#define VK_OEM_MINUS 0xBD
#define VK_OEM_COMMA 0xBC
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_1 0xBA
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_BACK 0x08
#define VK_LEFT 0x25
#define VK_RIGHT 0x27
#define VK_UP 0x26
#define VK_DOWN 0x28
#define VK_SPACE 0x20
#define VK_CONTROL 0x11
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_SHIFT 0x10
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_MENU 0x12
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_HOME 0x24
#define VK_END 0x23
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_LWIN 0x5B // Left Windows Key
#define VK_RWIN 0x5C // Right Windows Key
#define VK_APPS 0x5D // Context Key
#define VK_SNAPSHOT 0x2C // Printscreen Key
#define VK_NONAME 0xFC

#define U(string) string
#define _ASSERT_EXPR(condition, message) assert(condition)

using BOOL = bool;
using WORD = unsigned short;
using DWORD = unsigned long;
using HANDLE = unsigned long;
using LPVOID = void *;
using TrayIconData = void *;
using UnicodeString = std::string;

// Current Working Directory can now be changed: these need to be dynamic
extern const char *AUTOLOAD_FOLDER();
extern const char *GYRO_CONFIGS_FOLDER();
extern const char *BASE_JSM_CONFIG_FOLDER();

extern unsigned long GetCurrentProcessId();

#else
#error "Unknown platform"
#endif
