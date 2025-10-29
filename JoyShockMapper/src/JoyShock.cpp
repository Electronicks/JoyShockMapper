#include "JoyShock.h"
#include "InputHelpers.h"
#include <algorithm>
#define _USE_MATH_DEFINES
#include <math.h> // M_PI

extern shared_ptr<JslWrapper> jsl;
extern vector<JSMButton> mappings;
extern vector<JSMButton> grid_mappings;
extern float os_mouse_speed;
extern float last_flick_and_rotation;

float radial(float vX, float vY, float X, float Y)
{
	if (X != 0.0f && Y != 0.0f)
		return (vX * X + vY * Y) / sqrt(X * X + Y * Y);
	else
		return 0.0f;
}

float angleBasedDeadzone(float theta, float returnDeadzone, float returnDeadzoneCutoff)
{
	if (theta <= returnDeadzoneCutoff)
	{
		return (theta - returnDeadzone) / returnDeadzoneCutoff;
	}
	return 0.f;
}

AdaptiveTriggerSetting JoyShock::_unusedEffect;

JoyShock::JoyShock(int uniqueHandle, int controllerSplitType, shared_ptr<DigitalButton::Context> sharedButtonCommon)
  : _handle(uniqueHandle)
  , _splitType(controllerSplitType)
  , _controllerType(jsl->GetControllerType(uniqueHandle))
  , _triggerState(NUM_ANALOG_TRIGGERS, DstState::NoPress)
  , _prevTriggerPosition(NUM_ANALOG_TRIGGERS, deque<float>(MAGIC_TRIGGER_SMOOTHING, 0.f))
  , _light_bar(SettingsManager::get<Color>(SettingID::LIGHT_BAR)->value())
  , _context(sharedButtonCommon)
  , _motion(MotionIf::getNew())
  , _leftStick(SettingID::LEFT_STICK_DEADZONE_INNER, SettingID::LEFT_STICK_DEADZONE_OUTER, SettingID::LEFT_RING_MODE,
      SettingID::LEFT_STICK_MODE, ButtonID::LRING, ButtonID::LLEFT, ButtonID::LRIGHT, ButtonID::LUP, ButtonID::LDOWN)
  , _rightStick(SettingID::RIGHT_STICK_DEADZONE_INNER, SettingID::RIGHT_STICK_DEADZONE_OUTER, SettingID::RIGHT_RING_MODE,
      SettingID::RIGHT_STICK_MODE, ButtonID::RRING, ButtonID::RLEFT, ButtonID::RRIGHT, ButtonID::RUP, ButtonID::RDOWN)
  , _motionStick(SettingID::MOTION_DEADZONE_INNER, SettingID::MOTION_DEADZONE_OUTER, SettingID::MOTION_RING_MODE,
      SettingID::MOTION_STICK_MODE, ButtonID::MRING, ButtonID::MLEFT, ButtonID::MRIGHT, ButtonID::MUP, ButtonID::MDOWN)
{
	if (!sharedButtonCommon)
	{
		_context = make_shared<DigitalButton::Context>(bind(&JoyShock::onVirtualControllerNotification, this, placeholders::_1, placeholders::_2, placeholders::_3), _motion);
	}
	_light_bar = getSetting<Color>(SettingID::LIGHT_BAR);

	_context->_getMatchingSimBtn = bind(&JoyShock::getMatchingSimBtn, this, placeholders::_1);
	_context->_getMatchingDiagBtn = bind(&JoyShock::getMatchingDiagBtn, this, placeholders::_1, placeholders::_2);
	_context->_rumble = bind(&JoyShock::sendRumble, this, placeholders::_1, placeholders::_2);

	_buttons.reserve(LAST_ANALOG_TRIGGER); // Don't include touch stick _buttons
	for (int i = 0; i <= LAST_ANALOG_TRIGGER; ++i)
	{
		_buttons.push_back(DigitalButton(_context, mappings[i]));
	}
	resetSmoothSample();
	if (!hasVirtualController())
	{
		SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER)->set(ControllerScheme::NONE);
	}
	jsl->SetLightColour(_handle, getSetting<Color>(SettingID::LIGHT_BAR).raw);
	for (int i = 0; i < MAX_NO_OF_TOUCH; ++i)
	{
		_touchpads.push_back(TouchStick(i, _context, _handle));
	}
	_leftStick.scroll.init(_buttons[int(ButtonID::LLEFT)], _buttons[int(ButtonID::LRIGHT)]);
	_rightStick.scroll.init(_buttons[int(ButtonID::RLEFT)], _buttons[int(ButtonID::RRIGHT)]);
	_motionStick.scroll.init(_buttons[int(ButtonID::MLEFT)], _buttons[int(ButtonID::MRIGHT)]);
	_touchScrollX.init(_touchpads[0].buttons.find(ButtonID::TLEFT)->second, _touchpads[0].buttons.find(ButtonID::TRIGHT)->second);
	_touchScrollY.init(_touchpads[0].buttons.find(ButtonID::TUP)->second, _touchpads[0].buttons.find(ButtonID::TDOWN)->second);
	updateGridSize();
	_touchpads[0].scroll.init(_touchpads[0].buttons.find(ButtonID::TLEFT)->second, _touchpads[0].buttons.find(ButtonID::TRIGHT)->second);
	_touchpads[0].verticalScroll.init(_touchpads[0].buttons.find(ButtonID::TUP)->second, _touchpads[0].buttons.find(ButtonID::TDOWN)->second);
}

JoyShock ::~JoyShock()
{
	if (_splitType == JS_SPLIT_TYPE_LEFT)
	{
		_context->leftMotion = nullptr;
	}
	else
	{
		_context->rightMainMotion = nullptr;
	}
}

void JoyShock::sendRumble(int smallRumble, int bigRumble)
{
	if (SettingsManager::getV<Switch>(SettingID::RUMBLE)->value() == Switch::ON)
	{
		// DEBUG_LOG << "Rumbling at " << smallRumble << " and " << bigRumble << '\n';
		jsl->SetRumble(_handle, smallRumble, bigRumble);
	}
}

bool JoyShock::hasVirtualController()
{
	auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER);
	if (virtual_controller && virtual_controller->value() != ControllerScheme::NONE)
	{
		string error = "There is no controller object";
		if (!_context->_vigemController || _context->_vigemController->isInitialized(&error) == false)
		{
			CERR << "[ViGEm Client] " << error << '\n';
			return false;
		}
		else if (_context->_vigemController->getType() != virtual_controller->value())
		{
			CERR << "[ViGEm Client] The controller is of the wrong type!\n";
			return false;
		}
	}
	return true;
}

void JoyShock::onVirtualControllerNotification(uint8_t largeMotor, uint8_t smallMotor, Indicator indicator)
{
	// static chrono::steady_clock::time_point last_call;
	// auto now = chrono::steady_clock::now();
	// auto diff = ((float)chrono::duration_cast<chrono::microseconds>(now - last_call).count()) / 1000000.0f;
	// last_call = now;
	// COUT_INFO << "Time since last vigem rumble is " << diff << " us\n";
	lock_guard guard(_context->callback_lock);
	switch (_controllerType)
	{
	case JS_TYPE_DS4:
	case JS_TYPE_DS:
		jsl->SetLightColour(_handle, _light_bar.raw);
		break;
	default:
		jsl->SetPlayerNumber(_handle, indicator.led);
		break;
	}
	sendRumble(smallMotor << 8, largeMotor << 8);
}

float JoyShock::getSetting(SettingID index)
{
	// Look at active chord mappings starting with the latest activates chord
	for (auto activeChord = _context->chordStack.begin(); activeChord != _context->chordStack.end(); activeChord++)
	{
		auto opt = getSettingAtChord<float>(index, *activeChord);
		switch (index)
		{
		case SettingID::TRIGGER_THRESHOLD:
			if (opt && _controllerType == JS_TYPE_DS && getSetting<Switch>(SettingID::ADAPTIVE_TRIGGER) == Switch::ON)
				opt = optional(max(0.f, *opt)); // hair trigger disabled on dual sense when adaptive triggers are active
			break;
		case SettingID::MOTION_DEADZONE_INNER:
		case SettingID::MOTION_DEADZONE_OUTER:
			if (opt)
				opt = *opt / 180.f;
			break;
		case SettingID::ZERO:
			opt = 0.f;
			break;
		case SettingID::GYRO_AXIS_X:
		case SettingID::GYRO_AXIS_Y:
			if (auto axisSign = getSettingAtChord<AxisMode>(index, *activeChord))
				opt = float(*axisSign);
		}
		if (opt)
			return *opt;
	}

	stringstream message;
	message << "Index " << index << " is not a valid float setting";
	throw out_of_range(message.str().c_str());
}

template<>
FloatXY JoyShock::getSetting<FloatXY>(SettingID index)
{
	// Look at active chord mappings starting with the latest activates chord
	for (auto activeChord = _context->chordStack.begin(); activeChord != _context->chordStack.end(); activeChord++)
	{
		optional<FloatXY> opt = getSettingAtChord<FloatXY>(index, *activeChord);
		if (opt)
			return *opt;
	} // Check next Chord

	stringstream ss;
	ss << "Index " << index << " is not a valid FloatXY setting";
	throw invalid_argument(ss.str().c_str());
}

template<>
GyroSettings JoyShock::getSetting<GyroSettings>(SettingID index)
{
	if (index == SettingID::GYRO_ON || index == SettingID::GYRO_OFF)
	{
		// Look at active chord mappings starting with the latest activates chord
		for (auto activeChord = _context->chordStack.begin(); activeChord != _context->chordStack.end(); activeChord++)
		{
			auto opt = getSettingAtChord<GyroSettings>(index, *activeChord);
			if (opt)
				return *opt;
		}
	}
	stringstream ss;
	ss << "Index " << index << " is not a valid GyroSetting";
	throw invalid_argument(ss.str().c_str());
}

template<>
Color JoyShock::getSetting<Color>(SettingID index)
{
	if (index == SettingID::LIGHT_BAR)
	{
		// Look at active chord mappings starting with the latest activates chord
		for (auto activeChord = _context->chordStack.begin(); activeChord != _context->chordStack.end(); activeChord++)
		{
			auto opt = getSettingAtChord<Color>(index, *activeChord);
			if (opt)
				return *opt;
		}
	}
	stringstream ss;
	ss << "Index " << index << " is not a valid Color";
	throw invalid_argument(ss.str().c_str());
}

template<>
AdaptiveTriggerSetting JoyShock::getSetting<AdaptiveTriggerSetting>(SettingID index)
{
	// Look at active chord mappings starting with the latest activates chord
	for (auto activeChord = _context->chordStack.begin(); activeChord != _context->chordStack.end(); activeChord++)
	{
		optional<AdaptiveTriggerSetting> opt = getSettingAtChord<AdaptiveTriggerSetting>(index, *activeChord);
		if (opt)
			return *opt;
	}
	stringstream ss;
	ss << "Index " << index << " is not a valid AdaptiveTriggerSetting";
	throw invalid_argument(ss.str().c_str());
}

template<>
AxisSignPair JoyShock::getSetting<AxisSignPair>(SettingID index)
{
	// Look at active chord mappings starting with the latest activates chord
	for (auto activeChord = _context->chordStack.begin(); activeChord != _context->chordStack.end(); activeChord++)
	{
		optional<AxisSignPair> opt = getSettingAtChord<AxisSignPair>(index, *activeChord);
		if (opt)
			return *opt;
	} // Check next Chord

	stringstream ss;
	ss << "Index " << index << " is not a valid AxisSignPair setting";
	throw invalid_argument(ss.str().c_str());
}

DigitalButton *JoyShock::getMatchingSimBtn(ButtonID index)
{
	JSMButton *mapping = int(index) < mappings.size()        ? &mappings[int(index)] :
	  int(index) - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &grid_mappings[int(index) - FIRST_TOUCH_BUTTON] :
	                                                           nullptr;
	DigitalButton *button1 = int(index) < mappings.size()    ? &_buttons[int(index)] :
	  int(index) - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &_gridButtons[int(index) - FIRST_TOUCH_BUTTON] :
	                                                           nullptr;
	if (!mapping)
	{
		CERR << "Cannot find the button " << index << '\n';
	}
	else if (!button1)
	{
		CERR << "Cannot find the button " << button1 << '\n';
	}
	else
	{
		// Find the simMapping where the other btn is in the same state as this btn.
		// POTENTIAL FLAW: The mapping you find may not necessarily be the one that got you in a
		// Simultaneous state in the first place if there is a second SimPress going on where one
		// of the _buttons has a third SimMap with this one. I don't know if it's worth solving though...
		for (auto iter = mapping->getSimMapIter() ; iter ; ++iter)
		{
			DigitalButton *button2 = int(iter->first) < mappings.size()      ? &_buttons[int(iter->first)] :
			  int(iter->first) - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &_gridButtons[int(iter->first) - FIRST_TOUCH_BUTTON] :
																				nullptr;

			if (!button2)
			{
				CERR << "Cannot find the button " << button2 << '\n';
			}
			else if (index != iter->first && button1->getState() == button2->getState())
			{
				return button2;
			}
		}
	}
	return nullptr;
}

DigitalButton *JoyShock::getMatchingDiagBtn(ButtonID index, optional<MapIterator> &iter)
{
	JSMButton *mapping = int(index) < mappings.size()        ? &mappings[int(index)] :
	  int(index) - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &grid_mappings[int(index) - FIRST_TOUCH_BUTTON] :
	                                                           nullptr;
	DigitalButton *button1 = int(index) < mappings.size()    ? &_buttons[int(index)] :
	  int(index) - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &_gridButtons[int(index) - FIRST_TOUCH_BUTTON] :
	                                                           nullptr;
	if (!mapping)
	{
		CERR << "Cannot find the button " << index << '\n';
	}
	else if (!button1)
	{
		CERR << "Cannot find the button " << button1 << '\n';
	}
	else
	{
		// Find the diagMapping where the other btn is in the same state as this btn.
		if (!iter)
			iter = mapping->getDiagMapIter();
		for (; *iter; ++*iter)
		{
			int i = int((*iter)->first);
			DigitalButton *button2 = i < mappings.size()    ? &_buttons[i] :
			  i - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &_gridButtons[i - FIRST_TOUCH_BUTTON] :
			                                                                 nullptr;

			if (!button2)
			{
				CERR << "Cannot find the button " << button2 << '\n';
			}
			else if (index != (*iter)->first && button2->getState() != BtnState::NoPress)
			{
				return button2;
			}
		}
	}
	return nullptr;
}

void JoyShock::resetSmoothSample()
{
	_frontSample = 0;
	for (int i = 0; i < NUM_SAMPLES; i++)
	{
		_flickSamples[i] = 0.0;
	}
}

float JoyShock::getSmoothedStickRotation(float value, float bottomThreshold, float topThreshold, int maxSamples)
{
	// which sample in the circular smoothing buffer do we want to write over?
	_frontSample--;
	if (_frontSample < 0)
		_frontSample = NUM_SAMPLES - 1;
	// if this input is bigger than the top threshold, it'll all be consumed immediately; 0 gets put into the smoothing buffer. If it's below the bottomThreshold, it'll all be put in the smoothing buffer
	float length = abs(value);
	float immediateFactor;
	if (topThreshold <= bottomThreshold)
	{
		immediateFactor = 1.0f;
	}
	else
	{
		immediateFactor = (length - bottomThreshold) / (topThreshold - bottomThreshold);
	}
	// clamp to [0, 1] range
	if (immediateFactor < 0.0f)
	{
		immediateFactor = 0.0f;
	}
	else if (immediateFactor > 1.0f)
	{
		immediateFactor = 1.0f;
	}
	float smoothFactor = 1.0f - immediateFactor;
	// now we can push the smooth sample (or as much of it as we want smoothed)
	float frontSample = _flickSamples[_frontSample] = value * smoothFactor;
	// and now calculate smoothed result
	float result = frontSample / maxSamples;
	for (int i = 1; i < maxSamples; i++)
	{
		int rotatedIndex = (_frontSample + i) % NUM_SAMPLES;
		frontSample = _flickSamples[rotatedIndex];
		result += frontSample / maxSamples;
	}
	// finally, add immediate portion
	return result + value * immediateFactor;
}

void JoyShock::getSmoothedGyro(float x, float y, float length, float bottomThreshold, float topThreshold, int maxSamples, float &outX, float &outY)
{
	// this is basically the same as we use for smoothing flick-stick rotations, but because this deals in vectors, it's a slightly different function. Not worth abstracting until it'll be used in more ways
	// which item in the circular smoothing buffer will we write over?
	_frontGyroSample--;
	if (_frontGyroSample < 0)
		_frontGyroSample = MAX_GYRO_SAMPLES - 1;
	float immediateFactor;
	if (topThreshold <= bottomThreshold)
	{
		immediateFactor = length < bottomThreshold ? 0.0f : 1.0f;
	}
	else
	{
		immediateFactor = (length - bottomThreshold) / (topThreshold - bottomThreshold);
	}
	// clamp to [0, 1] range
	if (immediateFactor < 0.0f)
	{
		immediateFactor = 0.0f;
	}
	else if (immediateFactor > 1.0f)
	{
		immediateFactor = 1.0f;
	}
	float smoothFactor = 1.0f - immediateFactor;
	// now we can push the smooth sample (or as much of it as we want smoothed)
	FloatXY frontSample = _gyroSamples[_frontGyroSample] = { x * smoothFactor, y * smoothFactor };
	// and now calculate smoothed result
	float xResult = frontSample.x() / maxSamples;
	float yResult = frontSample.y() / maxSamples;
	for (int i = 1; i < maxSamples; i++)
	{
		int rotatedIndex = (_frontGyroSample + i) % MAX_GYRO_SAMPLES;
		frontSample = _gyroSamples[rotatedIndex];
		xResult += frontSample.x() / maxSamples;
		yResult += frontSample.y() / maxSamples;
	}
	// finally, add immediate portion
	outX = xResult + x * immediateFactor;
	outY = yResult + y * immediateFactor;
}

void JoyShock::handleButtonChange(ButtonID id, bool pressed, int touchpadID)
{
	DigitalButton *button = int(id) <= LAST_ANALOG_TRIGGER ? &_buttons[int(id)] :
	  touchpadID >= 0 && touchpadID < _touchpads.size()    ? &_touchpads[touchpadID].buttons.find(id)->second :
	  id >= ButtonID::T1                                   ? &_gridButtons[int(id) - int(ButtonID::T1)] :
	                                                         nullptr;

	if (!button)
	{
		CERR << "Button " << id << " with tocuchpadId " << touchpadID << " could not be found\n";
		return;
	}
	else if ((!_context->nn && pressed) || (_context->nn > 0 && (id >= ButtonID::UP || id <= ButtonID::DOWN || id == ButtonID::S || id == ButtonID::E) && nnm.find(_context->nn) != nnm.end() && nnm.find(_context->nn)->second == id))
	{
		Pressed evt;
		evt.time_now = _timeNow;
		evt.turboTime = getSetting(SettingID::TURBO_PERIOD);
		evt.holdTime = getSetting(SettingID::HOLD_PRESS_TIME);
		evt.dblPressWindow = getSetting(SettingID::DBL_PRESS_WINDOW);
		button->sendEvent(evt);
	}
	else
	{
		Released evt;
		evt.time_now = _timeNow;
		evt.turboTime = getSetting(SettingID::TURBO_PERIOD);
		evt.holdTime = getSetting(SettingID::HOLD_PRESS_TIME);
		evt.dblPressWindow = getSetting(SettingID::DBL_PRESS_WINDOW);
		button->sendEvent(evt);
	}
}

float JoyShock::getTriggerEffectStartPos()
{
	float threshold = getSetting(SettingID::TRIGGER_THRESHOLD);
	if (_controllerType == JS_TYPE_DS && getSetting<Switch>(SettingID::ADAPTIVE_TRIGGER) != Switch::OFF)
		threshold = max(0.f, threshold); // hair trigger disabled on dual sense when adaptive triggers are active
	return clamp(threshold + 0.05f, 0.0f, 1.0f);
}

void JoyShock::handleTriggerChange(ButtonID softIndex, ButtonID fullIndex, TriggerMode mode, float position, AdaptiveTriggerSetting &trigger_rumble)
{
	uint8_t offset = SettingsManager::getV<int>(softIndex == ButtonID::ZL ? SettingID::LEFT_TRIGGER_OFFSET : SettingID::RIGHT_TRIGGER_OFFSET)->value();
	uint8_t range = SettingsManager::getV<int>(softIndex == ButtonID::ZL ? SettingID::LEFT_TRIGGER_RANGE : SettingID::RIGHT_TRIGGER_RANGE)->value();
	auto idxState = int(fullIndex) - FIRST_ANALOG_TRIGGER; // Get analog trigger index
	if (idxState < 0 || idxState >= (int)_triggerState.size())
	{
		COUT << "Error: Trigger " << fullIndex << " does not exist in state map. Dual Stage Trigger not possible.\n";
		return;
	}

	if (mode != TriggerMode::X_LT && mode != TriggerMode::X_RT && (_controllerType == JS_TYPE_PRO_CONTROLLER || _controllerType == JS_TYPE_JOYCON_LEFT || _controllerType == JS_TYPE_JOYCON_RIGHT))
	{
		// Override local variable because the controller has digital triggers. Effectively ignore Full Pull binding.
		mode = TriggerMode::NO_FULL;
	}

	if (mode == TriggerMode::X_LT)
	{
		if (_context->_vigemController)
			_context->_vigemController->setLeftTrigger(position);
		trigger_rumble.mode = AdaptiveTriggerMode::RESISTANCE_RAW;
		trigger_rumble.force = 0;
		trigger_rumble.start = offset + 0.05 * range;
		_context->updateChordStack(position > 0, softIndex);
		_context->updateChordStack(position >= 1.0, fullIndex);
		return;
	}
	else if (mode == TriggerMode::X_RT)
	{
		if (_context->_vigemController)
			_context->_vigemController->setRightTrigger(position);
		trigger_rumble.mode = AdaptiveTriggerMode::RESISTANCE_RAW;
		trigger_rumble.force = 0;
		trigger_rumble.start = offset + 0.05 * range;
		_context->updateChordStack(position > 0, softIndex);
		_context->updateChordStack(position >= 1.0, fullIndex);
		return;
	}

	// if either trigger is waiting to be tap released, give it a go
	if (_buttons[int(softIndex)].getState() == BtnState::TapPress)
	{
		// keep triggering until the tap release is complete
		handleButtonChange(softIndex, false);
	}
	if (_buttons[int(fullIndex)].getState() == BtnState::TapPress)
	{
		// keep triggering until the tap release is complete
		handleButtonChange(fullIndex, false);
	}

	switch (_triggerState[idxState])
	{
	case DstState::NoPress:
		// It actually doesn't matter what the last Press is. Theoretically, we could have missed the edge.
		if (mode == TriggerMode::NO_FULL)
		{
			trigger_rumble.mode = AdaptiveTriggerMode::RESISTANCE_RAW;
			trigger_rumble.force = UINT16_MAX;
			trigger_rumble.start = offset + getTriggerEffectStartPos() * range;
		}
		else
		{
			trigger_rumble.mode = AdaptiveTriggerMode::SEGMENT;
			trigger_rumble.force = 0.1 * UINT16_MAX;
			trigger_rumble.start = offset + getTriggerEffectStartPos() * range;
			trigger_rumble.end = offset + min(1.f, getTriggerEffectStartPos() + 0.1f) * range;
		}
		if (isSoftPullPressed(idxState, position))
		{
			if (mode == TriggerMode::MAY_SKIP || mode == TriggerMode::MUST_SKIP)
			{
				// Start counting press time to see if soft binding should be skipped
				_triggerState[idxState] = DstState::PressStart;
				_buttons[int(softIndex)].sendEvent(_timeNow);
			}
			else if (mode == TriggerMode::MAY_SKIP_R || mode == TriggerMode::MUST_SKIP_R)
			{
				_triggerState[idxState] = DstState::PressStartResp;
				_buttons[int(softIndex)].sendEvent(_timeNow);
				handleButtonChange(softIndex, true);
			}
			else // mode == NO_FULL or NO_SKIP, NO_SKIP_EXCLUSIVE
			{
				_triggerState[idxState] = DstState::SoftPress;
				handleButtonChange(softIndex, true);
			}
		}
		else
		{
			handleButtonChange(softIndex, false);
		}
		break;
	case DstState::PressStart:
		// don't change trigger rumble : keep whatever was set at no press
		if (!isSoftPullPressed(idxState, position))
		{
			// Trigger has been quickly tapped on the soft press
			_triggerState[idxState] = DstState::QuickSoftTap;
			handleButtonChange(softIndex, true);
		}
		else if (position == 1.0)
		{
			// Trigger has been full pressed quickly
			_triggerState[idxState] = DstState::QuickFullPress;
			handleButtonChange(fullIndex, true);
		}
		else
		{
			GetDuration dur{ _timeNow };
			if (_buttons[int(softIndex)].sendEvent(dur).out_duration >= getSetting(SettingID::TRIGGER_SKIP_DELAY))
			{
				if (mode == TriggerMode::MUST_SKIP)
				{
					trigger_rumble.start = offset + (position + 0.05) * range;
				}
				_triggerState[idxState] = DstState::SoftPress;
				// reset the time for hold soft press purposes.
				_buttons[int(softIndex)].sendEvent(_timeNow);
				handleButtonChange(softIndex, true);
			}
		}
		// Else, time passes as soft press is being held, waiting to see if the soft binding should be skipped
		break;
	case DstState::PressStartResp:
		// don't change trigger rumble : keep whatever was set at no press
		if (!isSoftPullPressed(idxState, position))
		{
			// Soft press is being released
			_triggerState[idxState] = DstState::NoPress;
			handleButtonChange(softIndex, false);
		}
		else if (position == 1.0)
		{
			// Trigger has been full pressed quickly
			_triggerState[idxState] = DstState::QuickFullPress;
			handleButtonChange(softIndex, false); // Remove soft press
			handleButtonChange(fullIndex, true);
		}
		else
		{
			GetDuration dur{ _timeNow };
			if (_buttons[int(softIndex)].sendEvent(dur).out_duration >= getSetting(SettingID::TRIGGER_SKIP_DELAY))
			{
				if (mode == TriggerMode::MUST_SKIP_R)
				{
					trigger_rumble.start = offset + (position + 0.05) * range;
				}
				_triggerState[idxState] = DstState::SoftPress;
			}
			handleButtonChange(softIndex, true);
		}
		break;
	case DstState::QuickSoftTap:
		// Soft trigger is already released. Send release now!
		// don't change trigger rumble : keep whatever was set at no press
		_triggerState[idxState] = DstState::NoPress;
		handleButtonChange(softIndex, false);
		break;
	case DstState::QuickFullPress:
		trigger_rumble.mode = AdaptiveTriggerMode::SEGMENT;
		trigger_rumble.force = UINT16_MAX;
		trigger_rumble.start = offset + 0.89 * range;
		trigger_rumble.end = offset + 0.99 * range;
		if (position < 1.0f)
		{
			// Full press is being release
			_triggerState[idxState] = DstState::QuickFullRelease;
			handleButtonChange(fullIndex, false);
		}
		else
		{
			// Full press is being held
			handleButtonChange(fullIndex, true);
		}
		break;
	case DstState::QuickFullRelease:
		trigger_rumble.mode = AdaptiveTriggerMode::SEGMENT;
		trigger_rumble.force = UINT16_MAX;
		trigger_rumble.start = offset + 0.89 * range;
		trigger_rumble.end = offset + 0.99 * range;
		if (!isSoftPullPressed(idxState, position))
		{
			_triggerState[idxState] = DstState::NoPress;
		}
		else if (position == 1.0f)
		{
			// Trigger is being full pressed again
			_triggerState[idxState] = DstState::QuickFullPress;
			handleButtonChange(fullIndex, true);
		}
		// else wait for the the trigger to be fully released
		break;
	case DstState::SoftPress:
		if (!isSoftPullPressed(idxState, position))
		{
			// Soft press is being released
			handleButtonChange(softIndex, false);
			_triggerState[idxState] = DstState::NoPress;
		}
		else // Soft Press is being held
		{
			float tick_time = SettingsManager::get<float>(SettingID::TICK_TIME)->value();
			if (mode == TriggerMode::NO_SKIP || mode == TriggerMode::MAY_SKIP || mode == TriggerMode::MAY_SKIP_R)
			{
				trigger_rumble.force = min(int(UINT16_MAX), trigger_rumble.force + int(1 / 30.f * tick_time * UINT16_MAX));
				trigger_rumble.start = min(offset + 0.89 * range, trigger_rumble.start + 1 / 150. * tick_time * range);
				trigger_rumble.end = trigger_rumble.start + 0.1 * range;
				handleButtonChange(softIndex, true);
				if (position == 1.0)
				{
					// Full press is allowed in addition to soft press
					_triggerState[idxState] = DstState::DelayFullPress;
					handleButtonChange(fullIndex, true);
				}
			}
			else if (mode == TriggerMode::NO_SKIP_EXCLUSIVE)
			{
				trigger_rumble.force = min(int(UINT16_MAX), trigger_rumble.force + int(1 / 30.f * tick_time * UINT16_MAX));
				trigger_rumble.start = min(offset + 0.89 * range, trigger_rumble.start + 1 / 150. * tick_time * range);
				trigger_rumble.end = trigger_rumble.start + 0.1 * range;
				handleButtonChange(softIndex, false);
				if (position == 1.0)
				{
					_triggerState[idxState] = DstState::ExclFullPress;
					handleButtonChange(fullIndex, true);
				}
			}
			else // NO_FULL, MUST_SKIP and MUST_SKIP_R
			{
				trigger_rumble.mode = AdaptiveTriggerMode::RESISTANCE_RAW;
				trigger_rumble.force = min(int(UINT16_MAX), trigger_rumble.force + int(1 / 30.f * tick_time * UINT16_MAX));
				// keep old trigger_rumble.start
				handleButtonChange(softIndex, true);
			}
		}
		break;
	case DstState::DelayFullPress:
		trigger_rumble.mode = AdaptiveTriggerMode::SEGMENT;
		trigger_rumble.force = UINT16_MAX;
		trigger_rumble.start = offset + 0.8 * range;
		trigger_rumble.end = offset + 0.99 * range;
		if (position < 1.0)
		{
			// Full Press is being released
			_triggerState[idxState] = DstState::SoftPress;
			handleButtonChange(fullIndex, false);
		}
		else // Full press is being held
		{
			handleButtonChange(fullIndex, true);
		}
		// Soft press is always held regardless
		handleButtonChange(softIndex, true);
		break;
	case DstState::ExclFullPress:
		trigger_rumble.mode = AdaptiveTriggerMode::SEGMENT;
		trigger_rumble.force = UINT16_MAX;
		trigger_rumble.start = offset + 0.89 * range;
		trigger_rumble.end = offset + 0.99 * range;
		if (position < 1.0f)
		{
			// Full press is being release
			_triggerState[idxState] = DstState::SoftPress;
			handleButtonChange(fullIndex, false);
			handleButtonChange(softIndex, true);
		}
		else
		{
			// Full press is being held
			handleButtonChange(fullIndex, true);
		}
		break;
	default:
		CERR << "Trigger " << softIndex << " has invalid state " << _triggerState[idxState] << ". Reset to NoPress.\n";
		_triggerState[idxState] = DstState::NoPress;
		break;
	}

	return;
}

bool JoyShock::isPressed(ButtonID btn)
{
	// Use chord stack to know if a mapping is pressed, because the state from the callback
	// only holds half the information when it comes to a joycon pair.
	// Also, NONE is always part of the stack (for chord handling) but NONE is never pressed.
	return btn != ButtonID::NONE && find(_context->chordStack.begin(), _context->chordStack.end(), btn) != _context->chordStack.end();
}

// return true if it hits the outer deadzone
bool JoyShock::processDeadZones(float &x, float &y, float innerDeadzone, float outerDeadzone)
{
	float length = sqrtf(x * x + y * y);
	if (length <= innerDeadzone)
	{
		x = 0.0f;
		y = 0.0f;
		return false;
	}
	if (length >= outerDeadzone)
	{
		// normalize
		x /= length;
		y /= length;
		return true;
	}
	if (length > innerDeadzone)
	{
		float scaledLength = (length - innerDeadzone) / (outerDeadzone - innerDeadzone);
		float rescale = scaledLength / length;
		x *= rescale;
		y *= rescale;
	}
	return false;
}

void JoyShock::updateGridSize()
{
	while (_gridButtons.size() > grid_mappings.size())
		_gridButtons.pop_back();

	for (size_t i = _gridButtons.size(); i < grid_mappings.size(); ++i)
	{
		JSMButton &map(grid_mappings[i]);
		_gridButtons.push_back(DigitalButton(_context, map));
	}
}

bool JoyShock::isSoftPullPressed(int triggerIndex, float triggerPosition)
{
	float threshold = getSetting(SettingID::TRIGGER_THRESHOLD);
	if (_controllerType == JS_TYPE_DS && getSetting<Switch>(SettingID::ADAPTIVE_TRIGGER) != Switch::OFF)
		threshold = max(0.f, threshold); // hair trigger disabled on dual sense when adaptive triggers are active
	if (threshold >= 0)
	{
		return triggerPosition > threshold;
	}
	// else HAIR TRIGGER

	// Calculate 3 sample averages with the last MAGIC_TRIGGER_SMOOTHING samples + new sample
	float sum = 0.f;
	for_each(_prevTriggerPosition[triggerIndex].begin(), _prevTriggerPosition[triggerIndex].begin() + 3, [&sum](auto data)
	  { sum += data; });
	float avg_tm3 = sum / 3.0f;
	sum = sum - *(_prevTriggerPosition[triggerIndex].begin()) + *(_prevTriggerPosition[triggerIndex].end() - 2);
	float avg_tm2 = sum / 3.0f;
	sum = sum - *(_prevTriggerPosition[triggerIndex].begin() + 1) + *(_prevTriggerPosition[triggerIndex].end() - 1);
	float avg_tm1 = sum / 3.0f;
	sum = sum - *(_prevTriggerPosition[triggerIndex].begin() + 2) + triggerPosition;
	float avg_t0 = sum / 3.0f;
	// if (avg_t0 > 0) COUT << "Trigger: " << avg_t0 << '\n';

	// Soft press is pressed if we got three averaged samples in a row that are pressed
	bool isPressed;
	if (avg_t0 > avg_tm1 && avg_tm1 > avg_tm2 && avg_tm2 > avg_tm3)
	{
		// DEBUG_LOG << "Hair Trigger pressed: " << avg_t0 << " > " << avg_tm1 << " > " << avg_tm2 << " > " << avg_tm3 << '\n';
		isPressed = true;
	}
	else if (avg_t0 < avg_tm1 && avg_tm1 < avg_tm2 && avg_tm2 < avg_tm3)
	{
		// DEBUG_LOG << "Hair Trigger released: " << avg_t0 << " < " << avg_tm1 << " < " << avg_tm2 << " < " << avg_tm3 << '\n';
		isPressed = false;
	}
	else
	{
		isPressed = _triggerState[triggerIndex] != DstState::NoPress && _triggerState[triggerIndex] != DstState::QuickSoftTap;
	}
	_prevTriggerPosition[triggerIndex].pop_front();
	_prevTriggerPosition[triggerIndex].push_back(triggerPosition);
	return isPressed;
}

float JoyShock::handleFlickStick(float stickX, float stickY, Stick &stick, float stickLength, StickMode mode)
{
	GyroOutput flickStickOutput = getSetting<GyroOutput>(SettingID::FLICK_STICK_OUTPUT);
	bool isMouse = flickStickOutput == GyroOutput::MOUSE;
	float mouseCalibrationFactor = 180.0f / M_PI / os_mouse_speed;

	float camSpeedX = 0.0f;
	// let's centre this
	float offsetX = stickX;
	float offsetY = stickY;
	float lastOffsetX = stick.lastX;
	float lastOffsetY = stick.lastY;
	float flickStickThreshold = 1.0f;
	if (stick.is_flicking)
	{
		flickStickThreshold *= 0.9f;
	}
	if (stickLength >= flickStickThreshold)
	{
		float stickAngle = atan2f(-offsetX, offsetY);
		// COUT << ", %.4f\n", lastOffsetLength);
		if (!stick.is_flicking)
		{
			// bam! new flick!
			stick.is_flicking = true;
			if (mode != StickMode::ROTATE_ONLY)
			{
				auto flick_snap_mode = getSetting<FlickSnapMode>(SettingID::FLICK_SNAP_MODE);
				if (flick_snap_mode != FlickSnapMode::NONE)
				{
					// _handle snapping
					float snapInterval = M_PI;
					if (flick_snap_mode == FlickSnapMode::FOUR)
					{
						snapInterval = M_PI / 2.0f; // 90 degrees
					}
					else if (flick_snap_mode == FlickSnapMode::EIGHT)
					{
						snapInterval = M_PI / 4.0f; // 45 degrees
					}
					float snappedAngle = round(stickAngle / snapInterval) * snapInterval;
					// lerp by snap strength
					auto flick_snap_strength = getSetting(SettingID::FLICK_SNAP_STRENGTH);
					stickAngle = stickAngle * (1.0f - flick_snap_strength) + snappedAngle * flick_snap_strength;
				}
				if (abs(stickAngle) * (180.0f / M_PI) < getSetting(SettingID::FLICK_DEADZONE_ANGLE))
				{
					stickAngle = 0.0f;
				}

				stick.started_flick = chrono::steady_clock::now();
				stick.delta_flick = stickAngle;
				stick.flick_percent_done = 0.0f;
				resetSmoothSample();
				stick.flick_rotation_counter = stickAngle; // track all rotation for this flick
				COUT << "Flick: " << setprecision(3) << stickAngle * (180.0f / (float)M_PI) << " degrees\n";
			}
		}
		else // I am flicking!
		{
			if (mode != StickMode::FLICK_ONLY)
			{
				// not new? turn camera?
				float lastStickAngle = atan2f(-lastOffsetX, lastOffsetY);
				float angleChange = stickAngle - lastStickAngle;
				// https://stackoverflow.com/a/11498248/1130520
				angleChange = fmod(angleChange + M_PI, 2.0f * M_PI);
				if (angleChange < 0)
					angleChange += 2.0f * M_PI;
				angleChange -= M_PI;
				stick.flick_rotation_counter += angleChange; // track all rotation for this flick
				float flickSpeedConstant = isMouse ? getSetting(SettingID::REAL_WORLD_CALIBRATION) * mouseCalibrationFactor / getSetting(SettingID::IN_GAME_SENS) : 1.f;
				float flickSpeed = -(angleChange * flickSpeedConstant);
				float tick_time = SettingsManager::get<float>(SettingID::TICK_TIME)->value();
				int maxSmoothingSamples = min(NUM_SAMPLES, (int)ceil(64.0f / tick_time)); // target a max smoothing window size of 64ms
				float stepSize = 0.01f;                                                   // and we only want full on smoothing when the stick change each time we poll it is approximately the minimum stick resolution
				                                                                          // the fact that we're using radians makes this really easy
				auto rotate_smooth_override = getSetting(SettingID::ROTATE_SMOOTH_OVERRIDE);
				if (rotate_smooth_override < 0.0f)
				{
					camSpeedX = getSmoothedStickRotation(flickSpeed, flickSpeedConstant * stepSize * 2.0f, flickSpeedConstant * stepSize * 4.0f, maxSmoothingSamples);
				}
				else
				{
					camSpeedX = getSmoothedStickRotation(flickSpeed, flickSpeedConstant * rotate_smooth_override, flickSpeedConstant * rotate_smooth_override * 2.0f, maxSmoothingSamples);
				}

				if (!isMouse)
				{
					// convert to a velocity
					camSpeedX *= 180.0f / (M_PI * 0.001f * SettingsManager::get<float>(SettingID::TICK_TIME)->value());
				}
			}
		}
	}
	else if (stick.is_flicking)
	{
		// was a flick! how much was the flick and rotation?
		if (mode == StickMode::FLICK) // not only flick or only rotate
		{
			last_flick_and_rotation = abs(stick.flick_rotation_counter) / (2.0f * M_PI);
		}
		stick.is_flicking = false;
	}
	// do the flicking. this works very differently if it's mouse vs stick
	if (isMouse)
	{
		float secondsSinceFlick = ((float)chrono::duration_cast<chrono::microseconds>(_timeNow - stick.started_flick).count()) / 1000000.0f;
		float newPercent = secondsSinceFlick / getSetting(SettingID::FLICK_TIME);

		// don't divide by zero
		if (abs(stick.delta_flick) > 0.0f)
		{
			newPercent = newPercent / pow(abs(stick.delta_flick) / M_PI, getSetting(SettingID::FLICK_TIME_EXPONENT));
		}

		if (newPercent > 1.0f)
			newPercent = 1.0f;
		// warping towards 1.0
		float oldShapedPercent = 1.0f - stick.flick_percent_done;
		oldShapedPercent *= oldShapedPercent;
		oldShapedPercent = 1.0f - oldShapedPercent;
		// float oldShapedPercent = flick_percent_done;
		stick.flick_percent_done = newPercent;
		newPercent = 1.0f - newPercent;
		newPercent *= newPercent;
		newPercent = 1.0f - newPercent;
		float camSpeedChange = (newPercent - oldShapedPercent) * stick.delta_flick * getSetting(SettingID::REAL_WORLD_CALIBRATION) * -mouseCalibrationFactor / getSetting(SettingID::IN_GAME_SENS);
		camSpeedX += camSpeedChange;

		return camSpeedX;
	}
	else
	{
		float secondsSinceFlick = ((float)chrono::duration_cast<chrono::microseconds>(_timeNow - stick.started_flick).count()) / 1000000.0f;
		float maxStickGameSpeed = getSetting(SettingID::VIRTUAL_STICK_CALIBRATION);
		float flickTime = abs(stick.delta_flick) / (maxStickGameSpeed * M_PI / 180.f);

		if (secondsSinceFlick <= flickTime)
		{
			camSpeedX -= stick.delta_flick >= 0 ? maxStickGameSpeed : -maxStickGameSpeed;
		}

		// alright, but what happens if we've set gyro to one stick and flick stick to another?
		// Nic: FS is mouse output and gyrostick is stick output. The game handles the merging (or not)
		// Depends on the game, some take simultaneous input better than others. Players are aware of that. -Nic
		GyroOutput gyroOutput = getSetting<GyroOutput>(SettingID::GYRO_OUTPUT);
		if (gyroOutput == flickStickOutput)
		{
			gyroXVelocity += camSpeedX;
			processGyroStick(0.f, 0.f, 0.f, flickStickOutput == GyroOutput::LEFT_STICK ? StickMode::LEFT_STICK : StickMode::RIGHT_STICK, false);
		}
		else
		{
			float tempGyroXVelocity = gyroXVelocity;
			float tempGyroYVelocity = gyroYVelocity;
			gyroXVelocity = camSpeedX;
			gyroYVelocity = 0.f;
			processGyroStick(0.f, 0.f, 0.f, flickStickOutput == GyroOutput::LEFT_STICK ? StickMode::LEFT_STICK : StickMode::RIGHT_STICK, true);
			gyroXVelocity = tempGyroXVelocity;
			gyroYVelocity = tempGyroYVelocity;
		}

		return 0.f;
	}
}

void JoyShock::processStick(float stickX, float stickY, Stick &stick, float mouseCalibrationFactor, float deltaTime, bool &anyStickInput, bool &lockMouse, float &camSpeedX, float &camSpeedY)
{
	float temp;
	auto controllerOrientation = getSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION);
	switch (controllerOrientation)
	{
	case ControllerOrientation::LEFT:
		temp = stickX;
		stickX = -stickY;
		stickY = temp;
		temp = stick.lastX;
		stick.lastX = -stick.lastY;
		stick.lastY = temp;
		break;
	case ControllerOrientation::RIGHT:
		temp = stickX;
		stickX = stickY;
		stickY = -temp;
		temp = stick.lastX;
		stick.lastX = stick.lastY;
		stick.lastY = -temp;
		break;
	case ControllerOrientation::BACKWARD:
		stickX = -stickX;
		stickY = -stickY;
		stick.lastX = -stick.lastX;
		stick.lastY = -stick.lastY;
		break;
	}
	auto outerDeadzone = getSetting(stick._outerDeadzone);
	auto innerDeadzone = getSetting(stick._innerDeadzone);
	outerDeadzone = 1.0f - outerDeadzone;
	float rawX = stickX;
	float rawY = stickY;
	float rawLength = sqrtf(rawX * rawX + rawY * rawY);
	float rawLastX = stick.lastX;
	float rawLastY = stick.lastY;
	processDeadZones(stick.lastX, stick.lastY, innerDeadzone, outerDeadzone);
	bool pegged = processDeadZones(stickX, stickY, innerDeadzone, outerDeadzone);
	float absX = abs(stickX);
	float absY = abs(stickY);
	bool left = stickX < -0.5f * absY;
	bool right = stickX > 0.5f * absY;
	bool down = stickY < -0.5f * absX;
	bool up = stickY > 0.5f * absX;
	float stickLength = sqrtf(stickX * stickX + stickY * stickY);
	auto ringMode = getSetting<RingMode>(stick._ringMode);
	auto stickMode = getSetting<StickMode>(stick._stickMode);

	bool ring = ringMode == RingMode::INNER && stickLength > 0.0f && stickLength < 1.0f ||
				ringMode == RingMode::OUTER && stickLength >=  1.0f;
	handleButtonChange(stick._ringId, ring, stick._touchpadIndex);

	if (stick.ignore_stick_mode && stickMode == StickMode::INVALID && stickX == 0 && stickY == 0)
	{
		// clear ignore flag when stick is back at neutral
		stick.ignore_stick_mode = false;
	}
	else if (stickMode == StickMode::FLICK || stickMode == StickMode::FLICK_ONLY || stickMode == StickMode::ROTATE_ONLY)
	{
		camSpeedX += handleFlickStick(stickX, stickY, stick, stickLength, stickMode);
		anyStickInput = pegged;
	}
	else if (stickMode == StickMode::AIM)
	{
		// camera movement
		if (!pegged)
		{
			stick.acceleration = 1.0f; // reset
		}
		float stickLength = sqrt(stickX * stickX + stickY * stickY);
		if (stickLength != 0.0f)
		{
			anyStickInput = true;
			float warpedStickLengthX = pow(stickLength, getSetting(SettingID::STICK_POWER));
			float warpedStickLengthY = warpedStickLengthX;
			warpedStickLengthX *= getSetting<FloatXY>(SettingID::STICK_SENS).first * getSetting(SettingID::REAL_WORLD_CALIBRATION) / os_mouse_speed / getSetting(SettingID::IN_GAME_SENS);
			warpedStickLengthY *= getSetting<FloatXY>(SettingID::STICK_SENS).second * getSetting(SettingID::REAL_WORLD_CALIBRATION) / os_mouse_speed / getSetting(SettingID::IN_GAME_SENS);
			camSpeedX += stickX / stickLength * warpedStickLengthX * stick.acceleration * deltaTime;
			camSpeedY += stickY / stickLength * warpedStickLengthY * stick.acceleration * deltaTime;
			if (pegged)
			{
				stick.acceleration += getSetting(SettingID::STICK_ACCELERATION_RATE) * deltaTime;
				auto cap = getSetting(SettingID::STICK_ACCELERATION_CAP);
				if (stick.acceleration > cap)
				{
					stick.acceleration = cap;
				}
			}
		}
	}
	else if (stickMode == StickMode::MOUSE_AREA)
	{
		auto mouse_ring_radius = getSetting(SettingID::MOUSE_RING_RADIUS);

		float mouseX = (rawX - rawLastX) * mouse_ring_radius;
		float mouseY = (rawY - rawLastY) * -1 * mouse_ring_radius;
		// do it!
		moveMouse(mouseX, mouseY);
	}
	else if (stickMode == StickMode::MOUSE_RING)
	{
		if (stickX != 0.0f || stickY != 0.0f)
		{
			auto mouse_ring_radius = getSetting(SettingID::MOUSE_RING_RADIUS);
			float stickLength = sqrt(stickX * stickX + stickY * stickY);
			float normX = stickX / stickLength;
			float normY = stickY / stickLength;
			// use screen resolution
			float mouseX = getSetting(SettingID::SCREEN_RESOLUTION_X) * 0.5f + 0.5f + normX * mouse_ring_radius;
			float mouseY = getSetting(SettingID::SCREEN_RESOLUTION_X) * 0.5f + 0.5f - normY * mouse_ring_radius;
			// normalize
			mouseX = mouseX / getSetting(SettingID::SCREEN_RESOLUTION_X);
			mouseY = mouseY / getSetting(SettingID::SCREEN_RESOLUTION_Y);
			// do it!
			setMouseNorm(mouseX, mouseY);
			lockMouse = true;
		}
	}
	else if (stickMode == StickMode::SCROLL_WHEEL)
	{
		if (stick.scroll.isInitialized())
		{
			if (stickX == 0 && stickY == 0)
			{
				stick.scroll.reset(_timeNow);
			}
			else if (stick.lastX != 0 && stick.lastY != 0)
			{
				float lastAngle = atan2f(stick.lastY, stick.lastX) / M_PI * 180.f;
				float angle = atan2f(stickY, stickX) / M_PI * 180.f;
				if (((lastAngle > 0) ^ (angle > 0)) && fabsf(angle - lastAngle) > 270.f) // Handle loop the loop
				{
					lastAngle = lastAngle > 0 ? lastAngle - 360.f : lastAngle + 360.f;
				}
				// COUT << "Stick moved from " << lastAngle << " to " << angle; // << '\n';
				stick.scroll.processScroll(angle - lastAngle, getSetting<FloatXY>(SettingID::SCROLL_SENS).x(), _timeNow);
			}
		}
	}
	else if (stickMode == StickMode::NO_MOUSE || stickMode == StickMode::INNER_RING || stickMode == StickMode::OUTER_RING)
	{ // Do not do if invalid
		// left!
		handleButtonChange(stick._leftId, left, stick._touchpadIndex);
		// right!
		handleButtonChange(stick._rightId, right, stick._touchpadIndex);
		// up!
		handleButtonChange(stick._upId, up, stick._touchpadIndex);
		// down!
		handleButtonChange(stick._downId, down, stick._touchpadIndex);

		anyStickInput = left || right || up || down; // ring doesn't count
	}
	else if (stickMode == StickMode::LEFT_STICK || stickMode == StickMode::RIGHT_STICK)
	{
		if (_context->_vigemController)
		{
			anyStickInput = processGyroStick(stick.lastX, stick.lastY, stickLength, stickMode, false);
		}
	}
	else if (stickMode >= StickMode::LEFT_ANGLE_TO_X && stickMode <= StickMode::RIGHT_ANGLE_TO_Y)
	{
		if (_context->_vigemController && rawLength > innerDeadzone)
		{
			bool isX = stickMode == StickMode::LEFT_ANGLE_TO_X || stickMode == StickMode::RIGHT_ANGLE_TO_X;
			bool isLeft = stickMode == StickMode::LEFT_ANGLE_TO_X || stickMode == StickMode::LEFT_ANGLE_TO_Y;

			float stickAngle = isX ? atan2f(stickX, absY) : atan2f(stickY, absX);
			float absAngle = abs(stickAngle * 180.0f / M_PI);
			float signAngle = stickAngle < 0.f ? -1.f : 1.f;
			float angleDeadzoneInner = getSetting(SettingID::ANGLE_TO_AXIS_DEADZONE_INNER);
			float angleDeadzoneOuter = getSetting(SettingID::ANGLE_TO_AXIS_DEADZONE_OUTER);
			float absStickValue = clamp((absAngle - angleDeadzoneInner) / (90.f - angleDeadzoneOuter - angleDeadzoneInner), 0.f, 1.f);

			absStickValue *= pow(stickLength, getSetting(SettingID::STICK_POWER));

			// now actually convert to output stick value, taking deadzones and power curve into account
			float undeadzoneInner, undeadzoneOuter, unpower;
			if (isLeft)
			{
				undeadzoneInner = getSetting(SettingID::LEFT_STICK_UNDEADZONE_INNER);
				undeadzoneOuter = getSetting(SettingID::LEFT_STICK_UNDEADZONE_OUTER);
				unpower = getSetting(SettingID::LEFT_STICK_UNPOWER);
			}
			else
			{
				undeadzoneInner = getSetting(SettingID::RIGHT_STICK_UNDEADZONE_INNER);
				undeadzoneOuter = getSetting(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);
				unpower = getSetting(SettingID::RIGHT_STICK_UNPOWER);
			}

			float livezoneSize = 1.f - undeadzoneOuter - undeadzoneInner;
			if (livezoneSize > 0.f)
			{
				anyStickInput = true;

				// unpower curve
				if (unpower != 0.f)
				{
					absStickValue = pow(absStickValue, 1.f / unpower);
				}

				if (absStickValue < 1.f)
				{
					absStickValue = undeadzoneInner + absStickValue * livezoneSize;
				}

				float signedStickValue = signAngle * absStickValue;
				if (isX)
				{
					_context->_vigemController->setStick(signedStickValue, 0.f, isLeft);
				}
				else
				{
					_context->_vigemController->setStick(0.f, signedStickValue, isLeft);
				}
			}
		}
	}
	else if (stickMode == StickMode::LEFT_WIND_X || stickMode == StickMode::RIGHT_WIND_X)
	{
		if (_context->_vigemController)
		{
			bool isLeft = stickMode == StickMode::LEFT_WIND_X;

			float &currentWindingAngle = isLeft ? _windingAngleLeft : _windingAngleRight;

			// currently, just use the same hard-coded thresholds we use for flick stick. These are affected by deadzones
			if (stickLength > 0.f && stick.lastX != 0.f && stick.lastY != 0.f)
			{
				// use difference between last stick angle and current
				float stickAngle = atan2f(-stickX, stickY);
				float lastStickAngle = atan2f(-stick.lastX, stick.lastY);
				float angleChange = fmod((stickAngle - lastStickAngle) + M_PI, 2.0f * M_PI);
				if (angleChange < 0)
					angleChange += 2.0f * M_PI;
				angleChange -= M_PI;

				currentWindingAngle -= angleChange * stickLength * 180.f / M_PI;

				anyStickInput = true;
			}

			if (stickLength < 1.f)
			{
				float absWindingAngle = abs(currentWindingAngle);
				float unwindAmount = getSetting(SettingID::UNWIND_RATE) * (1.f - stickLength) * deltaTime;
				float windingSign = currentWindingAngle < 0.f ? -1.f : 1.f;
				if (absWindingAngle <= unwindAmount)
				{
					currentWindingAngle = 0.f;
				}
				else
				{
					currentWindingAngle -= unwindAmount * windingSign;
				}
			}

			float newWindingSign = currentWindingAngle < 0.f ? -1.f : 1.f;
			float newAbsWindingAngle = abs(currentWindingAngle);

			float windingRange = getSetting(SettingID::WIND_STICK_RANGE);
			float windingPower = getSetting(SettingID::WIND_STICK_POWER);
			if (windingPower == 0.f)
			{
				windingPower = 1.f;
			}

			float windingRemapped = min(pow(newAbsWindingAngle / windingRange * 2.f, windingPower), 1.f);

			// let's account for deadzone!
			float undeadzoneInner, undeadzoneOuter, unpower;
			if (isLeft)
			{
				undeadzoneInner = getSetting(SettingID::LEFT_STICK_UNDEADZONE_INNER);
				undeadzoneOuter = getSetting(SettingID::LEFT_STICK_UNDEADZONE_OUTER);
				unpower = getSetting(SettingID::LEFT_STICK_UNPOWER);
			}
			else
			{
				undeadzoneInner = getSetting(SettingID::RIGHT_STICK_UNDEADZONE_INNER);
				undeadzoneOuter = getSetting(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);
				unpower = getSetting(SettingID::RIGHT_STICK_UNPOWER);
			}

			float livezoneSize = 1.f - undeadzoneOuter - undeadzoneInner;
			if (livezoneSize > 0.f)
			{
				anyStickInput = true;

				// unpower curve
				if (unpower != 0.f)
				{
					windingRemapped = pow(windingRemapped, 1.f / unpower);
				}

				if (windingRemapped < 1.f)
				{
					windingRemapped = undeadzoneInner + windingRemapped * livezoneSize;
				}

				float signedStickValue = newWindingSign * windingRemapped;
				_context->_vigemController->setStick(signedStickValue, 0.f, isLeft);
			}
		}
	}
	else if (stickMode == StickMode::HYBRID_AIM)
	{
		float velocityX = rawX - rawLastX;
		float velocityY = -rawY + rawLastY;
		float velocity = sqrt(velocityX * velocityX + velocityY * velocityY);
		float velocityRadial = radial(velocityX, velocityY, rawX, -rawY);
		float deflection = rawLength;
		float previousDeflection = sqrt(rawLastX * rawLastX + rawLastY * rawLastY);
		float magnitude = 0;
		float angle = atan2f(-rawY, rawX);
		bool inDeadzone = false;

		// check deadzones
		if (deflection > innerDeadzone)
		{
			inDeadzone = false;
			magnitude = (deflection - innerDeadzone) / (outerDeadzone - innerDeadzone);

			// check outer_deadzone
			if (deflection > outerDeadzone)
			{
				// clip outward radial velocity
				if (velocityRadial > 0.0f)
				{
					float dotProduct = velocityX * sin(angle) + velocityY * -cos(angle);
					velocityX = dotProduct * sin(angle);
					velocityY = dotProduct * -cos(angle);
				}
				magnitude = 1.0f;

				// check entering
				if (previousDeflection <= outerDeadzone)
				{
					float averageVelocityX = 0.0f;
					float averageVelocityY = 0.0f;
					int steps = 0;
					int counter = stick.smoothingCounter;
					while (steps < stick.SMOOTHING_STEPS) // sqrt(cumulativeVX * cumulativeVX + cumulativeVY * cumulativeVY) < smoothingDistance &&
					{
						averageVelocityX += stick.previousVelocitiesX[counter];
						averageVelocityY += stick.previousVelocitiesY[counter];
						if (counter == 0)
							counter = stick.SMOOTHING_STEPS - 1;
						else
							counter--;
						steps++;
					}

					if (getSetting<Switch>(SettingID::EDGE_PUSH_IS_ACTIVE) == Switch::ON)
					{
						stick.edgePushAmount *= stick.smallestMagnitude;
						stick.edgePushAmount += radial(averageVelocityX, averageVelocityY, rawX, -rawY) / steps;
						stick.smallestMagnitude = 1.f;
					}
				}
			}
		}
		else
		{
			stick.edgePushAmount = 0.0f;
			inDeadzone = true;
		}

		if (magnitude < stick.smallestMagnitude)
			stick.smallestMagnitude = magnitude;

		// compute output
		FloatXY sticklikeFactor = getSetting<FloatXY>(SettingID::STICK_SENS);
		FloatXY mouselikeFactor = getSetting<FloatXY>(SettingID::MOUSELIKE_FACTOR);
		float outputX = sticklikeFactor.x() / 2.f * pow(magnitude, getSetting(SettingID::STICK_POWER)) * cos(angle) * deltaTime;
		float outputY = sticklikeFactor.y() / 2.f * pow(magnitude, getSetting(SettingID::STICK_POWER)) * sin(angle) * deltaTime;
		outputX += mouselikeFactor.x() * pow(stick.smallestMagnitude, getSetting(SettingID::STICK_POWER)) * cos(angle) * stick.edgePushAmount;
		outputY += mouselikeFactor.y() * pow(stick.smallestMagnitude, getSetting(SettingID::STICK_POWER)) * sin(angle) * stick.edgePushAmount;
		outputX += mouselikeFactor.x() * velocityX;
		outputY += mouselikeFactor.y() * velocityY;

		// for smoothing edgePush and clipping on returning to center
		// probably only needed as deltaTime is faster than the controller's polling rate
		// (was tested with xbox360 controller which has a polling rate of 125Hz)
		if (stick.smoothingCounter < stick.SMOOTHING_STEPS - 1)
			stick.smoothingCounter++;
		else
			stick.smoothingCounter = 0;

		stick.previousVelocitiesX[stick.smoothingCounter] = velocityX;
		stick.previousVelocitiesY[stick.smoothingCounter] = velocityY;
		float outputRadial = radial(outputX, outputY, rawX, -rawY);
		stick.previousOutputRadial[stick.smoothingCounter] = outputRadial;
		stick.previousOutputX[stick.smoothingCounter] = outputX;
		stick.previousOutputY[stick.smoothingCounter] = outputY;

		if (getSetting<Switch>(SettingID::RETURN_DEADZONE_IS_ACTIVE) == Switch::ON)
		{
			// 0 means deadzone fully active 1 means unaltered output
			float averageOutputX = 0.f;
			float averageOutputY = 0.f;
			for (int i = 0; i < stick.SMOOTHING_STEPS; i++)
			{
				averageOutputX += stick.previousOutputX[i];
				averageOutputY += stick.previousOutputY[i];
			}
			float averageOutput = sqrt(averageOutputX * averageOutputX + averageOutputY * averageOutputY) / stick.SMOOTHING_STEPS;
			averageOutputX /= stick.SMOOTHING_STEPS;
			averageOutputY /= stick.SMOOTHING_STEPS;

			float averageOutputRadial = 0;
			for (int i = 0; i < stick.SMOOTHING_STEPS; i++)
			{
				averageOutputRadial += stick.previousOutputRadial[i];
			}
			averageOutputRadial /= stick.SMOOTHING_STEPS;
			float returnDeadzone1 = 1.f;
			float angleOutputToCenter = 0;
			const float returningDeadzone = getSetting(SettingID::RETURN_DEADZONE_ANGLE) / 180.f * M_PI;
			const float returningCutoff = getSetting(SettingID::RETURN_DEADZONE_ANGLE_CUTOFF) / 180.f * M_PI;

			if (averageOutputRadial < 0.f)
			{
				angleOutputToCenter = abs(M_PI - acosf((averageOutputX * rawLastX + averageOutputY * -rawLastY) / (averageOutput * previousDeflection))); /// STILL WRONG
				returnDeadzone1 = angleBasedDeadzone(angleOutputToCenter, returningDeadzone, returningCutoff);
			}
			float returnDeadzone2 = 1.f;
			if (inDeadzone)
			{
				if (averageOutputRadial < 0.f)
				{
					// if angle inward output deadzone based on tangent concentric circle
					float angleEquivalent = abs(rawLastX * averageOutputY + -rawLastY * -averageOutputX) / averageOutput;
					returnDeadzone2 = angleBasedDeadzone(angleEquivalent, returningDeadzone, returningCutoff);
				}
				else
				{
					// output deadzone based on distance to center
					float angleEquivalent = asinf(previousDeflection / innerDeadzone);
					returnDeadzone2 = angleBasedDeadzone(angleEquivalent, returningDeadzone, returningCutoff);
				}
			}
			float returnDeadzone = min({ returnDeadzone1, returnDeadzone2 });
			outputX *= returnDeadzone;
			outputY *= returnDeadzone;
			if (returnDeadzone == 0.f)
				stick.edgePushAmount = 0.f;
		}
		moveMouse(outputX, outputY);
	}
}

void JoyShock::handleTouchStickChange(TouchStick &ts, bool down, short movX, short movY, float delta_time)
{
	float stickX = down ? clamp<float>((ts._currentLocation.x() + movX) / getSetting(SettingID::TOUCH_STICK_RADIUS), -1.f, 1.f) : 0.f;
	float stickY = down ? clamp<float>((ts._currentLocation.y() - movY) / getSetting(SettingID::TOUCH_STICK_RADIUS), -1.f, 1.f) : 0.f;
	float innerDeadzone = getSetting(SettingID::TOUCH_DEADZONE_INNER);
	RingMode ringMode = getSetting<RingMode>(SettingID::TOUCH_RING_MODE);
	StickMode stickMode = getSetting<StickMode>(SettingID::TOUCH_STICK_MODE);
	ControllerOrientation controllerOrientation = getSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION);
	float mouseCalibrationFactor = 180.0f / M_PI / os_mouse_speed;

	bool anyStickInput = false;
	bool lockMouse = false;
	float camSpeedX = 0.f;
	float camSpeedY = 0.f;
	auto axisSign = getSetting<AxisSignPair>(SettingID::TOUCH_STICK_AXIS);

	stickX *= float(axisSign.first);
	stickY *= float(axisSign.second);
	processStick(stickX, stickY, ts, mouseCalibrationFactor, delta_time, anyStickInput, lockMouse, camSpeedX, camSpeedY);
	ts.lastX = stickX;
	ts.lastY = stickY;

	moveMouse(camSpeedX * float(getSetting<AxisSignPair>(SettingID::TOUCH_STICK_AXIS).first), -camSpeedY * float(getSetting<AxisSignPair>(SettingID::TOUCH_STICK_AXIS).second));

	if (!down && ts._prevDown)
	{
		ts._currentLocation = { 0.f, 0.f };

		ts.is_flicking = false;
		ts.acceleration = 1.0;
		ts.ignore_stick_mode = false;
	}
	else
	{
		ts._currentLocation = { ts._currentLocation.x() + movX, ts._currentLocation.y() - movY };
	}

	ts._prevDown = down;
}

bool JoyShock::processGyroStick(float stickX, float stickY, float stickLength, StickMode stickMode, bool forceOutput)

{
	GyroOutput gyroOutput = getSetting<GyroOutput>(SettingID::GYRO_OUTPUT);
	bool isLeft = stickMode == StickMode::LEFT_STICK;
	bool gyroMatchesStickMode = (gyroOutput == GyroOutput::LEFT_STICK && stickMode == StickMode::LEFT_STICK) || (gyroOutput == GyroOutput::RIGHT_STICK && stickMode == StickMode::RIGHT_STICK) || stickMode == StickMode::INVALID;

	float undeadzoneInner, undeadzoneOuter, unpower, virtualScale;
	if (isLeft)
	{
		undeadzoneInner = getSetting(SettingID::LEFT_STICK_UNDEADZONE_INNER);
		undeadzoneOuter = getSetting(SettingID::LEFT_STICK_UNDEADZONE_OUTER);
		unpower = getSetting(SettingID::LEFT_STICK_UNPOWER);
		virtualScale = getSetting(SettingID::LEFT_STICK_VIRTUAL_SCALE);
	}
	else
	{
		undeadzoneInner = getSetting(SettingID::RIGHT_STICK_UNDEADZONE_INNER);
		undeadzoneOuter = getSetting(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);
		unpower = getSetting(SettingID::RIGHT_STICK_UNPOWER);
		virtualScale = getSetting(SettingID::RIGHT_STICK_VIRTUAL_SCALE);
	}

	// in order to correctly combine gyro and stick, we need to calculate what the stick aiming is supposed to be doing, add gyro result to it, and convert back to stick
	float maxStickGameSpeed = getSetting(SettingID::VIRTUAL_STICK_CALIBRATION);
	float livezoneSize = 1.f - undeadzoneOuter - undeadzoneInner;
	if (livezoneSize <= 0.f || maxStickGameSpeed <= 0.f)
	{
		// can't do anything with that
		processed_gyro_stick |= gyroMatchesStickMode;
		return false;
	}
	if (unpower == 0.f)
		unpower = 1.f;
	float stickVelocity = pow(clamp<float>((stickLength - undeadzoneInner) / livezoneSize, 0.f, 1.f), unpower) * maxStickGameSpeed * virtualScale;
	float expectedX = 0.f;
	float expectedY = 0.f;
	if (stickVelocity > 0.f)
	{
		expectedX = stickX / stickLength * stickVelocity;
		expectedY = -stickY / stickLength * stickVelocity;
	}

	if (gyroMatchesStickMode || forceOutput)
	{
		expectedX += gyroXVelocity;
		expectedY += gyroYVelocity;
	}

	float targetGyroVelocity = sqrtf(expectedX * expectedX + expectedY * expectedY);
	// map gyro velocity to achievable range in 0-1
	float gyroInStickStrength = targetGyroVelocity >= maxStickGameSpeed ? 1.f : targetGyroVelocity / maxStickGameSpeed;
	// unpower curve
	if (unpower != 0.f)
	{
		gyroInStickStrength = pow(gyroInStickStrength, 1.f / unpower);
	}
	// remap to between inner and outer deadzones
	float gyroStickX = 0.f;
	float gyroStickY = 0.f;
	if (gyroInStickStrength > 0.01f)
	{
		gyroInStickStrength = undeadzoneInner + gyroInStickStrength * livezoneSize;
		gyroStickX = expectedX / targetGyroVelocity * gyroInStickStrength;
		gyroStickY = expectedY / targetGyroVelocity * gyroInStickStrength;
	}
	if (_context->_vigemController)
	{
		if (stickLength <= undeadzoneInner)
		{
			if (gyroInStickStrength == 0.f)
			{
				// hack to help with finding deadzones more quickly
				_context->_vigemController->setStick(undeadzoneInner, 0.f, isLeft);
			}
			else
			{
				_context->_vigemController->setStick(gyroStickX, -gyroStickY, isLeft);
			}
		}
		else
		{
			_context->_vigemController->setStick(gyroStickX, -gyroStickY, isLeft);
		}
	}

	processed_gyro_stick |= gyroMatchesStickMode;

	return stickLength > undeadzoneInner;
}