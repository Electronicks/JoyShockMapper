#include "DigitalButton.h"
#include "JSMVariable.hpp"
#include "GamepadMotion.h"
#include "InputHelpers.h"

// Append to pocketfsm macro
#define DB_CONCRETE_STATE(statename)           \
	CONCRETE_STATE(statename)                  \
	virtual BtnState getState() const override \
	{                                          \
		return BtnState::statename;            \
	}

// JSM global variables
extern JSMVariable<ControllerScheme> virtual_controller;
extern JSMVariable<float> sim_press_window;
extern JSMVariable<float> dbl_press_window;
extern JSMSetting<JoyconMask> joycon_gyro_mask;
extern JSMSetting<JoyconMask> joycon_motion_mask;

struct Sync
{
	BtnState nextState;
	chrono::steady_clock::time_point pressTime;
	Mapping *activeMapping;
	string nameToRelease;
	float turboTime;
	float holdTime;
};

// Hidden implementation of the digital button
// This class holds all the logic related to a single digital button. It does not hold the mapping but only a reference
// to it. It also contains it's various states, flags and data. The concrete state of the state machine hands off the
// instance to the next state, and so is persistent across states
struct DigitalButtonImpl : public pocket_fsm::PimplBase, public EventActionIf
{
private:
	unsigned int _turboCount = 0;
	vector<BtnEvent> _instantReleaseQueue;

public:
	DigitalButtonImpl(JSMButton &mapping, shared_ptr<DigitalButton::Common> common)
	  : _id(mapping._id)
	  , _common(common)
	  , _press_times()
	  , _keyToRelease()
	  , _mapping(mapping)
	  , _instantReleaseQueue()
	{
		_instantReleaseQueue.reserve(2);
	}

	const ButtonID _id; // Always ID first for easy debugging
	string _nameToRelease;
	shared_ptr<DigitalButton::Common> _common;
	chrono::steady_clock::time_point _press_times;
	unique_ptr<Mapping> _keyToRelease; // At key press, remember what to release
	const JSMButton &_mapping;
	DigitalButton *_simPressMaster = nullptr;

	// Pretty wrapper
	inline float GetPressDurationMS(chrono::steady_clock::time_point time_now)
	{
		return static_cast<float>(chrono::duration_cast<chrono::milliseconds>(time_now - _press_times).count());
	}

	void ClearKey()
	{
		_keyToRelease.reset();
		_instantReleaseQueue.clear();
		_nameToRelease.clear();
		_turboCount = 0;
	}

	bool CheckInstantRelease(BtnEvent instantEvent)
	{
		auto instant = find(_instantReleaseQueue.begin(), _instantReleaseQueue.end(), instantEvent);
		if (instant != _instantReleaseQueue.end())
		{
			//COUT << "Button " << _id << " releases instant " << instantEvent << endl;
			_keyToRelease->ProcessEvent(BtnEvent::OnInstantRelease, *this);
			_instantReleaseQueue.erase(instant);
			return true;
		}
		return false;
	}

	Mapping *GetPressMapping()
	{
		if (!_keyToRelease)
		{
			// Look at active chord mappings starting with the latest activates chord
			for (auto activeChord = _common->chordStack.cbegin(); activeChord != _common->chordStack.cend(); activeChord++)
			{
				auto binding = _mapping.get(*activeChord);
				if (binding && *activeChord != _id)
				{
					_keyToRelease.reset(new Mapping(*binding));
					_nameToRelease = _mapping.getName(*activeChord);
					return _keyToRelease.get();
				}
			}
			// Chord stack should always include NONE which will provide a value in the loop above
			throw runtime_error("ChordStack should always include ButtonID::NONE, for the chorded variable to return the base value.");
		}
		return _keyToRelease.get();
	}

	void ProcessEvent(const Pressed &evt)
	{
		auto elapsed_time = GetPressDurationMS(evt.time_now);
		if (_turboCount == 0)
		{
			if (elapsed_time > MAGIC_INSTANT_DURATION)
			{
				CheckInstantRelease(BtnEvent::OnPress);
			}
			if (elapsed_time > evt.holdTime)
			{
				_keyToRelease->ProcessEvent(BtnEvent::OnHold, *this);
				_keyToRelease->ProcessEvent(BtnEvent::OnTurbo, *this);
				_turboCount++;
			}
		}
		else
		{
			if (elapsed_time > evt.holdTime + MAGIC_INSTANT_DURATION)
			{
				CheckInstantRelease(BtnEvent::OnHold);
			}
			if (floorf((elapsed_time - evt.holdTime) / evt.turboTime) >= _turboCount)
			{
				_keyToRelease->ProcessEvent(BtnEvent::OnTurbo, *this);
				_turboCount++;
			}
			if (elapsed_time > evt.holdTime + _turboCount * evt.turboTime + MAGIC_INSTANT_DURATION)
			{
				CheckInstantRelease(BtnEvent::OnTurbo);
			}
		}
	}

	BtnState ProcessEvent(const Released &evt)
	{
		BtnState nextState = BtnState::INVALID;

		_keyToRelease->ProcessEvent(BtnEvent::OnRelease, *this);
		if (_turboCount == 0)
		{
			_keyToRelease->ProcessEvent(BtnEvent::OnTap, *this);
			nextState = BtnState::TapRelease;
			_press_times = evt.time_now; // Start counting tap duration
		}
		else
		{
			_keyToRelease->ProcessEvent(BtnEvent::OnHoldRelease, *this);
			if (_instantReleaseQueue.empty())
			{
				nextState = BtnState::NoPress;
				ClearKey();
			}
			else
			{
				nextState = BtnState::InstRelease;
				_press_times = evt.time_now; // Start counting tap duration
			}
		}
		return nextState;
	}

	void RegisterInstant(BtnEvent evt) override
	{
		//COUT << "Button " << _id << " registers instant " << evt << endl;
		_instantReleaseQueue.push_back(evt);
	}

	void ApplyGyroAction(KeyCode gyroAction) override
	{
		_common->gyroActionQueue.push_back({ _id, gyroAction });
	}

	void RemoveGyroAction() override
	{
		auto gyroAction = find_if(_common->gyroActionQueue.begin(), _common->gyroActionQueue.end(),
		  [this](auto pair) {
			  // On a sim press, release the master button (the one who triggered the press)
			  return pair.first == (_simPressMaster ? _simPressMaster->_id : _id);
		  });
		if (gyroAction != _common->gyroActionQueue.end())
		{
			ClearAllActiveToggle(gyroAction->second);
			_common->gyroActionQueue.erase(gyroAction);
		}
	}

	void SetRumble(int smallRumble, int bigRumble) override
	{
		COUT << "Rumbling at " << smallRumble << " and " << bigRumble << endl;
		_common->_rumble(smallRumble, bigRumble);
	}

	void ApplyBtnPress(KeyCode key) override

	{
		if (key.code >= X_UP && key.code <= X_START || key.code == PS_HOME || key.code == PS_PAD_CLICK)
		{
			if (_common->_vigemController)
				_common->_vigemController->setButton(key, true);
		}
		else if (key.code != NO_HOLD_MAPPED)
		{
			pressKey(key, true);
		}
	}

	void ApplyBtnRelease(KeyCode key) override
	{
		if (key.code >= X_UP && key.code <= X_START || key.code == PS_HOME || key.code == PS_PAD_CLICK)
		{
			if (_common->_vigemController)
				_common->_vigemController->setButton(key, false);
		}
		else if (key.code != NO_HOLD_MAPPED)
		{
			pressKey(key, false);
			ClearAllActiveToggle(key);
		}
	}

	void ApplyButtonToggle(KeyCode key, EventActionIf::Callback apply, EventActionIf::Callback release) override
	{
		auto currentlyActive = find_if(_common->activeTogglesQueue.begin(), _common->activeTogglesQueue.end(),
		  [this, key](pair<ButtonID, KeyCode> pair) {
			  return pair.first == _id && pair.second == key;
		  });
		if (currentlyActive == _common->activeTogglesQueue.end())
		{
			apply(this);
			_common->activeTogglesQueue.push_front({ _id, key });
		}
		else
		{
			release(this); // The bound action here should always erase the active toggle from the queue
		}
	}

	void ClearAllActiveToggle(KeyCode key)
	{
		static const std::function<bool(pair<ButtonID, KeyCode>)> isSameKey = [key](pair<ButtonID, KeyCode> pair) {
			return pair.second == key;
		};

		for (auto currentlyActive = find_if(_common->activeTogglesQueue.begin(), _common->activeTogglesQueue.end(), isSameKey);
		     currentlyActive != _common->activeTogglesQueue.end();
		     currentlyActive = find_if(_common->activeTogglesQueue.begin(), _common->activeTogglesQueue.end(), isSameKey))
		{
			_common->activeTogglesQueue.erase(currentlyActive);
		}
	}

	void StartCalibration() override
	{
		COUT << "Starting continuous calibration" << endl;
		if (_common->leftMotion)
		{
			int gyroMask = int(joycon_gyro_mask.get().value());
			if (gyroMask & int(JoyconMask::IGNORE_LEFT) || gyroMask & int(JoyconMask::IGNORE_LEFT))
			{
				_common->rightMainMotion->ResetContinuousCalibration();
				_common->rightMainMotion->StartContinuousCalibration();
			}
			int motionMask = int(joycon_motion_mask.get().value());
			if (motionMask & int(JoyconMask::IGNORE_RIGHT) || motionMask & int(JoyconMask::IGNORE_RIGHT))
			{
				_common->leftMotion->ResetContinuousCalibration();
				_common->leftMotion->StartContinuousCalibration();
			}
		}
		else
		{
			_common->rightMainMotion->ResetContinuousCalibration();
			_common->rightMainMotion->StartContinuousCalibration();
		}
	}

	void FinishCalibration() override
	{
		if (_common->leftMotion)
		{
			int gyroMask = int(joycon_gyro_mask.get().value());
			if (gyroMask & int(JoyconMask::IGNORE_LEFT) || gyroMask & int(JoyconMask::IGNORE_LEFT))
			{
				_common->rightMainMotion->PauseContinuousCalibration();
			}
			int motionMask = int(joycon_motion_mask.get().value());
			if (motionMask & int(JoyconMask::IGNORE_RIGHT) || motionMask & int(JoyconMask::IGNORE_RIGHT))
			{
				_common->rightMainMotion->PauseContinuousCalibration();
			}
		}
		else
		{
			_common->rightMainMotion->PauseContinuousCalibration();
		}
		COUT << "Gyro calibration set" << endl;
		ClearAllActiveToggle(KeyCode("CALIBRATE"));
	}

	const char *getDisplayName() override
	{
		return _nameToRelease.c_str();
	}
};

// Forward declare concrete states
class NoPress;
class BtnPress;
class TapRelease;
class WaitSim;
class SimPress;
class SimRelease;
class DblPressStart;
class DblPressNoPressTap;
class DblPressNoPressHold;
class DblPressPress;
class InstRelease;

void DigitalButtonState::changeState(BtnState next)
{
#define CASE(statename)           \
	case BtnState::statename:     \
		changeState<statename>(); \
		break

	switch (next)
	{
		CASE(NoPress);
		CASE(BtnPress);
		CASE(TapRelease);
		CASE(WaitSim);
		CASE(SimPress);
		CASE(SimRelease);
		CASE(DblPressStart);
		CASE(DblPressNoPressTap);
		CASE(DblPressNoPressHold);
		CASE(DblPressPress);
		CASE(InstRelease);
	}
}

void DigitalButtonState::react(OnEntry &e)
{
	// Uncomment below to diplay a log each time a button changes state
	DEBUG << "Button " << pimpl()->_id << " is now in state " << _name << endl;
}

// Basic Press reaction should be called in every concrete Press reaction
void DigitalButtonState::react(Pressed &e)
{
	if (pimpl()->_id < ButtonID::SIZE || pimpl()->_id >= ButtonID::T1) // Can't chord touch stick buttons
	{
		auto foundChord = find(pimpl()->_common->chordStack.begin(), pimpl()->_common->chordStack.end(), pimpl()->_id);
		if (foundChord == pimpl()->_common->chordStack.end())
		{
			//COUT << "Button " << index << " is pressed!" << endl;
			pimpl()->_common->chordStack.push_front(pimpl()->_id); // Always push at the fromt to make it a stack
		}
	}
}

// Basic Release reaction should be called in every concrete Release reaction
void DigitalButtonState::react(Released &e)
{
	if (pimpl()->_id < ButtonID::SIZE || pimpl()->_id >= ButtonID::T1) // Can't chord touch stick buttons?!?
	{
		auto foundChord = find(pimpl()->_common->chordStack.begin(), pimpl()->_common->chordStack.end(), pimpl()->_id);
		if (foundChord != pimpl()->_common->chordStack.end())
		{
			//COUT << "Button " << index << " is released!" << endl;
			pimpl()->_common->chordStack.erase(foundChord); // The chord is released
		}
	}
}

void DigitalButtonState::react(chrono::steady_clock::time_point &e)
{
	// final implementation. All states can be assigned a new press time
	pimpl()->_press_times = e;
}

void DigitalButtonState::react(GetDuration &e)
{
	// final implementation. All states can be querried it's duration time.
	e.out_duration = pimpl()->GetPressDurationMS(e.in_now);
}

class NoPress : public DigitalButtonState
{
	DB_CONCRETE_STATE(NoPress)
	INITIAL_STATE(NoPress)

	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		pimpl()->_press_times = e.time_now;
		if (pimpl()->_mapping.HasSimMappings())
		{
			changeState<WaitSim>();
		}
		else if (pimpl()->_mapping.getDblPressMap())
		{
			// Start counting time between two start presses
			changeState<DblPressStart>();
		}
		else
		{
			changeState<BtnPress>();
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
	}
};

class BtnPress : public DigitalButtonState
{
	DB_CONCRETE_STATE(BtnPress)

	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		pimpl()->ProcessEvent(e);
	}
	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		changeState(pimpl()->ProcessEvent(e));
	}
};

class TapRelease : public DigitalButtonState
{
	DB_CONCRETE_STATE(TapRelease)

	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		pimpl()->CheckInstantRelease(BtnEvent::OnRelease);
		pimpl()->CheckInstantRelease(BtnEvent::OnTap);
		pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnTapRelease, *pimpl());
		pimpl()->ClearKey();
	}

	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		if (pimpl()->GetPressDurationMS(e.time_now) > MAGIC_INSTANT_DURATION)
		{
			pimpl()->CheckInstantRelease(BtnEvent::OnRelease);
			pimpl()->CheckInstantRelease(BtnEvent::OnTap);
		}
		if (!pimpl()->_keyToRelease || pimpl()->GetPressDurationMS(e.time_now) > pimpl()->_keyToRelease->getTapDuration())
		{
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnTapRelease, *pimpl());
			changeState<NoPress>();
			pimpl()->ClearKey();
		}
	}
};

class WaitSim : public DigitalButtonState
{
	DB_CONCRETE_STATE(WaitSim)

	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		// Is there a sim mapping on this button where the other button is in WaitSim state too?
		auto simBtn = pimpl()->_common->_getMatchingSimBtn(pimpl()->_id);
		if (simBtn)
		{
			changeState<SimPress>();
			pimpl()->_press_times = e.time_now;                                                          // Reset Timer
			pimpl()->_keyToRelease.reset(new Mapping(pimpl()->_mapping.AtSimPress(simBtn->_id)->get())); // Make a copy
			pimpl()->_nameToRelease = pimpl()->_mapping.getSimPressName(simBtn->_id);
			pimpl()->_simPressMaster = simBtn; // Second to press is the slave

			Sync sync;
			sync.nextState = BtnState::SimPress;
			sync.pressTime = e.time_now;
			sync.activeMapping = new Mapping(*pimpl()->_keyToRelease);
			sync.nameToRelease = pimpl()->_nameToRelease;
			simBtn->sendEvent(sync);

			pimpl()->_keyToRelease->ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
		else if (pimpl()->GetPressDurationMS(e.time_now) > sim_press_window)
		{
			// Button is still pressed but Sim delay did expire
			if (pimpl()->_mapping.getDblPressMap())
			{
				// Start counting time between two start presses
				changeState<DblPressStart>();
			}
			else // Handle regular press mapping
			{
				changeState<BtnPress>();
				pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
				// _press_times = time_now;
			}
		}
		// Else let time flow, stay in this state, no output.
	}

	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		// Button was released before sim delay expired
		if (pimpl()->_mapping.getDblPressMap())
		{
			// Start counting time between two start presses
			changeState<DblPressStart>();
		}
		else
		{
			changeState<BtnPress>();
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
			//_press_times = time_now;
		}
	}

	REACT(Sync)
	override
	{
		pimpl()->_simPressMaster = nullptr;
		pimpl()->_press_times = e.pressTime;
		pimpl()->_keyToRelease.reset(e.activeMapping);
		pimpl()->_nameToRelease = e.nameToRelease;
		changeState(e.nextState);
	}
};

class SimPress : public DigitalButtonState
{
	DB_CONCRETE_STATE(SimPress)

	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		if (pimpl()->_simPressMaster)
		{
			if (pimpl()->_simPressMaster->getState() != BtnState::SimPress)
			{
				// The master button has released! change state now!
				changeState<SimRelease>();
				pimpl()->_simPressMaster = nullptr;
			}
			// else slave does nothing (ironically?)
		}
		else
		{
			// only the master does the work
			pimpl()->ProcessEvent(e);
		}
	}

	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		if (pimpl()->_simPressMaster)
		{
			if (pimpl()->_simPressMaster->getState() != BtnState::SimPress)
			{
				// The master button has released! change state now!
				changeState<SimRelease>();
				pimpl()->_simPressMaster = nullptr;
			}
			else
			{
				// Process at the master's end
				Sync sync;
				sync.pressTime = e.time_now;
				sync.holdTime = e.holdTime;
				sync.turboTime = e.turboTime;
				changeState(pimpl()->_simPressMaster->sendEvent(sync).nextState);
			}
		}
		else
		{
			// Master release processing, slave will notice
			changeState(pimpl()->ProcessEvent(e));
		}
	}

	REACT(Sync)
	override
	{
		e.nextState = pimpl()->ProcessEvent(Released{ e.pressTime, e.turboTime, e.holdTime });
		changeState<SimRelease>();
	}
};

class SimRelease : public DigitalButtonState
{
	DB_CONCRETE_STATE(SimRelease)

	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		changeState<NoPress>();
		pimpl()->ClearKey();
	}
};

class DblPressStart : public DigitalButtonState
{
	DB_CONCRETE_STATE(DblPressStart)
	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		if (pimpl()->GetPressDurationMS(e.time_now) > dbl_press_window)
		{
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
			changeState<BtnPress>();
			//_press_times = time_now; // Reset Timer
		}
	}

	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		if (pimpl()->GetPressDurationMS(e.time_now) > dbl_press_window)
		{
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
			changeState<BtnPress>();
			//_press_times = time_now; // Reset Timer
		}
		else
		{
			if (pimpl()->GetPressDurationMS(e.time_now) > e.holdTime)
			{
				changeState<DblPressNoPressHold>();
			}
			else
			{
				changeState<DblPressNoPressTap>();
			}
		}
	}
};

class DblPressNoPressTap : public DigitalButtonState
{
	DB_CONCRETE_STATE(DblPressNoPressTap)
	REACT(Pressed)
	override
	{
		if (pimpl()->GetPressDurationMS(e.time_now) > dbl_press_window)
		{
			changeState<BtnPress>();
			pimpl()->_press_times = e.time_now; // Reset Timer to raise a tap
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
		else
		{
			changeState<DblPressPress>();
			pimpl()->_press_times = e.time_now;
			pimpl()->_keyToRelease.reset(new Mapping(pimpl()->_mapping.getDblPressMap()->second));
			pimpl()->_nameToRelease = pimpl()->_mapping.getName(pimpl()->_id);
			pimpl()->_mapping.getDblPressMap()->second.get().ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
	}

	REACT(Released)
	override
	{
		if (pimpl()->GetPressDurationMS(e.time_now) > dbl_press_window)
		{
			changeState<BtnPress>();
			pimpl()->_press_times = e.time_now; // Reset Timer to raise a tap
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
	}
};

class DblPressNoPressHold : public DigitalButtonState
{
	DB_CONCRETE_STATE(DblPressNoPressHold)
	REACT(Pressed)
	{
		DigitalButtonState::react(e);
		if (pimpl()->GetPressDurationMS(e.time_now) > dbl_press_window)
		{
			changeState<BtnPress>();
			// Don't reset timer to preserve hold press behaviour
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
		else
		{
			changeState<DblPressPress>();
			pimpl()->_press_times = e.time_now;
			pimpl()->_keyToRelease.reset(new Mapping(pimpl()->_mapping.getDblPressMap()->second));
			pimpl()->_nameToRelease = pimpl()->_mapping.getName(pimpl()->_id);
			pimpl()->_mapping.getDblPressMap()->second.get().ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
	}

	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		if (pimpl()->GetPressDurationMS(e.time_now) > dbl_press_window)
		{
			changeState<BtnPress>();
			// Don't reset timer to preserve hold press behaviour
			pimpl()->GetPressMapping()->ProcessEvent(BtnEvent::OnPress, *pimpl());
		}
	}
};

class DblPressPress : public DigitalButtonState
{
	DB_CONCRETE_STATE(DblPressPress)
	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		pimpl()->ProcessEvent(e);
	}

	REACT(Released)
	override
	{
		DigitalButtonState::react(e);
		changeState(pimpl()->ProcessEvent(e));
	}
};

class InstRelease : public DigitalButtonState
{
	DB_CONCRETE_STATE(InstRelease)
	REACT(Pressed)
	override
	{
		DigitalButtonState::react(e);
		if (pimpl()->GetPressDurationMS(e.time_now) > MAGIC_INSTANT_DURATION)
		{
			pimpl()->CheckInstantRelease(BtnEvent::OnRelease);
			pimpl()->ClearKey();
			changeState<NoPress>();
		}
	}
};

DigitalButton::DigitalButton(shared_ptr<DigitalButton::Common> btnCommon, JSMButton &mapping)
  : _id(mapping._id)
{
	initialize(new NoPress(new DigitalButtonImpl(mapping, btnCommon)));
}

DigitalButton::Common::Common(Gamepad::Callback virtualControllerCallback, GamepadMotion *mainMotion)
{
	rightMainMotion = mainMotion;
	chordStack.push_front(ButtonID::NONE); //Always hold mapping none at the end to handle modeshifts and chords
	if (virtual_controller.get() != ControllerScheme::NONE)
	{
		_vigemController.reset(Gamepad::getNew(virtual_controller.get(), virtualControllerCallback));
	}
}
