#pragma once

#include "JoyShockMapper.h"
#include "PlatformDefinitions.h"

#include <functional>
#include <string>
#include <cmath>
#include <utility>
#include <vector>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

enum class CommandSource { CONSOLE, FIFO, INTERNAL };

struct Command {
    std::string text;
    CommandSource source;
};

// Setup the input pipe for console input 
#ifndef _WIN32
extern int input_pipe_fd[2];

extern std::queue<Command> commandQueue;
extern std::mutex commandQueueMutex;
extern std::condition_variable commandQueueCV;

#endif


// get the user's mouse sensitivity multiplier from the user. In Windows it's an int, but who cares? it's well within range for float to represent it exactly
// also, if this is ported to other platforms, we might want non-integer sensitivities
float getMouseSpeed();

// send mouse button
int pressMouse(KeyCode vkKey, bool isPressed);

// send key press
int pressKey(KeyCode vkKey, bool pressed);

void moveMouse(float x, float y);

void setMouseNorm(float x, float y);

// delta time will apply to shaped movement, but the extra (velocity parameters after deltaTime) is
// applied as given
inline void shapedSensitivityMoveMouse(float x, float y, float deltaTime, float extraVelocityX, float extraVelocityY)
{
	// apply all values
	moveMouse(x * deltaTime + extraVelocityX, y * deltaTime + extraVelocityY);
}

BOOL WriteToConsole(string_view command);

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType);

// just setting up the console with standard stuff
void initConsole();
void initConsole(std::function<void()>);

void initFifoCommandListener();

tuple<string, string> GetActiveWindowName();

vector<string> ListDirectory(string directory);

string GetCWD();

bool SetCWD(string_view newCWD);

class PollingThread
{
public:
	PollingThread(const char *label, function<bool(void *)> loopContent,
	  void *funcParam,
	  DWORD pollPeriodMs,
	  bool startNow)
	  : _label(label)
	  , _thread()
	  , _loopContent(loopContent)
	  , _sleepTimeMs(pollPeriodMs)
	  , _funcParam(funcParam)
	  , _continue(false)
	{
		if (startNow)
			Start();
	}

	virtual ~PollingThread()
	{
		if (_continue)
		{
			Stop();
		}
		if (_thread)
		{
			_thread->join();
			_thread.reset();
		}
		// Let poll function cleanup
	}

	inline operator bool()
	{
		return _thread != nullptr;
	}

	bool Start()
	{
		if (_thread && !_continue) // thread is running but hasn't stopped yet
		{
			_thread->join();
			_thread.reset();
		}
		if (!_thread) // thread is clear
		{
			_continue = true;
			_thread.reset(new thread(&PollingThread::pollFunction, this));
		}
		return isRunning();
	}

	inline bool Stop()
	{
		_continue = false;
		return true;
	}

	inline bool isRunning()
	{
		return _thread && _continue;
	}

	const char *_label;

private:
	static DWORD WINAPI pollFunction(LPVOID param)
	{
		auto workerThread = static_cast<PollingThread *>(param);
		if (workerThread)
		{
			while (workerThread->_continue && workerThread->_loopContent(workerThread->_funcParam))
			{
				this_thread::sleep_for(
				  chrono::milliseconds{ workerThread->_sleepTimeMs });
			}
		}

		return 0;
	}

private:
	unique_ptr<thread> _thread;
	function<bool(void *)> _loopContent;
	uint64_t _sleepTimeMs;
	void *_funcParam;
	atomic_bool _continue;
};

DWORD ShowOnlineHelp();

void HideConsole();
void UnhideConsole();

void ShowConsole();

void ReleaseConsole();

bool IsVisible();

bool isConsoleMinimized();

bool ClearConsole();
