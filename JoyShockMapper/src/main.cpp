#include "JoyShockMapper.h"
#include "JSMVersion.h"
#include "DigitalButton.h"
#include "InputHelpers.h"
#include "Whitelister.h"
#include "TrayIcon.h"
#include "JSMAssignment.hpp"
#include "Gamepad.h"
#include "AutoLoad.h"
#include "AutoConnect.h"
#include "SettingsManager.h"
#include "JoyShock.h"
#include <filesystem>
#define _USE_MATH_DEFINES
#include <math.h> // M_PI

#ifdef _WIN32
#include <shellapi.h>
#else
#define UCHAR unsigned char
#include <algorithm>
#include <unistd.h>
#endif

#pragma warning(disable : 4996) // Disable deprecated API warnings

std::string NONAME;
shared_ptr<JslWrapper> jsl;
unique_ptr<TrayIcon> tray;
unique_ptr<Whitelister> whitelister;

vector<JSMButton> grid_mappings; // array of virtual _buttons on the touchpad grid
vector<JSMButton> mappings;      // array enables use of for each loop and other i/f

float os_mouse_speed = 1.0;
float last_flick_and_rotation = 0.0;
unique_ptr<PollingThread> autoLoadThread;
unique_ptr<JSM::AutoConnect> autoConnectThread;
unique_ptr<PollingThread> minimizeThread;
bool devicesCalibrating = false;
unordered_map<int, shared_ptr<JoyShock>> handle_to_joyshock;

int input_pipe_fd[2];
int triggerCalibrationStep = 0;

struct TOUCH_POINT
{
	TOUCH_POINT() = default;
	TOUCH_POINT(optional<FloatXY> newState, optional<FloatXY> prevState, FloatXY tpSize)
	{
		if (newState)
		{
			posX = newState->x(); // Absolute position in percentage
			posY = newState->y();
			if (prevState)
			{
				movX = int16_t((newState->x() - prevState->x()) * tpSize.x()); // Relative movement in unit
				movY = int16_t((newState->y() - prevState->y()) * tpSize.y());
			}
		}
	}
	float posX = -1.f;
	float posY = -1.f;
	short movX = 0;
	short movY = 0;
	inline bool isDown()
	{
		return posX >= 0.f && posX <= 1.f && posY >= 0.f && posY <= 1.f;
	}
};
// void DisplayTouchInfo(int id, optional<FloatXY> xy, optional<FloatXY> prevXY = nullopt)
//{
//	if (xy)
//	{
//		if (!prevXY)
//		{
//			cout << "New touch " << id << " at " << *xy << '\n';
//		}
//		else if (fabsf(xy->x() - prevXY->x()) > FLT_EPSILON || fabsf(xy->y() - prevXY->y()) > FLT_EPSILON)
//		{
//			cout << "Touch " << id << " moved to " << *xy << '\n';
//		}
//	}
//	else if (prevXY)
//	{
//		cout << "Touch " << id << " has been released\n";
//	}
// }

void touchCallback(int jcHandle, TOUCH_STATE newState, TOUCH_STATE prevState, float delta_time)
{

	// if (current.t0Down || previous.t0Down)
	//{
	//	DisplayTouchInfo(newState.t0Down ? newState.t0Id : prevState.t0Id,
	//	  newState.t0Down ? optional<FloatXY>({ newState.t0X, newState.t0Y }) : nullopt,
	//	  prevState.t0Down ? optional<FloatXY>({ prevState.t0X, prevState.t0Y }) : nullopt);
	//}

	//if (newState.t1Down || prevState.t1Down)
	//{
	//	DisplayTouchInfo(newState.t1Down ? newState.t1Id : prevState.t1Id,
	//	  newState.t1Down ? optional<FloatXY>({ newState.t1X, newState.t1Y }) : nullopt,
	//	  prevState.t1Down ? optional<FloatXY>({ prevState.t1X, prevState.t1Y }) : nullopt);
	//}

	shared_ptr<JoyShock> js = handle_to_joyshock[jcHandle];
	int tpSizeX, tpSizeY;
	if (!js || jsl->GetTouchpadDimension(jcHandle, tpSizeX, tpSizeY) == false)
		return;
	FloatXY tpSize{ float(tpSizeX), float(tpSizeY) };

	lock_guard guard(js->_context->callback_lock);

	TOUCH_POINT point0(newState.t0Down ? make_optional<FloatXY>(newState.t0X, newState.t0Y) : nullopt,
	  prevState.t0Down ? make_optional<FloatXY>(prevState.t0X, prevState.t0Y) : nullopt, tpSize);

	TOUCH_POINT point1(newState.t1Down ? make_optional<FloatXY>(newState.t1X, newState.t1Y) : nullopt,
	  prevState.t1Down ? make_optional<FloatXY>(prevState.t1X, prevState.t1Y) : nullopt, tpSize);

	auto mode = js->getSetting<TouchpadMode>(SettingID::TOUCHPAD_MODE);
	// js->handleButtonChange(ButtonID::TOUCH, point0.isDown() || point1.isDown()); // This is handled by dual stage "trigger" step
	if (!point0.isDown() && !point1.isDown())
	{

		static const function<bool(ButtonID)> IS_TOUCH_BUTTON = [](ButtonID id)
		{
			return id >= ButtonID::T1;
		};

		for (auto currentlyActive = find_if(js->_context->chordStack.begin(), js->_context->chordStack.end(), IS_TOUCH_BUTTON);
		     currentlyActive != js->_context->chordStack.end();
		     currentlyActive = find_if(js->_context->chordStack.begin(), js->_context->chordStack.end(), IS_TOUCH_BUTTON))
		{
			js->_context->chordStack.erase(currentlyActive);
		}
	}
	if (mode == TouchpadMode::GRID_AND_STICK)
	{
		auto &grid_size = *SettingsManager::getV<FloatXY>(SettingID::GRID_SIZE);
		// Handle grid
		int index0 = -1, index1 = -1;
		if (point0.isDown())
		{
			point0.posY += 1e-6f;
			float row = ceilf(point0.posY * grid_size.value().y()) - 1.f;
			float col = ceilf(point0.posX * grid_size.value().x()) - 1.f;
			// COUT << "I should be in button " << row << " " << col << '\n';
			index0 = int(row * grid_size.value().x() + col);
		}

		if (point1.isDown())
		{
			float row = ceilf(point1.posY * grid_size.value().y()) - 1.f;
			float col = ceilf(point1.posX * grid_size.value().x()) - 1.f;
			// COUT << "I should be in button " << row << " " << col << '\n';
			index1 = int(row * grid_size.value().x() + col);
		}

		for (size_t i = 0; i < grid_mappings.size(); ++i)
		{
			auto optId = magic_enum::enum_cast<ButtonID>(int(FIRST_TOUCH_BUTTON + i));

			// JSM can get touch button callbacks before the grid _buttons are setup at startup. Just skip then.
			if (optId && js->_gridButtons.size() == grid_mappings.size())
				js->handleButtonChange(*optId, i == index0 || i == index1);
		}

		// Handle stick
		js->handleTouchStickChange(js->_touchpads[0], point0.isDown(), point0.movX, point0.movY, delta_time);
		js->handleTouchStickChange(js->_touchpads[1], point1.isDown(), point1.movX, point1.movY, delta_time);
	}
	else if (mode == TouchpadMode::MOUSE)
	{
		// Disable gestures
		// if (point0.isDown() && point1.isDown())
		//{
		// if (prevState.t0Down && prevState.t1Down)
		//{
		//	float x = fabsf(newState.t0X - newState.t1X);
		//	float y = fabsf(newState.t0Y - newState.t1Y);
		//	float angle = atan2f(y, x) / PI * 360;
		//	float dist = sqrt(x * x + y * y);
		//	x = fabsf(prevState.t0X - prevState.t1X);
		//	y = fabsf(prevState.t0Y - prevState.t1Y);
		//	float oldAngle = atan2f(y, x) / PI * 360;
		//	float oldDist = sqrt(x * x + y * y);
		//	if (angle != oldAngle)
		//		DEBUG_LOG << "Angle went from " << oldAngle << " degrees to " << angle << " degress. Diff is " << angle - oldAngle << " degrees. ";
		//	js->_touchScrollX.processScroll(angle - oldAngle, js->getSetting<FloatXY>(SettingID::SCROLL_SENS).x(), js->_timeNow);
		//	if (dist != oldDist)
		//		DEBUG_LOG << "Dist went from " << oldDist << " points to " << dist << " points. Diff is " << dist - oldDist << " points. ";
		//	js->_touchScrollY.processScroll(dist - oldDist, js->getSetting<FloatXY>(SettingID::SCROLL_SENS).y(), js->_timeNow);
		//}
		// else
		//{
		//	js->_touchScrollX.reset(js->_timeNow);
		//	js->_touchScrollY.reset(js->_timeNow);
		//}
		//}
		// else
		//{
		//	js->_touchScrollX.reset(js->_timeNow);
		//	js->_touchScrollY.reset(js->_timeNow);
		//  if (point0.isDown() ^ point1.isDown()) // XOR
		if (point0.isDown() || point1.isDown())
		{
			TOUCH_POINT *downPoint = point0.isDown() ? &point0 : &point1;
			FloatXY sens = js->getSetting<FloatXY>(SettingID::TOUCHPAD_SENS);
			// if(downPoint->movX || downPoint->movY) cout << "Moving the cursor by " << dec << int(downPoint->movX) << " h and " << int(downPoint->movY) << " v\n";
			moveMouse(downPoint->movX * sens.x(), downPoint->movY * sens.y());
			// Ignore second touch point in this mode for now until gestures gets handled here
		}
		//}
	}
	else if (mode == TouchpadMode::PS_TOUCHPAD)
	{
		if (js->hasVirtualController())
		{
			optional<FloatXY> p0 = newState.t0Down ? make_optional<FloatXY>(newState.t0X, newState.t0Y) : nullopt;
			optional<FloatXY> p1 = newState.t1Down ? make_optional<FloatXY>(newState.t1X, newState.t1Y) : nullopt;
			js->_context->_vigemController->setTouchState(p0, p1);
		}
	}
}

void calibrateTriggers(shared_ptr<JoyShock> jc)
{
	if (jsl->GetButtons(jc->_handle) & (1 << JSOFFSET_HOME))
	{
		COUT << "Abandonning calibration\n";
		triggerCalibrationStep = 0;
		return;
	}

	auto rpos = jsl->GetRightTrigger(jc->_handle);
	auto lpos = jsl->GetLeftTrigger(jc->_handle);
	auto tick_time = *SettingsManager::get<float>(SettingID::TICK_TIME);
	static auto &right_trigger_offset = *SettingsManager::getV<int>(SettingID::RIGHT_TRIGGER_OFFSET);
	static auto &right_trigger_range = *SettingsManager::getV<int>(SettingID::RIGHT_TRIGGER_RANGE);
	static auto &left_trigger_offset = *SettingsManager::getV<int>(SettingID::LEFT_TRIGGER_OFFSET);
	static auto &left_trigger_range = *SettingsManager::getV<int>(SettingID::LEFT_TRIGGER_RANGE);
	switch (triggerCalibrationStep)
	{
	case 1:
		COUT << "Softly press on the right trigger until you just feel the resistance.\n";
		COUT << "Then press the dpad down button to proceed, or press HOME to abandon.\n";
		tick_time.set(100.f);
		jc->_rightEffect.mode = AdaptiveTriggerMode::SEGMENT;
		jc->_rightEffect.start = 0;
		jc->_rightEffect.end = 255;
		jc->_rightEffect.force = 255;
		triggerCalibrationStep++;
		break;
	case 2:
		if (jsl->GetButtons(jc->_handle) & (1 << JSOFFSET_DOWN))
		{
			triggerCalibrationStep++;
		}
		break;
	case 3:
		DEBUG_LOG << "trigger pos is at " << int(rpos * 255.f) << " (" << int(rpos * 100.f) << "%) and effect pos is at " << int(jc->_rightEffect.start) << '\n';
		if (int(rpos * 255.f) > 0)
		{
			right_trigger_offset.set(jc->_rightEffect.start);
			tick_time.set(40);
			triggerCalibrationStep++;
		}
		++jc->_rightEffect.start;
		break;
	case 4:
		DEBUG_LOG << "trigger pos is at " << int(rpos * 255.f) << " (" << int(rpos * 100.f) << "%) and effect pos is at " << int(jc->_rightEffect.start) << '\n';
		if (int(rpos * 255.f) > 240)
		{
			tick_time.set(100);
			triggerCalibrationStep++;
		}
		++jc->_rightEffect.start;
		break;
	case 5:
		DEBUG_LOG << "trigger pos is at " << int(rpos * 255.f) << " (" << int(rpos * 100.f) << "%) and effect pos is at " << int(jc->_rightEffect.start) << '\n';
		if (int(rpos * 255.f) == 255)
		{
			triggerCalibrationStep++;
			right_trigger_range.set(int(jc->_rightEffect.start - right_trigger_offset));
		}
		++jc->_rightEffect.start;
		break;
	case 6:
		COUT << "Softly press on the left trigger until you just feel the resistance.\n";
		COUT << "Then press the cross button to proceed, or press HOME to abandon.\n";
		tick_time.set(100);
		jc->_leftEffect.mode = AdaptiveTriggerMode::SEGMENT;
		jc->_leftEffect.start = 0;
		jc->_leftEffect.end = 255;
		jc->_leftEffect.force = 255;
		triggerCalibrationStep++;
		break;
	case 7:
		if (jsl->GetButtons(jc->_handle) & (1 << JSOFFSET_S))
		{
			triggerCalibrationStep++;
		}
		break;
	case 8:
		DEBUG_LOG << "trigger pos is at " << int(lpos * 255.f) << " (" << int(lpos * 100.f) << "%) and effect pos is at " << int(jc->_leftEffect.start) << '\n';
		if (int(lpos * 255.f) > 0)
		{
			left_trigger_offset.set(jc->_leftEffect.start);
			tick_time.set(40);
			triggerCalibrationStep++;
		}
		++jc->_leftEffect.start;
		break;
	case 9:
		DEBUG_LOG << "trigger pos is at " << int(lpos * 255.f) << " (" << int(lpos * 100.f) << "%) and effect pos is at " << int(jc->_leftEffect.start) << '\n';
		if (int(lpos * 255.f) > 240)
		{
			tick_time.set(100);
			triggerCalibrationStep++;
		}
		++jc->_leftEffect.start;
		break;
	case 10:
		DEBUG_LOG << "trigger pos is at " << int(lpos * 255.f) << " (" << int(lpos * 100.f) << "%) and effect pos is at " << int(jc->_leftEffect.start) << '\n';
		if (int(lpos * 255.f) == 255)
		{
			triggerCalibrationStep++;
			left_trigger_range.set(int(jc->_leftEffect.start - left_trigger_offset));
		}
		++jc->_leftEffect.start;
		break;
	case 11:
		COUT << "Your triggers have been successfully calibrated. Add the trigger offset and range values in your OnReset.txt file to have those values set by default.\n";
		COUT_INFO << SettingID::RIGHT_TRIGGER_OFFSET << " = " << right_trigger_offset << '\n';
		COUT_INFO << SettingID::RIGHT_TRIGGER_RANGE << " = " << right_trigger_range << '\n';
		COUT_INFO << SettingID::LEFT_TRIGGER_OFFSET << " = " << left_trigger_offset << '\n';
		COUT_INFO << SettingID::LEFT_TRIGGER_RANGE << " = " << left_trigger_range << '\n';
		triggerCalibrationStep = 0;
		tick_time.reset();
		break;
	}
	jsl->SetTriggerEffect(jc->_handle, jc->_leftEffect, jc->_rightEffect);
}

void joyShockPollCallback(int jcHandle, JOY_SHOCK_STATE state, JOY_SHOCK_STATE lastState, IMU_STATE imuState, IMU_STATE lastImuState, float deltaTime)
{

	shared_ptr<JoyShock> jc = handle_to_joyshock[jcHandle];
	if (jc == nullptr)
		return;
	jc->_context->callback_lock.lock();

	auto timeNow = chrono::steady_clock::now();
	deltaTime = ((float)chrono::duration_cast<chrono::microseconds>(timeNow - jc->_timeNow).count()) / 1000000.0f;
	jc->_timeNow = timeNow;

	if (triggerCalibrationStep)
	{
		calibrateTriggers(jc);
		jc->_context->callback_lock.unlock();
		return;
	}

	MotionIf &motion = *jc->_motion;

	IMU_STATE imu = jsl->GetIMUState(jc->_handle);

	if (SettingsManager::getV<Switch>(SettingID::AUTO_CALIBRATE_GYRO)->value() == Switch::ON)
	{
		motion.SetAutoCalibration(true, 1.2f, 0.015f);
	}
	else
	{
		motion.SetAutoCalibration(false, 0.f, 0.f);
	}
	motion.ProcessMotion(imu.gyroX, imu.gyroY, imu.gyroZ, imu.accelX, imu.accelY, imu.accelZ, deltaTime);

	float inGyroX, inGyroY, inGyroZ;
	motion.GetCalibratedGyro(inGyroX, inGyroY, inGyroZ);

	float inGravX, inGravY, inGravZ;
	motion.GetGravity(inGravX, inGravY, inGravZ);

	float inQuatW, inQuatX, inQuatY, inQuatZ;
	motion.GetOrientation(inQuatW, inQuatX, inQuatY, inQuatZ);

	//// These are for sanity checking sensor fusion against a simple complementary filter:
	// float angle = sqrtf(inGyroX * inGyroX + inGyroY * inGyroY + inGyroZ * inGyroZ) * PI / 180.f * deltaTime;
	Vec normAxis = Vec(-inGyroX, -inGyroY, -inGyroZ).Normalized();
	// Quat reverseRotation = Quat(cosf(angle * 0.5f), normAxis.x, normAxis.y, normAxis.z);
	// reverseRotation.Normalize();
	// jc->_lastGrav *= reverseRotation;
	// Vec newGrav = Vec(-imu.accelX, -imu.accelY, -imu.accelZ);
	// jc->_lastGrav += (newGrav - jc->_lastGrav) * 0.01f;
	//
	// Vec normFancyGrav = Vec(inGravX, inGravY, inGravZ).Normalized();
	// Vec normSimpleGrav = jc->_lastGrav.Normalized();
	//
	// float gravAngleDiff = acosf(normFancyGrav.Dot(normSimpleGrav)) * 180.f / PI;

	// COUT << "Angle diff: " << gravAngleDiff << "\n\tFancy gravity: " << normFancyGrav.x << ", " << normFancyGrav.y << ", " << normFancyGrav.z << "\n\tSimple gravity: " << normSimpleGrav.x << ", " << normSimpleGrav.y << ", " << normSimpleGrav.z << "\n";
	// COUT << "Quat: " << inQuatW << ", " << inQuatX << ", " << inQuatY << ", " << inQuatZ << "\n";

	// inGravX = normSimpleGrav.x;
	// inGravY = normSimpleGrav.y;
	// inGravZ = normSimpleGrav.z;

	// COUT << "DS4 accel: %.4f, %.4f, %.4f\n", imuState.accelX, imuState.accelY, imuState.accelZ);
	// COUT << "\tDS4 gyro: %.4f, %.4f, %.4f\n", imuState.gyroX, imuState.gyroY, imuState.gyroZ);
	// COUT << "\tDS4 quat: %.4f, %.4f, %.4f, %.4f | accel: %.4f, %.4f, %.4f | grav: %.4f, %.4f, %.4f\n",
	//	inQuatW, inQuatX, inQuatY, inQuatZ,
	//	_motion.accelX, _motion.accelY, _motion.accelZ,
	//	inGravvX, inGravY, inGravZ);

	bool blockGyro = false;
	bool lockMouse = false;
	bool leftAny = false;
	bool rightAny = false;
	bool motionAny = false;

	if (jc->set_neutral_quat)
	{
		// _motion stick neutral should be calculated from the gravity vector
		Vec gravDirection = Vec(inGravX, inGravY, inGravZ);
		Vec normalizedGravDirection = gravDirection.Normalized();
		float diffAngle = acosf(clamp(-gravDirection.y, -1.f, 1.f));
		Vec neutralGravAxis = Vec(0.0f, -1.0f, 0.0f).Cross(normalizedGravDirection);
		Quat neutralQuat = Quat(cosf(diffAngle * 0.5f), neutralGravAxis.x, neutralGravAxis.y, neutralGravAxis.z);
		neutralQuat.Normalize();

		jc->neutralQuatW = neutralQuat.w;
		jc->neutralQuatX = neutralQuat.x;
		jc->neutralQuatY = neutralQuat.y;
		jc->neutralQuatZ = neutralQuat.z;
		jc->set_neutral_quat = false;
		COUT << "Neutral orientation for device " << jc->_handle << " set...\n";
	}

	float gyroX = 0.0;
	float gyroY = 0.0;
	GyroSpace gyroSpace = jc->getSetting<GyroSpace>(SettingID::GYRO_SPACE);
	if (gyroSpace == GyroSpace::LOCAL)
	{
		int mouse_x_flag = (int)jc->getSetting<GyroAxisMask>(SettingID::MOUSE_X_FROM_GYRO_AXIS);
		if ((mouse_x_flag & (int)GyroAxisMask::X) > 0)
		{
			gyroX += inGyroX;
		}
		if ((mouse_x_flag & (int)GyroAxisMask::Y) > 0)
		{
			gyroX -= inGyroY;
		}
		if ((mouse_x_flag & (int)GyroAxisMask::Z) > 0)
		{
			gyroX -= inGyroZ;
		}
		int mouse_y_flag = (int)jc->getSetting<GyroAxisMask>(SettingID::MOUSE_Y_FROM_GYRO_AXIS);
		if ((mouse_y_flag & (int)GyroAxisMask::X) > 0)
		{
			gyroY -= inGyroX;
		}
		if ((mouse_y_flag & (int)GyroAxisMask::Y) > 0)
		{
			gyroY += inGyroY;
		}
		if ((mouse_y_flag & (int)GyroAxisMask::Z) > 0)
		{
			gyroY += inGyroZ;
		}
	}
	else
	{
		float gravLength = sqrtf(inGravX * inGravX + inGravY * inGravY + inGravZ * inGravZ);
		float normGravX = 0.f;
		float normGravY = 0.f;
		float normGravZ = 0.f;
		if (gravLength > 0.f)
		{
			float gravNormalizer = 1.f / gravLength;
			normGravX = inGravX * gravNormalizer;
			normGravY = inGravY * gravNormalizer;
			normGravZ = inGravZ * gravNormalizer;
		}

		float flatness = abs(normGravY);
		float upness = abs(normGravZ);
		float sideReduction = clamp((max(flatness, upness) - 0.125f) / 0.125f, 0.f, 1.f);

		if (gyroSpace == GyroSpace::PLAYER_TURN || gyroSpace == GyroSpace::PLAYER_LEAN)
		{
			if (gyroSpace == GyroSpace::PLAYER_TURN)
			{
				// grav dot gyro axis (but only Y (yaw) and Z (roll))
				float worldYaw = normGravY * inGyroY + normGravZ * inGyroZ;
				float worldYawSign = worldYaw < 0.f ? -1.f : 1.f;
				const float yawRelaxFactor = 2.f; // 60 degree buffer
				// const float yawRelaxFactor = 1.41f; // 45 degree buffer
				// const float yawRelaxFactor = 1.15f; // 30 degree buffer
				gyroX += worldYawSign * min(abs(worldYaw) * yawRelaxFactor, sqrtf(inGyroY * inGyroY + inGyroZ * inGyroZ));
			}
			else // PLAYER_LEAN
			{
				// project local pitch axis (X) onto gravity plane
				// super simple since our point is only non-zero in one axis
				float gravDotPitchAxis = normGravX;
				float pitchAxisX = 1.f - normGravX * gravDotPitchAxis;
				float pitchAxisY = -normGravY * gravDotPitchAxis;
				float pitchAxisZ = -normGravZ * gravDotPitchAxis;
				// normalize
				float pitchAxisLengthSquared = pitchAxisX * pitchAxisX + pitchAxisY * pitchAxisY + pitchAxisZ * pitchAxisZ;
				if (pitchAxisLengthSquared > 0.f)
				{
					// world roll axis is cross (yaw, pitch)
					float rollAxisX = pitchAxisY * normGravZ - pitchAxisZ * normGravY;
					float rollAxisY = pitchAxisZ * normGravX - pitchAxisX * normGravZ;
					float rollAxisZ = pitchAxisX * normGravY - pitchAxisY * normGravX;

					// normalize
					float rollAxisLengthSquared = rollAxisX * rollAxisX + rollAxisY * rollAxisY + rollAxisZ * rollAxisZ;
					if (rollAxisLengthSquared > 0.f)
					{
						float rollAxisLength = sqrtf(rollAxisLengthSquared);
						float lengthReciprocal = 1.f / rollAxisLength;
						rollAxisX *= lengthReciprocal;
						rollAxisY *= lengthReciprocal;
						rollAxisZ *= lengthReciprocal;

						float worldRoll = rollAxisY * inGyroY + rollAxisZ * inGyroZ;
						float worldRollSign = worldRoll < 0.f ? -1.f : 1.f;
						// const float rollRelaxFactor = 2.f; // 60 degree buffer
						const float rollRelaxFactor = 1.41f; // 45 degree buffer
						// const float rollRelaxFactor = 1.15f; // 30 degree buffer
						gyroX += worldRollSign * min(abs(worldRoll) * rollRelaxFactor, sqrtf(inGyroY * inGyroY + inGyroZ * inGyroZ));
						gyroX *= sideReduction;
					}
				}
			}

			gyroY -= inGyroX;
		}
		else // WORLD_TURN or WORLD_LEAN
		{
			// grav dot gyro axis
			float worldYaw = normGravX * inGyroX + normGravY * inGyroY + normGravZ * inGyroZ;
			// project local pitch axis (X) onto gravity plane
			// super simple since our point is only non-zero in one axis
			float gravDotPitchAxis = normGravX;
			float pitchAxisX = 1.f - normGravX * gravDotPitchAxis;
			float pitchAxisY = -normGravY * gravDotPitchAxis;
			float pitchAxisZ = -normGravZ * gravDotPitchAxis;
			// normalize
			float pitchAxisLengthSquared = pitchAxisX * pitchAxisX + pitchAxisY * pitchAxisY + pitchAxisZ * pitchAxisZ;
			if (pitchAxisLengthSquared > 0.f)
			{
				float pitchAxisLength = sqrtf(pitchAxisLengthSquared);
				float lengthReciprocal = 1.f / pitchAxisLength;
				pitchAxisX *= lengthReciprocal;
				pitchAxisY *= lengthReciprocal;
				pitchAxisZ *= lengthReciprocal;

				// get global pitch factor (dot)
				gyroY = -(pitchAxisX * inGyroX + pitchAxisY * inGyroY + pitchAxisZ * inGyroZ);
				// by the way, pinch it towards the nonsense limit
				gyroY *= sideReduction;

				if (gyroSpace == GyroSpace::WORLD_LEAN)
				{
					// world roll axis is cross (yaw, pitch)
					float rollAxisX = pitchAxisY * normGravZ - pitchAxisZ * normGravY;
					float rollAxisY = pitchAxisZ * normGravX - pitchAxisX * normGravZ;
					float rollAxisZ = pitchAxisX * normGravY - pitchAxisY * normGravX;

					// normalize
					float rollAxisLengthSquared = rollAxisX * rollAxisX + rollAxisY * rollAxisY + rollAxisZ * rollAxisZ;
					if (rollAxisLengthSquared > 0.f)
					{
						float rollAxisLength = sqrtf(rollAxisLengthSquared);
						lengthReciprocal = 1.f / rollAxisLength;
						rollAxisX *= lengthReciprocal;
						rollAxisY *= lengthReciprocal;
						rollAxisZ *= lengthReciprocal;

						// get global roll factor (dot)
						gyroX = rollAxisX * inGyroX + rollAxisY * inGyroY + rollAxisZ * inGyroZ;
						// by the way, pinch because we rely on a good pitch vector here
						gyroX *= sideReduction;
					}
				}
			}

			if (gyroSpace == GyroSpace::WORLD_TURN)
			{
				gyroX += worldYaw;
			}
		}
	}
	float gyroLength = sqrt(gyroX * gyroX + gyroY * gyroY);
	// do gyro smoothing
	// convert gyro smooth time to number of samples
	auto tick_time = SettingsManager::get<float>(SettingID::TICK_TIME)->value();
	auto numGyroSamples = jc->getSetting(SettingID::GYRO_SMOOTH_TIME) * 1000.f / tick_time;
	if (numGyroSamples < 1)
		numGyroSamples = 1; // need at least 1 sample
	auto threshold = jc->getSetting(SettingID::GYRO_SMOOTH_THRESHOLD);
	jc->getSmoothedGyro(gyroX, gyroY, gyroLength, threshold / 2.0f, threshold, int(numGyroSamples), gyroX, gyroY);
	// COUT << "%d Samples for threshold: %0.4f\n", numGyroSamples, gyro_smooth_threshold * maxSmoothingSamples);

	// now, honour gyro_cutoff_speed
	gyroLength = sqrt(gyroX * gyroX + gyroY * gyroY);
	auto speed = jc->getSetting(SettingID::GYRO_CUTOFF_SPEED);
	auto recovery = jc->getSetting(SettingID::GYRO_CUTOFF_RECOVERY);
	if (recovery > speed)
	{
		// we can use gyro_cutoff_speed
		float gyroIgnoreFactor = (gyroLength - speed) / (recovery - speed);
		if (gyroIgnoreFactor < 1.0f)
		{
			if (gyroIgnoreFactor <= 0.0f)
			{
				gyroX = gyroY = gyroLength = 0.0f;
			}
			else
			{
				gyroX *= gyroIgnoreFactor;
				gyroY *= gyroIgnoreFactor;
				gyroLength *= gyroIgnoreFactor;
			}
		}
	}
	else if (speed > 0.0f && gyroLength < speed)
	{
		// gyro_cutoff_recovery is something weird, so we just do a hard threshold
		gyroX = gyroY = gyroLength = 0.0f;
	}

	// Handle _buttons before GYRO because some of them may affect the value of blockGyro
	auto gyro = jc->getSetting<GyroSettings>(SettingID::GYRO_ON); // same result as getting GYRO_OFF
	switch (gyro.ignore_mode)
	{
	case GyroIgnoreMode::BUTTON:
		blockGyro = gyro.always_off ^ jc->isPressed(gyro.button);
		break;
	case GyroIgnoreMode::LEFT_STICK:
	{
		float leftX = jsl->GetLeftX(jc->_handle);
		float leftY = jsl->GetLeftY(jc->_handle);
		float leftLength = sqrtf(leftX * leftX + leftY * leftY);
		float deadzoneInner = jc->getSetting(SettingID::LEFT_STICK_DEADZONE_INNER);
		float deadzoneOuter = jc->getSetting(SettingID::LEFT_STICK_DEADZONE_OUTER);
		leftAny = false;
		switch (jc->getSetting<StickMode>(SettingID::LEFT_STICK_MODE))
		{
		case StickMode::AIM:
			leftAny = leftLength > deadzoneInner;
			break;
		case StickMode::FLICK:
			leftAny = leftLength > (1.f - deadzoneOuter);
			break;
		case StickMode::LEFT_STICK:
		case StickMode::RIGHT_STICK:
			leftAny = leftLength > deadzoneInner;
			break;
		case StickMode::NO_MOUSE:
		case StickMode::INNER_RING:
		case StickMode::OUTER_RING:
		{
			jc->processDeadZones(leftX, leftY, deadzoneInner, deadzoneOuter);
			float absX = abs(leftX);
			float absY = abs(leftY);
			bool left = leftX < -0.5f * absY;
			bool right = leftX > 0.5f * absY;
			bool down = leftY < -0.5f * absX;
			bool up = leftY > 0.5f * absX;
			leftAny = left || right || up || down;
		}
		break;
		default:
			break;
		}
		blockGyro = (gyro.always_off ^ leftAny);
	}
	break;
	case GyroIgnoreMode::RIGHT_STICK:
	{
		float rightX = jsl->GetRightX(jc->_handle);
		float rightY = jsl->GetRightY(jc->_handle);
		float rightLength = sqrtf(rightX * rightX + rightY * rightY);
		float deadzoneInner = jc->getSetting(SettingID::RIGHT_STICK_DEADZONE_INNER);
		float deadzoneOuter = jc->getSetting(SettingID::RIGHT_STICK_DEADZONE_OUTER);
		rightAny = false;
		switch (jc->getSetting<StickMode>(SettingID::RIGHT_STICK_MODE))
		{
		case StickMode::AIM:
			rightAny = rightLength > deadzoneInner;
			break;
		case StickMode::FLICK:
			rightAny = rightLength > (1.f - deadzoneOuter);
			break;
		case StickMode::LEFT_STICK:
		case StickMode::RIGHT_STICK:
			rightAny = rightLength > deadzoneInner;
			break;
		case StickMode::NO_MOUSE:
		case StickMode::INNER_RING:
		case StickMode::OUTER_RING:
		{
			jc->processDeadZones(rightX, rightY, deadzoneInner, deadzoneOuter);
			float absX = abs(rightX);
			float absY = abs(rightY);
			bool left = rightX < -0.5f * absY;
			bool right = rightX > 0.5f * absY;
			bool down = rightY < -0.5f * absX;
			bool up = rightY > 0.5f * absX;
			rightAny = left || right || up || down;
		}
		break;
		default:
			break;
		}
		blockGyro = (gyro.always_off ^ rightAny);
	}
	break;
	}
	float gyro_x_sign_to_use = jc->getSetting(SettingID::GYRO_AXIS_X);
	float gyro_y_sign_to_use = jc->getSetting(SettingID::GYRO_AXIS_Y);

	bool trackball_x_pressed = false;
	bool trackball_y_pressed = false;

	// Apply gyro modifiers in the queue from oldest to newest (thus giving priority to most recent)
	for (auto pair : jc->_context->gyroActionQueue)
	{
		if (pair.second.code == GYRO_ON_BIND)
			blockGyro = false;
		else if (pair.second.code == GYRO_OFF_BIND)
			blockGyro = true;
		else if (pair.second.code == GYRO_INV_X)
			gyro_x_sign_to_use = jc->getSetting(SettingID::GYRO_AXIS_X) * -1; // Intentionally don't support multiple inversions
		else if (pair.second.code == GYRO_INV_Y)
			gyro_y_sign_to_use = jc->getSetting(SettingID::GYRO_AXIS_Y) * -1; // Intentionally don't support multiple inversions
		else if (pair.second.code == GYRO_INVERT)
		{
			// Intentionally don't support multiple inversions
			gyro_x_sign_to_use = jc->getSetting(SettingID::GYRO_AXIS_X) * -1;
			gyro_y_sign_to_use = jc->getSetting(SettingID::GYRO_AXIS_Y) * -1;
		}
		else if (pair.second.code == GYRO_TRACK_X)
			trackball_x_pressed = true;
		else if (pair.second.code == GYRO_TRACK_Y)
			trackball_y_pressed = true;
		else if (pair.second.code == GYRO_TRACKBALL)
		{
			trackball_x_pressed = true;
			trackball_y_pressed = true;
		}
	}

	float decay = exp2f(-deltaTime * jc->getSetting(SettingID::TRACKBALL_DECAY));
	int maxTrackballSamples = max(1, min(jc->NUM_LAST_GYRO_SAMPLES, (int)(1.f / deltaTime * 0.125f)));

	if (!trackball_x_pressed && !trackball_y_pressed)
	{
		jc->lastGyroAbsX = abs(gyroX);
		jc->lastGyroAbsY = abs(gyroY);
	}

	if (!trackball_x_pressed)
	{
		int gyroSampleIndex = jc->lastGyroIndexX = (jc->lastGyroIndexX + 1) % maxTrackballSamples;
		jc->lastGyroX[gyroSampleIndex] = gyroX;
	}
	else
	{
		float lastGyroX = 0.f;
		for (int gyroAverageIdx = 0; gyroAverageIdx < maxTrackballSamples; gyroAverageIdx++)
		{
			lastGyroX += jc->lastGyroX[gyroAverageIdx];
			jc->lastGyroX[gyroAverageIdx] *= decay;
		}
		lastGyroX /= maxTrackballSamples;
		float lastGyroAbsX = abs(lastGyroX);
		if (lastGyroAbsX > jc->lastGyroAbsX)
		{
			lastGyroX *= jc->lastGyroAbsX / lastGyroAbsX;
		}
		gyroX = lastGyroX;
	}
	if (!trackball_y_pressed)
	{
		int gyroSampleIndex = jc->lastGyroIndexY = (jc->lastGyroIndexY + 1) % maxTrackballSamples;
		jc->lastGyroY[gyroSampleIndex] = gyroY;
	}
	else
	{
		float lastGyroY = 0.f;
		for (int gyroAverageIdx = 0; gyroAverageIdx < maxTrackballSamples; gyroAverageIdx++)
		{
			lastGyroY += jc->lastGyroY[gyroAverageIdx];
			jc->lastGyroY[gyroAverageIdx] *= decay;
		}
		lastGyroY /= maxTrackballSamples;
		float lastGyroAbsY = abs(lastGyroY);
		if (lastGyroAbsY > jc->lastGyroAbsY)
		{
			lastGyroY *= jc->lastGyroAbsY / lastGyroAbsY;
		}
		gyroY = lastGyroY;
	}

	if (blockGyro)
	{
		gyroX = 0;
		gyroY = 0;
	}

	float camSpeedX = 0.0f;
	float camSpeedY = 0.0f;

	float gyroXVelocity = gyroX * gyro_x_sign_to_use;
	float gyroYVelocity = gyroY * gyro_y_sign_to_use;

	pair<float, float> lowSensXY = jc->getSetting<FloatXY>(SettingID::MIN_GYRO_SENS);
	pair<float, float> hiSensXY = jc->getSetting<FloatXY>(SettingID::MAX_GYRO_SENS);

	// apply calibration factor
	// get input velocity
	float magnitude = sqrt(gyroX * gyroX + gyroY * gyroY);
	// COUT << "Gyro mag: " << setprecision(4) << magnitude << '\n';
	// calculate position on minThreshold to maxThreshold scale
	float minThreshold = jc->getSetting(SettingID::MIN_GYRO_THRESHOLD);
	float maxThreshold = jc->getSetting(SettingID::MAX_GYRO_THRESHOLD);
	magnitude -= minThreshold;
	if (magnitude < 0.0f)
		magnitude = 0.0f;
	float denom = maxThreshold - minThreshold;
	float newSensitivity;
	if (denom <= 0.0f)
	{
		newSensitivity =
		  magnitude > 0.0f ? 1.0f : 0.0f; // if min threshold overlaps max threshold, pop up to
		                                  // max lowSens as soon as we're above min threshold
	}
	else
	{
		newSensitivity = magnitude / denom;
	}
	if (newSensitivity > 1.0f)
		newSensitivity = 1.0f;

	// interpolate between low sensitivity and high sensitivity
	gyroXVelocity *= lowSensXY.first * (1.0f - newSensitivity) + hiSensXY.first * newSensitivity;
	gyroYVelocity *= lowSensXY.second * (1.0f - newSensitivity) + hiSensXY.second * newSensitivity;

	jc->gyroXVelocity = gyroXVelocity;
	jc->gyroYVelocity = gyroYVelocity;

	jc->_timeNow = chrono::steady_clock::now();

	// sticks!
	jc->processed_gyro_stick = false;
	ControllerOrientation controllerOrientation = jc->getSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION);
	// account for os mouse speed and convert from radians to degrees because gyro reports in degrees per second
	float mouseCalibrationFactor = 180.0f / M_PI / os_mouse_speed;
	if (jc->_splitType != JS_SPLIT_TYPE_RIGHT)
	{
		// let's do these sticks... don't want to constantly send input, so we need to compare them to last time
		auto axisSign = jc->getSetting<AxisSignPair>(SettingID::LEFT_STICK_AXIS);
		float calX = jsl->GetLeftX(jc->_handle) * float(axisSign.first);
		float calY = jsl->GetLeftY(jc->_handle) * float(axisSign.second);

		jc->processStick(calX, calY, jc->_leftStick, mouseCalibrationFactor, deltaTime, leftAny, lockMouse, camSpeedX, camSpeedY);
		jc->_leftStick.lastX = calX;
		jc->_leftStick.lastY = calY;
	}

	if (jc->_splitType != JS_SPLIT_TYPE_LEFT)
	{
		auto axisSign = jc->getSetting<AxisSignPair>(SettingID::RIGHT_STICK_AXIS);
		float calX = jsl->GetRightX(jc->_handle) * float(axisSign.first);
		float calY = jsl->GetRightY(jc->_handle) * float(axisSign.second);

		jc->processStick(calX, calY, jc->_rightStick, mouseCalibrationFactor, deltaTime, rightAny, lockMouse, camSpeedX, camSpeedY);
		jc->_rightStick.lastX = calX;
		jc->_rightStick.lastY = calY;
	}

	if (jc->_splitType == JS_SPLIT_TYPE_FULL ||
	  (jc->_splitType & (int)jc->getSetting<JoyconMask>(SettingID::JOYCON_MOTION_MASK)) == 0)
	{
		Quat neutralQuat = Quat(jc->neutralQuatW, jc->neutralQuatX, jc->neutralQuatY, jc->neutralQuatZ);
		Vec grav = Vec(inGravX, inGravY, inGravZ) * neutralQuat.Inverse();

		float lastCalX = jc->_motionStick.lastX;
		float lastCalY = jc->_motionStick.lastY;
		// float lastCalX = jc->_motionStick.lastX;
		// float lastCalY = jc->_motionStick.lastY;
		//  use gravity vector deflection
		auto axisSign = jc->getSetting<AxisSignPair>(SettingID::MOTION_STICK_AXIS);
		float calX = grav.x * float(axisSign.first);
		float calY = -grav.z * float(axisSign.second);
		float gravLength2D = sqrtf(grav.x * grav.x + grav.z * grav.z);
		float gravStickDeflection = atan2f(gravLength2D, -grav.y) / M_PI;
		if (gravLength2D > 0)
		{
			calX *= gravStickDeflection / gravLength2D;
			calY *= gravStickDeflection / gravLength2D;
		}

		jc->processStick(calX, calY, jc->_motionStick, mouseCalibrationFactor, deltaTime, motionAny, lockMouse, camSpeedX, camSpeedY);
		jc->_motionStick.lastX = calX;
		jc->_motionStick.lastY = calY;

		float gravLength3D = grav.Length();
		if (gravLength3D > 0)
		{
			float gravSideDir;
			switch (controllerOrientation)
			{
			case ControllerOrientation::FORWARD:
				gravSideDir = grav.x;
				break;
			case ControllerOrientation::LEFT:
				gravSideDir = grav.z;
				break;
			case ControllerOrientation::RIGHT:
				gravSideDir = -grav.z;
				break;
			case ControllerOrientation::BACKWARD:
				gravSideDir = -grav.x;
				break;
			}
			float gravDirX = gravSideDir / gravLength3D;
			float sinLeanThreshold = sin(jc->getSetting(SettingID::LEAN_THRESHOLD) * M_PI / 180.f);
			jc->handleButtonChange(ButtonID::LEAN_LEFT, gravDirX < -sinLeanThreshold);
			jc->handleButtonChange(ButtonID::LEAN_RIGHT, gravDirX > sinLeanThreshold);

			// _motion stick can be set to control steering by leaning
			StickMode motionStickMode = jc->getSetting<StickMode>(SettingID::MOTION_STICK_MODE);
			if (jc->_context->_vigemController && (motionStickMode == StickMode::LEFT_STEER_X || motionStickMode == StickMode::RIGHT_STEER_X))
			{
				bool isLeft = motionStickMode == StickMode::LEFT_STEER_X;
				float leanAngle = asinf(clamp(gravDirX, -1.f, 1.f)) * 180.f / M_PI;
				float leanSign = leanAngle < 0.f ? -1.f : 1.f;
				float absLeanAngle = abs(leanAngle);
				if (grav.y > 0.f)
				{
					absLeanAngle = 180.f - absLeanAngle;
				}
				float motionDZInner = jc->getSetting(SettingID::MOTION_DEADZONE_INNER);
				float motionDZOuter = jc->getSetting(SettingID::MOTION_DEADZONE_OUTER);
				float remappedLeanAngle = pow(clamp((absLeanAngle - motionDZInner) / (180.f - motionDZOuter - motionDZInner), 0.f, 1.f), jc->getSetting(SettingID::STICK_POWER));

				// now actually convert to output stick value, taking deadzones and power curve into account
				float undeadzoneInner, undeadzoneOuter, unpower;
				if (isLeft)
				{
					undeadzoneInner = jc->getSetting(SettingID::LEFT_STICK_UNDEADZONE_INNER);
					undeadzoneOuter = jc->getSetting(SettingID::LEFT_STICK_UNDEADZONE_OUTER);
					unpower = jc->getSetting(SettingID::LEFT_STICK_UNPOWER);
				}
				else
				{
					undeadzoneInner = jc->getSetting(SettingID::RIGHT_STICK_UNDEADZONE_INNER);
					undeadzoneOuter = jc->getSetting(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);
					unpower = jc->getSetting(SettingID::RIGHT_STICK_UNPOWER);
				}

				float livezoneSize = 1.f - undeadzoneOuter - undeadzoneInner;
				if (livezoneSize > 0.f)
				{
					// unpower curve
					if (unpower != 0.f)
					{
						remappedLeanAngle = pow(remappedLeanAngle, 1.f / unpower);
					}

					if (remappedLeanAngle < 1.f)
					{
						remappedLeanAngle = undeadzoneInner + remappedLeanAngle * livezoneSize;
					}

					float signedStickValue = leanSign * remappedLeanAngle;
					// COUT << "LEAN ANGLE: " << (leanSign * absLeanAngle) << "    REMAPPED: " << (leanSign * remappedLeanAngle) << "     STICK OUT: " << signedStickValue << '\n';
					jc->_context->_vigemController->setStick(signedStickValue, 0.f, isLeft);
				}
			}
		}
	}

	int buttons = jsl->GetButtons(jc->_handle);
	// button mappings
	if (jc->_splitType != JS_SPLIT_TYPE_RIGHT)
	{
		jc->handleButtonChange(ButtonID::UP, buttons & (1 << JSOFFSET_UP));
		jc->handleButtonChange(ButtonID::DOWN, buttons & (1 << JSOFFSET_DOWN));
		jc->handleButtonChange(ButtonID::LEFT, buttons & (1 << JSOFFSET_LEFT));
		jc->handleButtonChange(ButtonID::RIGHT, buttons & (1 << JSOFFSET_RIGHT));
		jc->handleButtonChange(ButtonID::L, buttons & (1 << JSOFFSET_L));
		jc->handleButtonChange(ButtonID::MINUS, buttons & (1 << JSOFFSET_MINUS));
		jc->handleButtonChange(ButtonID::L3, buttons & (1 << JSOFFSET_LCLICK));

		float lTrigger = jsl->GetLeftTrigger(jc->_handle);
		jc->handleTriggerChange(ButtonID::ZL, ButtonID::ZLF, jc->getSetting<TriggerMode>(SettingID::ZL_MODE), lTrigger, jc->_leftEffect);

		bool touch = jsl->GetTouchDown(jc->_handle, false) || jsl->GetTouchDown(jc->_handle, true);
		switch (jc->_controllerType)
		{
		case JS_TYPE_DS:
			// JSL mapps mic button on the SL index
			// Edge grips
			jc->handleButtonChange(ButtonID::LSL, buttons & (1 << JSOFFSET_SL));
			jc->handleButtonChange(ButtonID::RSR, buttons & (1 << JSOFFSET_SR));
			// Edge FN
			jc->handleButtonChange(ButtonID::LSR, buttons & (1 << JSOFFSET_FNL));
			jc->handleButtonChange(ButtonID::RSL, buttons & (1 << JSOFFSET_FNR));

			jc->handleButtonChange(ButtonID::MIC, buttons & (1 << JSOFFSET_MIC));
			// Don't break but continue onto DS4 stuff too
		case JS_TYPE_DS4:
		{
			float triggerpos = buttons & (1 << JSOFFSET_CAPTURE) ? 1.f :
			  touch                                              ? 0.99f :
			                                                       0.f;
			jc->handleTriggerChange(ButtonID::TOUCH, ButtonID::CAPTURE, jc->getSetting<TriggerMode>(SettingID::TOUCHPAD_DUAL_STAGE_MODE), triggerpos, jc->_unusedEffect);
		}
		break;
		case JS_TYPE_XBOXONE_ELITE:
			jc->handleButtonChange(ButtonID::LSL, buttons & (1 << JSOFFSET_SL)); // Xbox Elite back paddles
			jc->handleButtonChange(ButtonID::RSR, buttons & (1 << JSOFFSET_SR));
			jc->handleButtonChange(ButtonID::LSR, buttons & (1 << JSOFFSET_FNL));
			jc->handleButtonChange(ButtonID::RSL, buttons & (1 << JSOFFSET_FNR));
			break;
		case JS_TYPE_XBOX_SERIES:
			jc->handleButtonChange(ButtonID::CAPTURE, buttons & (1 << JSOFFSET_CAPTURE));
			break;
		default: // Switch Pro controllers and left joycon
		{
			jc->handleButtonChange(ButtonID::CAPTURE, buttons & (1 << JSOFFSET_CAPTURE));
			jc->handleButtonChange(ButtonID::LSL, buttons & (1 << JSOFFSET_SL));
			jc->handleButtonChange(ButtonID::LSR, buttons & (1 << JSOFFSET_SR));
		}
		break;
		}
	}
	else // split type IS right
	{
		// Right joycon bumpers
		jc->handleButtonChange(ButtonID::RSL, buttons & (1 << JSOFFSET_SL));
		jc->handleButtonChange(ButtonID::RSR, buttons & (1 << JSOFFSET_SR));
	}

	if (jc->_splitType != JS_SPLIT_TYPE_LEFT)
	{
		jc->handleButtonChange(ButtonID::E, buttons & (1 << JSOFFSET_E));
		jc->handleButtonChange(ButtonID::S, buttons & (1 << JSOFFSET_S));
		jc->handleButtonChange(ButtonID::N, buttons & (1 << JSOFFSET_N));
		jc->handleButtonChange(ButtonID::W, buttons & (1 << JSOFFSET_W));
		jc->handleButtonChange(ButtonID::R, buttons & (1 << JSOFFSET_R));
		jc->handleButtonChange(ButtonID::PLUS, buttons & (1 << JSOFFSET_PLUS));
		jc->handleButtonChange(ButtonID::HOME, buttons & (1 << JSOFFSET_HOME));
		jc->handleButtonChange(ButtonID::R3, buttons & (1 << JSOFFSET_RCLICK));

		float rTrigger = jsl->GetRightTrigger(jc->_handle);
		jc->handleTriggerChange(ButtonID::ZR, ButtonID::ZRF, jc->getSetting<TriggerMode>(SettingID::ZR_MODE), rTrigger, jc->_rightEffect);
	}
	else
	{
		// Left joycon bumpers
		jc->handleButtonChange(ButtonID::LSL, buttons & (1 << JSOFFSET_SL));
		jc->handleButtonChange(ButtonID::LSR, buttons & (1 << JSOFFSET_SR));
	}

	auto at = jc->getSetting<Switch>(SettingID::ADAPTIVE_TRIGGER);
	if (at == Switch::OFF)
	{
		AdaptiveTriggerSetting none;
		jsl->SetTriggerEffect(jc->_handle, none, none);
	}
	else
	{
		auto leftEffect = jc->getSetting<AdaptiveTriggerSetting>(SettingID::LEFT_TRIGGER_EFFECT);
		auto rightEffect = jc->getSetting<AdaptiveTriggerSetting>(SettingID::RIGHT_TRIGGER_EFFECT);
		jsl->SetTriggerEffect(jc->_handle, leftEffect.mode == AdaptiveTriggerMode::ON ? jc->_leftEffect : leftEffect,
		  rightEffect.mode == AdaptiveTriggerMode::ON ? jc->_rightEffect : rightEffect);
	}

	bool currentMicToggleState = find_if(jc->_context->activeTogglesQueue.cbegin(), jc->_context->activeTogglesQueue.cend(),
	                               [](const auto &pair)
	                               {
		                               return pair.first == ButtonID::MIC;
	                               }) != jc->_context->activeTogglesQueue.cend();
	for (auto controller : handle_to_joyshock)
	{
		jsl->SetMicLight(controller.first, currentMicToggleState ? 1 : 0);
	}

	GyroOutput gyroOutput = jc->getSetting<GyroOutput>(SettingID::GYRO_OUTPUT);
	if (!jc->processed_gyro_stick)
	{
		if (gyroOutput == GyroOutput::LEFT_STICK)
		{
			jc->processGyroStick(0.f, 0.f, 0.f, StickMode::LEFT_STICK, false);
		}
		else if (gyroOutput == GyroOutput::RIGHT_STICK)
		{
			jc->processGyroStick(0.f, 0.f, 0.f, StickMode::RIGHT_STICK, false);
		}
		else if (gyroOutput == GyroOutput::PS_MOTION)
		{
			if (jc->hasVirtualController())
			{
				jc->_context->_vigemController->setGyro(jc->_timeNow, imu.accelX, imu.accelY, imu.accelZ, imu.gyroX, imu.gyroY, imu.gyroZ);
			}
		}
	}

	// optionally ignore the gyro of one of the joycons
	if (!lockMouse && gyroOutput == GyroOutput::MOUSE &&
	  (jc->_splitType == JS_SPLIT_TYPE_FULL ||
	    (jc->_splitType & (int)jc->getSetting<JoyconMask>(SettingID::JOYCON_GYRO_MASK)) == 0))
	{
		// COUT << "GX: %0.4f GY: %0.4f GZ: %0.4f\n", imuState.gyroX, imuState.gyroY, imuState.gyroZ);
		float mouseCalibration = jc->getSetting(SettingID::REAL_WORLD_CALIBRATION) / os_mouse_speed / jc->getSetting(SettingID::IN_GAME_SENS);
		shapedSensitivityMoveMouse(gyroXVelocity * mouseCalibration, gyroYVelocity * mouseCalibration, deltaTime, camSpeedX, -camSpeedY);
	}

	if (jc->_context->_vigemController)
	{
		jc->_context->_vigemController->update(); // Check for initialized built-in
	}
	auto newColor = jc->getSetting<Color>(SettingID::LIGHT_BAR);
	if (jc->_light_bar != newColor)
	{
		jsl->SetLightColour(jc->_handle, newColor.raw);
		jc->_light_bar = newColor;
	}
	if (jc->_context->nn)
	{
		jc->_context->nn = (jc->_context->nn + 1) % 22;
	}
	jc->_context->callback_lock.unlock();
}

void connectDevices(bool mergeJoycons = true)
{
	handle_to_joyshock.clear();
	this_thread::sleep_for(100ms);
	int numConnected = jsl->ConnectDevices();
	vector<int> deviceHandles(numConnected, 0);
	if (numConnected > 0)
	{
		numConnected = jsl->GetConnectedDeviceHandles(&deviceHandles[0], numConnected);

		if (numConnected < deviceHandles.size())
		{
			deviceHandles.erase(remove(deviceHandles.begin(), deviceHandles.end(), -1), deviceHandles.end());
			// deviceHandles.resize(numConnected);
		}

		for (auto handle : deviceHandles) // Don't use foreach!
		{
			auto type = jsl->GetControllerSplitType(handle);
			auto otherJoyCon = find_if(handle_to_joyshock.begin(), handle_to_joyshock.end(),
			  [type](auto &pair)
			  {
				  return type == JS_SPLIT_TYPE_LEFT && pair.second->_splitType == JS_SPLIT_TYPE_RIGHT ||
				    type == JS_SPLIT_TYPE_RIGHT && pair.second->_splitType == JS_SPLIT_TYPE_LEFT;
			  });
			if (mergeJoycons && otherJoyCon != handle_to_joyshock.end())
			{
				// The second JC points to the same common _buttons as the other one.
				COUT << "Found a joycon pair!\n";
				handle_to_joyshock[handle] = make_shared<JoyShock>(handle, type, otherJoyCon->second->_context);
			}
			else
			{
				handle_to_joyshock[handle] = make_shared<JoyShock>(handle, type);
			}
		}
	}

	if (numConnected == 1)
	{
		COUT << "1 device connected\n";
	}
	else if (numConnected == 0)
	{
		CERR << numConnected << " devices connected\n";
	}
	else
	{
		COUT << numConnected << " devices connected\n";
	}
	// if (!IsVisible())
	//{
	//	tray->SendNotification(wstring(msg.begin(), msg.end()));
	// }

	// if (numConnected != 0) {
	//	COUT << "All devices have started continuous gyro calibration\n";
	// }
}

void updateSimPressPartner(ButtonID sim, ButtonID origin, const Mapping &newVal)
{
	JSMButton *button = int(sim) < mappings.size() ? &mappings[int(sim)] :
	  int(sim) - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &grid_mappings[int(sim) - FIRST_TOUCH_BUTTON] :
	                                                      nullptr;
	if (button)
		button->atSimPress(origin)->set(newVal);
	else
		CERR << "Cannot find the button " << sim << '\n';
}

void updateDiagPressPartner(ButtonID diag, ButtonID origin, const Mapping &newVal)
{
	JSMButton *button = int(diag) < mappings.size()         ? &mappings[int(diag)] :
	  int(diag) - FIRST_TOUCH_BUTTON < grid_mappings.size() ? &grid_mappings[int(diag) - FIRST_TOUCH_BUTTON] :
	                                                         nullptr;
	if (button)
		button->atDiagPress(origin)->set(newVal);
	else
		CERR << "Cannot find the button " << diag << '\n';
}

void updateThread(PollingThread *thread, const Switch &newValue)
{
	if (thread)
	{
		if (newValue == Switch::ON)
		{
			thread->Start();
		}
		else if (newValue == Switch::OFF)
		{
			thread->Stop();
		}
	}
	else
	{
		CERR << "The thread does not exist\n";
	}
}

bool do_NO_GYRO_BUTTON()
{
	// TODO: _handle chords
	SettingsManager::get<GyroSettings>(SettingID::GYRO_ON)->reset();
	return true;
}

bool do_RESET_MAPPINGS(CmdRegistry *registry)
{
	COUT << "Resetting all mappings to defaults\n";
	static constexpr auto callReset = [](JSMButton &map)
	{
		map.reset();
	};
	ranges::for_each(mappings, callReset);
	// It is possible some settings were intentionally omitted (JSM_DIRECTORY?, Whitelister?)
	// TODO: make sure omitted settings don't get reset
	SettingsManager::resetAllSettings();
	ranges::for_each(grid_mappings, callReset);

	os_mouse_speed = 1.0f;
	last_flick_and_rotation = 0.0f;
	if (registry)
	{
		if (!registry->loadConfigFile("OnReset.txt"))
		{
			COUT << "There is no ";
			COUT_INFO << "OnReset.txt";
			COUT << " file to load.\n";
		}
	}
	return true;
}

bool do_RECONNECT_CONTROLLERS(string_view arguments, std::function<void()> loadOnReconnect)
{
	static bool mergeJoycons = true;
	if (arguments.compare("MERGE") == 0)
	{
		mergeJoycons = true;
	}
	else if(arguments.compare("SPLIT") == 0)
	{
		mergeJoycons = false;
	}
	else if(!arguments.empty())
	{
		CERR << "Invalid argument: " << arguments << '\n';
		return false;
	}
	// else remember last
	 
	COUT << "Reconnecting controllers: " << (mergeJoycons ? "MERGE" : "SPLIT") << '\n';
	jsl->DisconnectAndDisposeAll();
	connectDevices(mergeJoycons);
	jsl->SetCallback(&joyShockPollCallback);
	jsl->SetTouchCallback(&touchCallback);

	if (loadOnReconnect)
		loadOnReconnect();

	return true;
}

bool do_COUNTER_OS_MOUSE_SPEED()
{
	COUT << "Countering OS mouse speed setting\n";
	os_mouse_speed = getMouseSpeed();
	return true;
}

bool do_IGNORE_OS_MOUSE_SPEED()
{
	COUT << "Ignoring OS mouse speed setting\n";
	os_mouse_speed = 1.0;
	return true;
}

bool do_CALCULATE_REAL_WORLD_CALIBRATION(string_view argument)
{
	// first, check for a parameter
	float numRotations = 1.0;
	if (argument.length() > 0)
	{
		try
		{
			numRotations = stof(string(argument));
		}
		catch (invalid_argument ia)
		{
			COUT << "Can't convert \"" << argument << "\" to a number\n";
			return false;
		}
	}
	if (numRotations == 0)
	{
		COUT << "Can't calculate calibration from zero rotations\n";
	}
	else if (last_flick_and_rotation == 0)
	{
		COUT << "Need to use the flick stick at least once before calculating an appropriate calibration value\n";
	}
	else
	{
		COUT << "Recommendation: REAL_WORLD_CALIBRATION = " << setprecision(5) << (SettingsManager::get<float>(SettingID::REAL_WORLD_CALIBRATION)->value() * last_flick_and_rotation / numRotations) << '\n';
	}
	return true;
}

bool do_FINISH_GYRO_CALIBRATION()
{
	COUT << "Finishing continuous calibration for all devices\n";
	for (auto iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter)
	{
		iter->second->_motion->PauseContinuousCalibration();
	}
	devicesCalibrating = false;
	return true;
}

bool do_RESTART_GYRO_CALIBRATION()
{
	COUT << "Restarting continuous calibration for all devices\n";
	for (auto iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter)
	{
		iter->second->_motion->ResetContinuousCalibration();
		iter->second->_motion->StartContinuousCalibration();
	}
	devicesCalibrating = true;
	return true;
}

bool do_SET_MOTION_STICK_NEUTRAL()
{
	COUT << "Setting neutral motion stick orientation...\n";
	for (auto iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter)
	{
		iter->second->set_neutral_quat = true;
	}
	return true;
}

bool do_SLEEP(string_view argument)
{
	// first, check for a parameter
	float sleepTime = 1.0;
	if (argument.length() > 0)
	{
		try
		{
			sleepTime = stof(argument.data());
		}
		catch (invalid_argument ia)
		{
			COUT << "Can't convert \"" << argument << "\" to a number\n";
			return false;
		}
	}

	if (sleepTime <= 0)
	{
		COUT << "Sleep time must be greater than 0 and less than or equal to 10\n";
		return false;
	}

	if (sleepTime > 10)
	{
		COUT << "Sleep is capped at 10s per command\n";
		sleepTime = 10.f;
	}
	COUT << "Sleeping for " << setprecision(3) << sleepTime << " second(s)...\n";
	this_thread::sleep_for(chrono::milliseconds((int)(sleepTime * 1000)));
	COUT << "Finished sleeping.\n";

	return true;
}

bool do_README()
{
	auto err = ShowOnlineHelp();
	if (err != 0)
	{
		COUT << "Could not open online help. Error #" << err << '\n';
	}
	return true;
}

bool do_WHITELIST_SHOW()
{
	if (whitelister)
	{
		COUT << "Your PID is " << GetCurrentProcessId() << '\n';
		whitelister->ShowConsole();
	}
	return true;
}

bool do_WHITELIST_ADD()
{
	string errMsg = "Whitelister is not implemented";
	if (whitelister && whitelister->Add(&errMsg))
	{
		COUT << "JoyShockMapper was successfully whitelisted\n";
	}
	else
	{
		CERR << "Whitelist operation failed: " << errMsg << '\n';
	}
	return true;
}

bool do_WHITELIST_REMOVE()
{
	string errMsg = "Whitelister is not implemented";
	if (whitelister && whitelister->Remove(&errMsg))
	{
		COUT << "JoyShockMapper removed from whitelist\n";
	}
	else
	{
		CERR << "Whitelist operation failed: " << errMsg << '\n';
	}
	return true;
}

void beforeShowTrayMenu()
{
	if (!tray || !*tray)
		CERR << "ERROR: Cannot create tray item.\n";
	else
	{
		tray->ClearMenuMap();
		tray->AddMenuItem(U("Show Console"), &ShowConsole);
		tray->AddMenuItem(U("Reconnect controllers"), []()
		  { WriteToConsole("RECONNECT_CONTROLLERS"); });
		tray->AddMenuItem(
		  U("AutoLoad"), [](bool isChecked)
		  { SettingsManager::get<Switch>(SettingID::AUTOLOAD)->set(isChecked ? Switch::ON : Switch::OFF); },
		  bind(&PollingThread::isRunning, autoLoadThread.get()));

		tray->AddMenuItem(
		  U("AutoConnect"), [](bool isChecked)
		  { SettingsManager::get<Switch>(SettingID::AUTOCONNECT)->set(isChecked ? Switch::ON : Switch::OFF); },
		  bind(&PollingThread::isRunning, autoConnectThread.get()));

		if (whitelister && whitelister->IsAvailable())
		{
			tray->AddMenuItem(
			  U("Whitelist"), [](bool isChecked)
			  { isChecked ?
				  do_WHITELIST_ADD() :
				  do_WHITELIST_REMOVE(); },
			  bind(&Whitelister::operator bool, whitelister.get()));
		}
		tray->AddMenuItem(
		  U("Calibrate all devices"), [](bool isChecked)
		  { isChecked ?
			  WriteToConsole("RESTART_GYRO_CALIBRATION") :
			  WriteToConsole("FINISH_GYRO_CALIBRATION"); },
		  []()
		  { return devicesCalibrating; });

		string autoloadFolder{ AUTOLOAD_FOLDER() };
		for (auto file : ListDirectory(autoloadFolder.c_str()))
		{
			std::filesystem::path fullPath = std::filesystem::path("AutoLoad") / file;
			string fullPathName = fullPath.string();
			auto noext = file.substr(0, file.find_last_of('.'));
			tray->AddMenuItem(U("AutoLoad folder"), UnicodeString(noext.begin(), noext.end()), [fullPathName]
			  {
				WriteToConsole(string(fullPathName.begin(), fullPathName.end()));
				autoLoadThread->Stop(); });
		}
		string gyroConfigsFolder{ GYRO_CONFIGS_FOLDER() };
		for (auto file : ListDirectory(gyroConfigsFolder.c_str()))
		{
			std::filesystem::path fullPath = std::filesystem::path("GyroConfigs") / file;
			string fullPathName = fullPath.string();
			auto noext = file.substr(0, file.find_last_of('.'));
			tray->AddMenuItem(U("GyroConfigs folder"), UnicodeString(noext.begin(), noext.end()), [fullPathName]
			  {
				WriteToConsole(string(fullPathName.begin(), fullPathName.end()));
				autoLoadThread->Stop(); });
		}
		tray->AddMenuItem(U("Calculate RWC"), []()
		  {
			WriteToConsole("CALCULATE_REAL_WORLD_CALIBRATION");
			ShowConsole(); });
		tray->AddMenuItem(
		  U("Hide when minimized"), [](bool isChecked)
		  {
			  SettingsManager::getV<Switch>(SettingID::HIDE_MINIMIZED)->set(isChecked ? Switch::ON : Switch::OFF);
			  if (!isChecked)
				  UnhideConsole(); },
		  bind(&PollingThread::isRunning, minimizeThread.get()));
		tray->AddMenuItem(U("Quit"), []()
		  { WriteToConsole("QUIT"); });
	}
}

// Perform all cleanup tasks when JSM is exiting
void cleanUp()
{
	if (tray)
	{
		tray->Hide();
	}
	HideConsole();
	jsl->DisconnectAndDisposeAll();
	handle_to_joyshock.clear(); // Destroy Vigem Gamepads
	ReleaseConsole();
}

int filterClampByte(int current, int next)
{
	return max(0, min(0xff, next));
}

float filterClamp01(float current, float next)
{
	return max(0.0f, min(1.0f, next));
}

float filterPositive(float current, float next)
{
	return max(0.0f, next);
}

float filterSign(float current, float next)
{
	return next == -1.0f || next == 0.0f || next == 1.0f ?
	  next :
	  current;
}

template<typename E, E invalid>
E filterInvalidValue(E current, E next)
{
	return next != invalid ? next : current;
}

AdaptiveTriggerSetting filterInvalidValue(AdaptiveTriggerSetting current, AdaptiveTriggerSetting next)
{
	return next.mode != AdaptiveTriggerMode::INVALID ? next : current;
}

float filterFloat(float current, float next)
{
	// Exclude Infinite, NaN and Subnormal
	return fpclassify(next) == FP_NORMAL || fpclassify(next) == FP_ZERO ? next : current;
}

FloatXY filterFloatPair(FloatXY current, FloatXY next)
{
	return (fpclassify(next.x()) == FP_NORMAL || fpclassify(next.x()) == FP_ZERO) &&
	    (fpclassify(next.y()) == FP_NORMAL || fpclassify(next.y()) == FP_ZERO) ?
	  next :
	  current;
}

AxisSignPair filterSignPair(AxisSignPair current, AxisSignPair next)
{
	return next.first != AxisMode::INVALID && next.second != AxisMode::INVALID ?
	  next :
	  current;
}

float filterHoldPressDelay(float c, float next)
{
	auto sim_press_window = SettingsManager::getV<float>(SettingID::SIM_PRESS_WINDOW);
	if (sim_press_window && next <= sim_press_window->value())
	{
		CERR << SettingID::HOLD_PRESS_TIME << " can only be set to a value higher than " << SettingID::SIM_PRESS_WINDOW << " which is " << sim_press_window->value() << "ms.\n";
		return c;
	}
	return next;
}

float filterTickTime(float c, float next)
{
	return max(1.f, min(100.f, round(next)));
}

Mapping filterMapping(Mapping current, Mapping next)
{
	auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER);
	if (next.hasViGEmBtn())
	{
		if (virtual_controller && virtual_controller->value() == ControllerScheme::NONE)
		{
			COUT_WARN << "Before using this mapping, you need to set VIRTUAL_CONTROLLER.\n";
			return current;
		}
		for (auto &js : handle_to_joyshock)
		{
			if (js.second->hasVirtualController() == false)
				return current;
		}
	}
	return next.isValid() ? next : current;
}

TriggerMode filterTriggerMode(TriggerMode current, TriggerMode next)
{
	// With SDL, I'm not sure if we have a reliable way to check if the device has analog or digital triggers. There's a function to query them, but I don't know if it works with the devices with custom readers (Switch, PS)
	/*	for (auto &js : handle_to_joyshock)
	{
	    if (jsl->GetControllerType(js.first) != JS_TYPE_DS4 && next != TriggerMode::NO_FULL)
	    {
	        COUT_WARN << "WARNING: Dual Stage Triggers are only valid on analog triggers. Full pull bindings will be ignored on non DS4 controllers.\n";
	        break;
	    }
	}
*/
	auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER);

	if (next == TriggerMode::X_LT || next == TriggerMode::X_RT)
	{
		if (virtual_controller && virtual_controller->value() == ControllerScheme::NONE)
		{
			COUT_WARN << "Before using this trigger mode, you need to set VIRTUAL_CONTROLLER.\n";
			return current;
		}
		for (auto &js : handle_to_joyshock)
		{
			if (js.second->hasVirtualController() == false)
				return current;
		}
	}
	return filterInvalidValue<TriggerMode, TriggerMode::INVALID>(current, next);
}

TriggerMode filterTouchpadDualStageMode(TriggerMode current, TriggerMode next)
{
	if (next == TriggerMode::X_LT || next == TriggerMode::X_RT || next == TriggerMode::INVALID)
	{
		COUT_WARN << SettingID::TOUCHPAD_DUAL_STAGE_MODE << " doesn't support vigem analog modes.\n";
		return current;
	}
	return next;
}

StickMode filterMotionStickMode(StickMode current, StickMode next)
{
	auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER);
	if (next >= StickMode::LEFT_STICK && next <= StickMode::RIGHT_WIND_X)
	{
		if (virtual_controller && virtual_controller->value() == ControllerScheme::NONE)
		{
			COUT_WARN << "Before using this stick mode, you need to set VIRTUAL_CONTROLLER.\n";
			return current;
		}
		for (auto &js : handle_to_joyshock)
		{
			if (js.second->hasVirtualController() == false)
				return current;
		}
	}
	return filterInvalidValue<StickMode, StickMode::INVALID>(current, next);
}

StickMode filterStickMode(StickMode current, StickMode next)
{
	// these modes are only available to _motion stick
	if (next == StickMode::LEFT_STEER_X || next == StickMode::RIGHT_STEER_X)
	{
		COUT_WARN << "This mode is only available for MOTION_STICK_MODE.\n";
		return current;
	}
	return filterMotionStickMode(current, next);
}

GyroOutput filterGyroOutput(GyroOutput current, GyroOutput next)
{
	auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER);
	if (next == GyroOutput::PS_MOTION && virtual_controller && virtual_controller->value() != ControllerScheme::DS4)
	{
		COUT_WARN << "Before using gyro mode PS_MOTION, you need to set ";
		COUT_INFO << "VIRTUAL_CONTROLLER = DS4\n";
		return current;
	}
	if (next == GyroOutput::LEFT_STICK || next == GyroOutput::RIGHT_STICK)
	{
		if (virtual_controller && virtual_controller->value() == ControllerScheme::NONE)
		{
			COUT_WARN << "Before using this gyro mode, you need to set VIRTUAL_CONTROLLER.\n";
			return current;
		}
		for (auto &js : handle_to_joyshock)
		{
			if (js.second->hasVirtualController() == false)
				return current;
		}
	}
	return filterInvalidValue<GyroOutput, GyroOutput::INVALID>(current, next);
}

void updateRingModeFromStickMode(JSMVariable<RingMode> *stickRingMode, const StickMode &newValue)
{
	if (newValue == StickMode::INNER_RING)
	{
		stickRingMode->set(RingMode::INNER);
	}
	else if (newValue == StickMode::OUTER_RING)
	{
		stickRingMode->set(RingMode::OUTER);
	}
}

ControllerScheme updateVirtualController(ControllerScheme prevScheme, ControllerScheme nextScheme)
{
	string error;
	bool success = true;
	for (auto &js : handle_to_joyshock)
	{
		lock_guard guard(js.second->_context->callback_lock);
		if (!js.second->_context->_vigemController ||
		  js.second->_context->_vigemController->getType() != nextScheme)
		{
			if (nextScheme == ControllerScheme::NONE)
			{
				js.second->_context->_vigemController.reset(nullptr);
			}
			else
			{
				js.second->_context->_vigemController.reset(Gamepad::getNew(nextScheme, bind(&JoyShock::onVirtualControllerNotification, js.second.get(), placeholders::_1, placeholders::_2, placeholders::_3)));
				success &= js.second->_context->_vigemController && js.second->_context->_vigemController->isInitialized(&error);
				if (!error.empty())
				{
					CERR << error << '\n';
					error.clear();
				}
				if (!success)
				{
					js.second->_context->_vigemController.release();
					break;
				}
			}
		}
	}
	return success ? nextScheme : prevScheme;
}

void onVirtualControllerChange(const ControllerScheme &newScheme)
{
	for (auto &js : handle_to_joyshock)
	{
		// Display an error message if any vigem is no good.
		lock_guard guard(js.second->_context->callback_lock);
		if (!js.second->hasVirtualController())
		{
			break;
		}
	}
	// TODO: on NONE clear mappings with vigem commands?
}

void refreshAutoLoadHelp(JSMAssignment<Switch> *autoloadCmd)
{
	stringstream ss;
	ss << "AUTOLOAD will attempt load a file from the following folder when a window with a matching executable name enters focus:\n"
	   << AUTOLOAD_FOLDER();
	autoloadCmd->setHelp(ss.str());
}

void onNewGridDimensions(CmdRegistry *registry, const FloatXY &newGridDims)
{
	_ASSERT_EXPR(registry, U("You forgot to bind the command registry properly!"));
	auto numberOfButtons = size_t(newGridDims.first * newGridDims.second);

	if (numberOfButtons < grid_mappings.size())
	{
		// Remove all extra touch button commands
		bool successfulRemove = true;
		for (auto id = FIRST_TOUCH_BUTTON + numberOfButtons; successfulRemove; ++id)
		{
			string name(magic_enum::enum_name(*magic_enum::enum_cast<ButtonID>(id)));
			successfulRemove = registry->Remove(name);
		}

		// For all joyshocks, remove extra touch DigitalButtons
		for (auto &js : handle_to_joyshock)
		{
			lock_guard guard(js.second->_context->callback_lock);
			js.second->updateGridSize();
		}

		// Remove extra touch button variables
		while (grid_mappings.size() > numberOfButtons)
			grid_mappings.pop_back();
	}
	else if (numberOfButtons > grid_mappings.size())
	{
		// Add new touch button variables and commands
		for (int id = FIRST_TOUCH_BUTTON + int(grid_mappings.size()); grid_mappings.size() < numberOfButtons; ++id)
		{
			JSMButton touchButton(*magic_enum::enum_cast<ButtonID>(id), Mapping::NO_MAPPING);
			touchButton.setFilter(&filterMapping);
			grid_mappings.push_back(touchButton);
			registry->add(new JSMAssignment<Mapping>(grid_mappings.back()));
		}

		// For all joyshocks, remove extra touch DigitalButtons
		for (auto &js : handle_to_joyshock)
		{
			lock_guard guard(js.second->_context->callback_lock);
			js.second->updateGridSize();
		}
	}
	// Else numbers are the same, possibly just reconfigured
}

void onNewStickAxis(AxisMode newAxisMode, bool isVertical)
{
	static auto left_stick_axis = SettingsManager::get<AxisSignPair>(SettingID::LEFT_STICK_AXIS);
	static auto right_stick_axis = SettingsManager::get<AxisSignPair>(SettingID::RIGHT_STICK_AXIS);
	static auto motion_stick_axis = SettingsManager::get<AxisSignPair>(SettingID::MOTION_STICK_AXIS);
	static auto touch_stick_axis = SettingsManager::get<AxisSignPair>(SettingID::TOUCH_STICK_AXIS);
	if (isVertical)
	{
		left_stick_axis->set(AxisSignPair{ left_stick_axis->value().first, newAxisMode });
		right_stick_axis->set(AxisSignPair{ right_stick_axis->value().first, newAxisMode });
		motion_stick_axis->set(AxisSignPair{ motion_stick_axis->value().first, newAxisMode });
		touch_stick_axis->set(AxisSignPair{ touch_stick_axis->value().first, newAxisMode });
	}
	else // is horizontal
	{
		left_stick_axis->set(AxisSignPair{ newAxisMode, left_stick_axis->value().second });
		right_stick_axis->set(AxisSignPair{ newAxisMode, right_stick_axis->value().second });
		motion_stick_axis->set(AxisSignPair{ newAxisMode, motion_stick_axis->value().second });
		touch_stick_axis->set(AxisSignPair{ newAxisMode, touch_stick_axis->value().second });
	}
}

class GyroSensAssignment : public JSMAssignment<FloatXY>
{
public:
	GyroSensAssignment(SettingID id, JSMSetting<FloatXY> &gyroSens)
	  : JSMAssignment(magic_enum::enum_name(id).data(), string(magic_enum::enum_name(gyroSens._id)), gyroSens)
	{
		// min and max gyro sens already have a listener
		gyroSens.removeOnChangeListener(_listenerId);
	}
};

class StickDeadzoneAssignment : public JSMAssignment<float>
{
public:
	StickDeadzoneAssignment(SettingID id, JSMSetting<float> &stickDeadzone)
	  : JSMAssignment(magic_enum::enum_name(id).data(), string(magic_enum::enum_name(stickDeadzone._id)), stickDeadzone)
	{
		// min and max gyro sens already have a listener
		stickDeadzone.removeOnChangeListener(_listenerId);
	}
};

class GyroButtonAssignment : public JSMAssignment<GyroSettings>
{
protected:
	const bool _always_off;
	const ButtonID _chordButton;

	virtual void displayCurrentValue() override
	{
		GyroSettings value(_var);
		if (_chordButton > ButtonID::NONE)
		{
			COUT << _chordButton << ',';
		}
		COUT << (value.always_off ? string("GYRO_ON") : string("GYRO_OFF")) << " = " << value << '\n';
	}

	virtual GyroSettings readValue(stringstream &in) override
	{
		GyroSettings value;
		value.always_off = _always_off; // Added line from defaultParser
		in >> value;
		return value;
	}

	virtual void displayNewValue(const GyroSettings &value) override
	{
		if (_chordButton > ButtonID::NONE)
		{
			COUT << _chordButton << ',';
		}
		COUT << (value.always_off ? string("GYRO_ON") : string("GYRO_OFF")) << " has been set to " << value << '\n';
	}

public:
	GyroButtonAssignment(string_view name, string_view displayName, JSMVariable<GyroSettings> &setting, bool always_off, ButtonID chord = ButtonID::NONE)
	  : JSMAssignment(name, name, setting, true)
	  , _always_off(always_off)
	  , _chordButton(chord)
	{
	}

	GyroButtonAssignment(SettingID id, bool always_off)
	  : GyroButtonAssignment(magic_enum::enum_name(id).data(), magic_enum::enum_name(id).data(), *SettingsManager::get<GyroSettings>(SettingID::GYRO_ON), always_off)
	{
	}

	GyroButtonAssignment *setListener()
	{
		_listenerId = _var.addOnChangeListener(bind(&GyroButtonAssignment::displayNewValue, this, placeholders::_1));
		NONAME.push_back(NONAME[0] ^ 0x05);
		NONAME.push_back(NONAME[1] ^ 14);
		return this;
	}

	virtual unique_ptr<JSMCommand> getModifiedCmd(char op, string_view chord) override
	{
		auto optBtn = magic_enum::enum_cast<ButtonID>(chord);
		auto settingVar = dynamic_cast<JSMSetting<GyroSettings> *>(&_var);
		if (optBtn > ButtonID::NONE && op == ',' && settingVar)
		{
			// Create Modeshift
			string name{ chord };
			(name += op) += _displayName;
			auto chordAssignment = make_unique<GyroButtonAssignment>(_name, name, *settingVar->atChord(*optBtn), _always_off, *optBtn);
			chordAssignment->setListener();
			chordAssignment->setHelp(_help)->setParser(bind(&GyroButtonAssignment::modeshiftParser, *optBtn, settingVar, &_parse, placeholders::_1, placeholders::_2, placeholders::_3))->setTaskOnDestruction(bind(&JSMSetting<GyroSettings>::processModeshiftRemoval, settingVar, *optBtn));
			return chordAssignment;
		}
		return JSMCommand::getModifiedCmd(op, chord);
	}

	virtual ~GyroButtonAssignment()
	{
	}
};

class HelpCmd : public JSMMacro
{
private:
	// HELP runs the macro for each argument given to it.
	string arg; // parsed argument

	bool parser(string_view arguments)
	{
		stringstream ss(arguments.data());
		ss >> arg;
		do
		{ // Run at least once with an empty arg string if there's no argument.
			_macro(this, arguments);
			ss >> arg;
		} while (!ss.fail());
		arg.clear();
		return true;
	}

	// The run function is nothing like the delegate. See how I use the bind function
	// below to hard-code the pointer parameter and the instance pointer 'this'.
	bool runHelp(CmdRegistry *registry)
	{
		if (arg.empty())
		{
			// Show all commands
			COUT << "Here's the list of all commands.\n";
			vector<string_view> list;
			registry->GetCommandList(list);
			for (auto cmd : list)
			{
				COUT_INFO << "    " << cmd << '\n';
			}
			COUT << "Enter HELP [cmd1] [cmd2] ... for details on specific commands.\n";
		}
		else if (registry->hasCommand(arg))
		{
			auto help = registry->GetHelp(arg);
			if (!help.empty())
			{
				COUT << arg << " :\n"
				     << "    " << help << '\n';
			}
			else
			{
				COUT << arg << " is not a recognized command\n";
			}
		}
		else
		{
			// Show all commands that include ARG
			COUT << "\"" << arg << "\" is not a command, but the following are:\n";
			vector<string_view> list;
			registry->GetCommandList(list);
			for (auto cmd : list)
			{
				auto pos = cmd.find(arg);
				if (pos != string::npos)
					COUT_INFO << "    " << cmd << '\n';
			}
			COUT << "Enter HELP [cmd1] [cmd2] ... for details on specific commands.\n";
		}
		return true;
	}

public:
	HelpCmd(CmdRegistry &reg)
	  : JSMMacro("HELP")
	{
		// Bind allows me to use instance function by hardcoding the invisible "this" parameter, and the registry pointer
		SetMacro(bind(&HelpCmd::runHelp, this, &reg));

		// The placeholder parameter says to pass 2nd parameter of call to _parse to the 1st argument of the call to HelpCmd::parser.
		// The first parameter is the command pointer which is not required because parser is an instance function rather than a static one.
		setParser(bind(&HelpCmd::parser, this, ::placeholders::_2));
		NONAME.push_back(NONAME[2] - 1);
		NONAME.push_back(NONAME[1] & ~0x06);
	}
};

// Contains all settings that can be modeshifted. They should be accessed only via Joyshock::getSetting
void initJsmSettings(CmdRegistry *commandRegistry)
{
	auto left_ring_mode = new JSMSetting<RingMode>(SettingID::LEFT_RING_MODE, RingMode::OUTER);
	left_ring_mode->setFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	SettingsManager::add(left_ring_mode);
	commandRegistry->add((new JSMAssignment<RingMode>(*left_ring_mode))
	                       ->setHelp("Pick a ring where to apply the LEFT_RING binding. Valid values are the following: INNER and OUTER."));

	auto left_stick_mode = new JSMSetting<StickMode>(SettingID::LEFT_STICK_MODE, StickMode::NO_MOUSE);
	left_stick_mode->setFilter(&filterStickMode);
	left_stick_mode->addOnChangeListener(bind(&updateRingModeFromStickMode, left_ring_mode, placeholders::_1));
	SettingsManager::add(left_stick_mode);
	commandRegistry->add((new JSMAssignment<StickMode>(*left_stick_mode))
	                       ->setHelp("Set a mouse mode for the left stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING, SCROLL_WHEEL, LEFT_STICK, RIGHT_STICK"));

	auto right_ring_mode = new JSMSetting<RingMode>(SettingID::RIGHT_RING_MODE, RingMode::OUTER);
	right_ring_mode->setFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	SettingsManager::add(right_ring_mode);
	commandRegistry->add((new JSMAssignment<RingMode>(*right_ring_mode))
	                       ->setHelp("Pick a ring where to apply the RIGHT_RING binding. Valid values are the following: INNER and OUTER."));

	auto right_stick_mode = new JSMSetting<StickMode>(SettingID::RIGHT_STICK_MODE, StickMode::NO_MOUSE);
	right_stick_mode->setFilter(&filterStickMode);
	right_stick_mode->addOnChangeListener(bind(&updateRingModeFromStickMode, right_ring_mode, ::placeholders::_1));
	SettingsManager::add(right_stick_mode);
	commandRegistry->add((new JSMAssignment<StickMode>(*right_stick_mode))
	                       ->setHelp("Set a mouse mode for the right stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING LEFT_STICK, RIGHT_STICK"));

	auto motion_ring_mode = new JSMSetting<RingMode>(SettingID::MOTION_RING_MODE, RingMode::OUTER);
	motion_ring_mode->setFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	SettingsManager::add(motion_ring_mode);
	commandRegistry->add((new JSMAssignment<RingMode>(*motion_ring_mode))
	                       ->setHelp("Pick a ring where to apply the MOTION_RING binding. Valid values are the following: INNER and OUTER."));

	auto motion_stick_mode = new JSMSetting<StickMode>(SettingID::MOTION_STICK_MODE, StickMode::NO_MOUSE);
	motion_stick_mode->setFilter(&filterMotionStickMode);
	motion_stick_mode->addOnChangeListener(bind(&updateRingModeFromStickMode, motion_ring_mode, ::placeholders::_1));
	SettingsManager::add(motion_stick_mode);
	commandRegistry->add((new JSMAssignment<StickMode>(*motion_stick_mode))
	                       ->setHelp("Set a mouse mode for the motion-stick -- the whole controller is treated as a stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING LEFT_STICK, RIGHT_STICK"));

	auto mouse_x_from_gyro = new JSMSetting<GyroAxisMask>(SettingID::MOUSE_X_FROM_GYRO_AXIS, GyroAxisMask::Y);
	mouse_x_from_gyro->setFilter(&filterInvalidValue<GyroAxisMask, GyroAxisMask::INVALID>);
	SettingsManager::add(mouse_x_from_gyro);
	commandRegistry->add((new JSMAssignment<GyroAxisMask>(*mouse_x_from_gyro))
	                       ->setHelp("Pick a gyro axis to operate on the mouse's X axis. Valid values are the following: X, Y and Z."));

	auto mouse_y_from_gyro = new JSMSetting<GyroAxisMask>(SettingID::MOUSE_Y_FROM_GYRO_AXIS, GyroAxisMask::X);
	mouse_y_from_gyro->setFilter(&filterInvalidValue<GyroAxisMask, GyroAxisMask::INVALID>);
	SettingsManager::add(mouse_y_from_gyro);
	commandRegistry->add((new JSMAssignment<GyroAxisMask>(*mouse_y_from_gyro))
	                       ->setHelp("Pick a gyro axis to operate on the mouse's Y axis. Valid values are the following: X, Y and Z."));

	auto gyro_settings = new JSMSetting<GyroSettings>(SettingID::GYRO_ON, GyroSettings());
	gyro_settings->setFilter([](GyroSettings current, GyroSettings next)
	  { return next.ignore_mode != GyroIgnoreMode::INVALID ? next : current; });
	SettingsManager::add(gyro_settings);
	commandRegistry->add((new GyroButtonAssignment(SettingID::GYRO_OFF, false))
	                       ->setHelp("Assign a controller button to disable the gyro when pressed."));
	commandRegistry->add((new GyroButtonAssignment(SettingID::GYRO_ON, true))->setListener() // Set only one listener
	                       ->setHelp("Assign a controller button to enable the gyro when pressed."));

	auto joycon_gyro_mask = new JSMSetting<JoyconMask>(SettingID::JOYCON_GYRO_MASK, JoyconMask::IGNORE_LEFT);
	joycon_gyro_mask->setFilter(&filterInvalidValue<JoyconMask, JoyconMask::INVALID>);
	SettingsManager::add(joycon_gyro_mask);
	commandRegistry->add((new JSMAssignment<JoyconMask>(*joycon_gyro_mask))
	                       ->setHelp("When using two Joycons, select which one will be used for gyro. Valid values are the following:\nUSE_BOTH, IGNORE_LEFT, IGNORE_RIGHT, IGNORE_BOTH"));

	auto joycon_motion_mask = new JSMSetting<JoyconMask>(SettingID::JOYCON_MOTION_MASK, JoyconMask::IGNORE_RIGHT);
	joycon_motion_mask->setFilter(&filterInvalidValue<JoyconMask, JoyconMask::INVALID>);
	SettingsManager::add(joycon_motion_mask);
	commandRegistry->add((new JSMAssignment<JoyconMask>(*joycon_motion_mask))
	                       ->setHelp("When using two Joycons, select which one will be used for non-gyro motion. Valid values are the following:\nUSE_BOTH, IGNORE_LEFT, IGNORE_RIGHT, IGNORE_BOTH"));

	auto zlMode = new JSMSetting<TriggerMode>(SettingID::ZL_MODE, TriggerMode::NO_FULL);
	zlMode->setFilter(&filterTriggerMode);
	SettingsManager::add(zlMode);
	commandRegistry->add((new JSMAssignment<TriggerMode>(*zlMode))
	                       ->setHelp("Controllers with a right analog trigger can use one of the following dual stage trigger modes:\nNO_FULL, NO_SKIP, MAY_SKIP, MUST_SKIP, MAY_SKIP_R, MUST_SKIP_R, NO_SKIP_EXCLUSIVE, X_LT, X_RT, PS_L2, PS_R2"));

	auto zrMode = new JSMSetting<TriggerMode>(SettingID::ZR_MODE, TriggerMode::NO_FULL);
	zrMode->setFilter(&filterTriggerMode);
	SettingsManager::add(zrMode);
	commandRegistry->add((new JSMAssignment<TriggerMode>(*zrMode))
	                       ->setHelp("Controllers with a left analog trigger can use one of the following dual stage trigger modes:\nNO_FULL, NO_SKIP, MAY_SKIP, MUST_SKIP, MAY_SKIP_R, MUST_SKIP_R, NO_SKIP_EXCLUSIVE, X_LT, X_RT, PS_L2, PS_R2"));

	auto flick_snap_mode = new JSMSetting<FlickSnapMode>(SettingID::FLICK_SNAP_MODE, FlickSnapMode::NONE);
	flick_snap_mode->setFilter(&filterInvalidValue<FlickSnapMode, FlickSnapMode::INVALID>);
	SettingsManager::add(flick_snap_mode);
	commandRegistry->add((new JSMAssignment<FlickSnapMode>(*flick_snap_mode))
	                       ->setHelp("Snap flicks to cardinal directions. Valid values are the following: NONE or 0, FOUR or 4 and EIGHT or 8."));

	auto min_gyro_sens = new JSMSetting<FloatXY>(SettingID::MIN_GYRO_SENS, { 0.0f, 0.0f });
	min_gyro_sens->setFilter(&filterFloatPair);
	SettingsManager::add(min_gyro_sens);
	commandRegistry->add((new JSMAssignment<FloatXY>(*min_gyro_sens))
	                       ->setHelp("Minimum gyro sensitivity when turning controller at or below MIN_GYRO_THRESHOLD.\nYou can assign a second value as a different vertical sensitivity."));

	auto max_gyro_sens = new JSMSetting<FloatXY>(SettingID::MAX_GYRO_SENS, { 0.0f, 0.0f });
	max_gyro_sens->setFilter(&filterFloatPair);
	SettingsManager::add(max_gyro_sens);
	commandRegistry->add((new JSMAssignment<FloatXY>(*max_gyro_sens))
	                       ->setHelp("Maximum gyro sensitivity when turning controller at or above MAX_GYRO_THRESHOLD.\nYou can assign a second value as a different vertical sensitivity."));

	commandRegistry->add((new GyroSensAssignment(SettingID::GYRO_SENS, *max_gyro_sens))->setHelp(""));
	commandRegistry->add((new GyroSensAssignment(SettingID::GYRO_SENS, *min_gyro_sens))
	                       ->setHelp("Sets a gyro sensitivity to use. This sets both MIN_GYRO_SENS and MAX_GYRO_SENS to the same values. You can assign a second value as a different vertical sensitivity."));

	auto min_gyro_threshold = new JSMSetting<float>(SettingID::MIN_GYRO_THRESHOLD, 0.0f);
	min_gyro_threshold->setFilter(&filterFloat);
	SettingsManager::add(min_gyro_threshold);
	commandRegistry->add((new JSMAssignment<float>(*min_gyro_threshold))
	                       ->setHelp("Degrees per second at and below which to apply minimum gyro sensitivity."));

	auto max_gyro_threshold = new JSMSetting<float>(SettingID::MAX_GYRO_THRESHOLD, 0.0f);
	max_gyro_threshold->setFilter(&filterFloat);
	SettingsManager::add(max_gyro_threshold);
	commandRegistry->add((new JSMAssignment<float>(*max_gyro_threshold))
	                       ->setHelp("Degrees per second at and above which to apply maximum gyro sensitivity."));

	auto stick_power = new JSMSetting<float>(SettingID::STICK_POWER, 1.0f);
	stick_power->setFilter(&filterFloat);
	SettingsManager::add(stick_power);
	commandRegistry->add((new JSMAssignment<float>(*stick_power))
	                       ->setHelp("Power curve for stick input when in AIM mode. 1 for linear, 0 for no curve (full strength once out of deadzone). Higher numbers make more of the stick's range appear like a very slight tilt."));

	auto stick_sens = new JSMSetting<FloatXY>(SettingID::STICK_SENS, { 360.0f, 360.0f });
	stick_sens->setFilter(&filterFloatPair);
	SettingsManager::add(stick_sens);
	commandRegistry->add((new JSMAssignment<FloatXY>(*stick_sens))
	                      ->setHelp("Stick sensitivity when using classic AIM mode."));

	auto real_world_calibration = new JSMSetting<float>(SettingID::REAL_WORLD_CALIBRATION, 40.0f);
	real_world_calibration->setFilter(&filterFloat);
	SettingsManager::add(real_world_calibration);
	commandRegistry->add((new JSMAssignment<float>(*real_world_calibration))
	                       ->setHelp("Calibration value mapping mouse values to in game degrees. This value is used for FLICK mode, and to make GYRO and stick AIM sensitivities use real world values."));

	auto virtual_stick_calibration = new JSMSetting<float>(SettingID::VIRTUAL_STICK_CALIBRATION, 360.0f);
	virtual_stick_calibration->setFilter(&filterFloat);
	SettingsManager::add(virtual_stick_calibration);
	commandRegistry->add((new JSMAssignment<float>(*virtual_stick_calibration))
	                       ->setHelp("With a virtual controller, how fast a full tilt of the stick will turn the controller, in degrees per second. This value is used for FLICK mode with virtual controllers and to make GYRO sensitivities use real world values."));

	auto in_game_sens = new JSMSetting<float>(SettingID::IN_GAME_SENS, 1.0f);
	in_game_sens->setFilter(bind(&fmaxf, 0.0001f, ::placeholders::_2));
	SettingsManager::add(in_game_sens);
	commandRegistry->add((new JSMAssignment<float>(*in_game_sens))
	                       ->setHelp("Set this value to the sensitivity you use in game. It is used by stick FLICK and AIM modes as well as GYRO aiming."));

	auto trigger_threshold = new JSMSetting<float>(SettingID::TRIGGER_THRESHOLD, 0.0f);
	trigger_threshold->setFilter(&filterFloat);
	SettingsManager::add(trigger_threshold);
	commandRegistry->add((new JSMAssignment<float>(*trigger_threshold))
	                       ->setHelp("Set this to a value between 0 and 1. This is the threshold at which a soft press binding is triggered. Or set the value to -1 to use hair trigger mode"));

	auto left_stick_axis = new JSMSetting<AxisSignPair>(SettingID::LEFT_STICK_AXIS, { AxisMode::STANDARD, AxisMode::STANDARD });
	left_stick_axis->setFilter(&filterSignPair);
	SettingsManager::add(left_stick_axis);
	commandRegistry->add((new JSMAssignment<AxisSignPair>(*left_stick_axis))
	                       ->setHelp("When in AIM mode, set stick X axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));

	auto right_stick_axis = new JSMSetting<AxisSignPair>(SettingID::RIGHT_STICK_AXIS, { AxisMode::STANDARD, AxisMode::STANDARD });
	right_stick_axis->setFilter(&filterSignPair);
	SettingsManager::add(right_stick_axis);
	commandRegistry->add((new JSMAssignment<AxisSignPair>(*right_stick_axis))
	                       ->setHelp("When in AIM mode, set stick X axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));

	auto motion_stick_axis = new JSMSetting<AxisSignPair>(SettingID::MOTION_STICK_AXIS, { AxisMode::STANDARD, AxisMode::STANDARD });
	motion_stick_axis->setFilter(&filterSignPair);
	SettingsManager::add(motion_stick_axis);
	commandRegistry->add((new JSMAssignment<AxisSignPair>(*motion_stick_axis))
	                       ->setHelp("When in AIM mode, set stick X axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));

	auto touch_stick_axis = new JSMSetting<AxisSignPair>(SettingID::TOUCH_STICK_AXIS, { AxisMode::STANDARD, AxisMode::STANDARD });
	touch_stick_axis->setFilter(&filterSignPair);
	SettingsManager::add(touch_stick_axis);
	commandRegistry->add((new JSMAssignment<AxisSignPair>(*touch_stick_axis))
	                       ->setHelp("When in AIM mode, set stick X axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));

	// Legacy command
	auto aim_x_sign = new JSMSetting<AxisMode>(SettingID::STICK_AXIS_X, AxisMode::STANDARD);
	aim_x_sign->setFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>)->addOnChangeListener(bind(onNewStickAxis, placeholders::_1, false));
	SettingsManager::add(aim_x_sign);
	commandRegistry->add(new JSMAssignment<AxisMode>(*aim_x_sign, true));

	// Legacy command
	auto aim_y_sign = new JSMSetting<AxisMode>(SettingID::STICK_AXIS_Y, AxisMode::STANDARD);
	aim_y_sign->setFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>)->addOnChangeListener(bind(onNewStickAxis, placeholders::_1, true));
	SettingsManager::add(aim_y_sign);
	commandRegistry->add(new JSMAssignment<AxisMode>(*aim_y_sign, true));

	auto gyro_x_sign = new JSMSetting<AxisMode>(SettingID::GYRO_AXIS_Y, AxisMode::STANDARD);
	gyro_x_sign->setFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>);
	SettingsManager::add(gyro_x_sign);
	commandRegistry->add((new JSMAssignment<AxisMode>(*gyro_x_sign))
	                       ->setHelp("Set gyro X axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));

	auto gyro_y_sign = new JSMSetting<AxisMode>(SettingID::GYRO_AXIS_X, AxisMode::STANDARD);
	gyro_y_sign->setFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>);
	SettingsManager::add(gyro_y_sign);
	commandRegistry->add((new JSMAssignment<AxisMode>(*gyro_y_sign))
	                       ->setHelp("Set gyro Y axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));

	auto flick_time = new JSMSetting<float>(SettingID::FLICK_TIME, 0.1f);
	flick_time->setFilter(bind(&fmaxf, 0.0001f, ::placeholders::_2));
	SettingsManager::add(flick_time);
	commandRegistry->add((new JSMAssignment<float>(*flick_time))
	                       ->setHelp("Sets how long a flick takes in seconds. This value is used by stick FLICK mode."));

	auto flick_time_exponent = new JSMSetting<float>(SettingID::FLICK_TIME_EXPONENT, 0.0f);
	flick_time_exponent->setFilter(&filterFloat);
	SettingsManager::add(flick_time_exponent);
	commandRegistry->add((new JSMAssignment<float>(*flick_time_exponent))
	                       ->setHelp("Applies a delta exponent to flick_time, effectively making flick speed depend on the flick angle: use 0 for no effect and 1 for linear. This value is used by stick FLICK mode."));

	auto gyro_smooth_time = new JSMSetting<float>(SettingID::GYRO_SMOOTH_TIME, 0.125f);
	gyro_smooth_time->setFilter(bind(&fmaxf, 0.0001f, ::placeholders::_2));
	SettingsManager::add(gyro_smooth_time);
	commandRegistry->add((new JSMAssignment<float>(*gyro_smooth_time))
	                       ->setHelp("This length of the smoothing window in seconds. Smoothing is only applied below the GYRO_SMOOTH_THRESHOLD, with a smooth transition to full smoothing."));

	auto gyro_smooth_threshold = new JSMSetting<float>(SettingID::GYRO_SMOOTH_THRESHOLD, 0.0f);
	gyro_smooth_threshold->setFilter(&filterPositive);
	SettingsManager::add(gyro_smooth_threshold);
	commandRegistry->add((new JSMAssignment<float>(*gyro_smooth_threshold))
	                       ->setHelp("When the controller's angular velocity is below this threshold (in degrees per second), smoothing will be applied."));

	auto gyro_cutoff_speed = new JSMSetting<float>(SettingID::GYRO_CUTOFF_SPEED, 0.0f);
	gyro_cutoff_speed->setFilter(&filterPositive);
	SettingsManager::add(gyro_cutoff_speed);
	commandRegistry->add((new JSMAssignment<float>(*gyro_cutoff_speed))
	                       ->setHelp("Gyro deadzone. Gyro input will be ignored when below this angular velocity (in degrees per second). This should be a last-resort stability option."));

	auto gyro_cutoff_recovery = new JSMSetting<float>(SettingID::GYRO_CUTOFF_RECOVERY, 0.0f);
	gyro_cutoff_recovery->setFilter(&filterPositive);
	SettingsManager::add(gyro_cutoff_recovery);
	commandRegistry->add((new JSMAssignment<float>(*gyro_cutoff_recovery))
	                       ->setHelp("Below this threshold (in degrees per second), gyro sensitivity is pushed down towards zero. This can tighten and steady aim without a deadzone."));

	auto stick_acceleration_rate = new JSMSetting<float>(SettingID::STICK_ACCELERATION_RATE, 0.0f);
	stick_acceleration_rate->setFilter(&filterPositive);
	SettingsManager::add(stick_acceleration_rate);
	commandRegistry->add((new JSMAssignment<float>(*stick_acceleration_rate))
	                       ->setHelp("When in AIM mode and the stick is fully tilted, stick sensitivity increases over time. This is a multiplier starting at 1x and increasing this by this value per second."));

	auto stick_acceleration_cap = new JSMSetting<float>(SettingID::STICK_ACCELERATION_CAP, 1000000.0f);
	stick_acceleration_cap->setFilter(bind(&fmaxf, 1.0f, ::placeholders::_2));
	SettingsManager::add(stick_acceleration_cap);
	commandRegistry->add((new JSMAssignment<float>(*stick_acceleration_cap))
	                       ->setHelp("When in AIM mode and the stick is fully tilted, stick sensitivity increases over time. This value is the maximum sensitivity multiplier."));

	auto left_stick_deadzone_inner = new JSMSetting<float>(SettingID::LEFT_STICK_DEADZONE_INNER, 0.15f);
	left_stick_deadzone_inner->setFilter(&filterClamp01);
	SettingsManager::add(left_stick_deadzone_inner);
	commandRegistry->add((new JSMAssignment<float>(*left_stick_deadzone_inner))
	                       ->setHelp("Defines a radius of the left stick within which all values will be ignored. This value can only be between 0 and 1 but it should be small. Stick input out of this radius will be adjusted."));

	auto left_stick_deadzone_outer = new JSMSetting<float>(SettingID::LEFT_STICK_DEADZONE_OUTER, 0.1f);
	left_stick_deadzone_outer->setFilter(&filterClamp01);
	SettingsManager::add(left_stick_deadzone_outer);
	commandRegistry->add((new JSMAssignment<float>(*left_stick_deadzone_outer))
	                       ->setHelp("Defines a distance from the left stick's outer edge for which the stick will be considered fully tilted. This value can only be between 0 and 1 but it should be small. Stick input out of this deadzone will be adjusted."));

	auto flick_deadzone_angle = new JSMSetting<float>(SettingID::FLICK_DEADZONE_ANGLE, 0.0f);
	flick_deadzone_angle->setFilter(&filterPositive);
	SettingsManager::add(flick_deadzone_angle);
	commandRegistry->add((new JSMAssignment<float>(*flick_deadzone_angle))
	                       ->setHelp("Defines a minimum angle (in degrees) for the flick to be considered a flick. Helps ignore unintentional turns when tilting the stick straight forward."));

	auto right_stick_deadzone_inner = new JSMSetting<float>(SettingID::RIGHT_STICK_DEADZONE_INNER, 0.15f);
	right_stick_deadzone_inner->setFilter(&filterClamp01);
	SettingsManager::add(right_stick_deadzone_inner);
	commandRegistry->add((new JSMAssignment<float>(*right_stick_deadzone_inner))
	                       ->setHelp("Defines a radius of the right stick within which all values will be ignored. This value can only be between 0 and 1 but it should be small. Stick input out of this radius will be adjusted."));

	auto right_stick_deadzone_outer = new JSMSetting<float>(SettingID::RIGHT_STICK_DEADZONE_OUTER, 0.1f);
	right_stick_deadzone_outer->setFilter(&filterClamp01);
	SettingsManager::add(right_stick_deadzone_outer);
	commandRegistry->add((new JSMAssignment<float>(*right_stick_deadzone_outer))
	                       ->setHelp("Defines a distance from the right stick's outer edge for which the stick will be considered fully tilted. This value can only be between 0 and 1 but it should be small. Stick input out of this deadzone will be adjusted."));

	commandRegistry->add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_INNER, *right_stick_deadzone_inner))->setHelp(""));
	commandRegistry->add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_INNER, *left_stick_deadzone_inner))->setHelp(""));

	commandRegistry->add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_OUTER, *right_stick_deadzone_outer))->setHelp(""));
	commandRegistry->add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_OUTER, *left_stick_deadzone_outer))
	                       ->setHelp("Defines a distance from both sticks' outer edge for which the stick will be considered fully tilted. This value can only be between 0 and 1 but it should be small. Stick input out of this deadzone will be adjusted."));

	auto motion_deadzone_inner = new JSMSetting<float>(SettingID::MOTION_DEADZONE_INNER, 15.f);
	motion_deadzone_inner->setFilter(&filterPositive);
	SettingsManager::add(motion_deadzone_inner);
	commandRegistry->add((new JSMAssignment<float>(*motion_deadzone_inner))
	                       ->setHelp("Defines a radius of the motion-stick within which all values will be ignored. This value can only be between 0 and 1 but it should be small. Stick input out of this radius will be adjusted."));

	auto motion_deadzone_outer = new JSMSetting<float>(SettingID::MOTION_DEADZONE_OUTER, 135.f);
	motion_deadzone_outer->setFilter(&filterPositive);
	SettingsManager::add(motion_deadzone_outer);
	commandRegistry->add((new JSMAssignment<float>(*motion_deadzone_outer))
	                       ->setHelp("Defines a distance from the motion-stick's outer edge for which the stick will be considered fully tilted. Stick input out of this deadzone will be adjusted."));

	auto angle_to_axis_deadzone_inner = new JSMSetting<float>(SettingID::ANGLE_TO_AXIS_DEADZONE_INNER, 0.f);
	angle_to_axis_deadzone_inner->setFilter(&filterPositive);
	SettingsManager::add(angle_to_axis_deadzone_inner);
	commandRegistry->add((new JSMAssignment<float>(*angle_to_axis_deadzone_inner))
	                       ->setHelp("Defines an angle within which _ANGLE_TO_X and _ANGLE_TO_Y stick modes will be ignored (in degrees). Since a circular deadzone is already used for deciding whether the stick is engaged at all, it's recommended not to use an inner angular deadzone, which is why the default value is 0."));

	auto angle_to_axis_deadzone_outer = new JSMSetting<float>(SettingID::ANGLE_TO_AXIS_DEADZONE_OUTER, 10.f);
	angle_to_axis_deadzone_outer->setFilter(&filterPositive);
	SettingsManager::add(angle_to_axis_deadzone_outer);
	commandRegistry->add((new JSMAssignment<float>(*angle_to_axis_deadzone_outer))
	                       ->setHelp("Defines an angle from max or min rotation that will be treated as max or min rotation, respectively, for _ANGLE_TO_X and _ANGLE_TO_Y stick modes. Since players intending to point the stick perfectly up/down or perfectly left/right will usually be off by a few degrees, this enables players to more easily hit their intended min/max values, so the default value is 10 degrees."));

	auto lean_threshold = new JSMSetting<float>(SettingID::LEAN_THRESHOLD, 15.f);
	lean_threshold->setFilter(&filterPositive);
	SettingsManager::add(lean_threshold);
	commandRegistry->add((new JSMAssignment<float>(*lean_threshold))
	                       ->setHelp("How far the controller must be leaned left or right to trigger a LEAN_LEFT or LEAN_RIGHT binding."));

	auto controller_orientation = new JSMSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION, ControllerOrientation::FORWARD);
	controller_orientation->setFilter(&filterInvalidValue<ControllerOrientation, ControllerOrientation::INVALID>);
	SettingsManager::add(controller_orientation);
	commandRegistry->add((new JSMAssignment<ControllerOrientation>(*controller_orientation))
	                       ->setHelp("Let the stick modes account for how you're holding the controller:\nFORWARD, LEFT, RIGHT, BACKWARD"));

	auto gyro_space = new JSMSetting<GyroSpace>(SettingID::GYRO_SPACE, GyroSpace::LOCAL);
	gyro_space->setFilter(&filterInvalidValue<GyroSpace, GyroSpace::INVALID>);
	SettingsManager::add(gyro_space);
	commandRegistry->add((new JSMAssignment<GyroSpace>(*gyro_space))
	                       ->setHelp("How gyro input is converted to 2D input. With LOCAL, your MOUSE_X_FROM_GYRO_AXIS and MOUSE_Y_FROM_GYRO_AXIS settings decide which local angular axis maps to which 2D mouse axis.\nYour other options are PLAYER_TURN and PLAYER_LEAN. These both take gravity into account to combine your axes more reliably.\n\tUse PLAYER_TURN if you like to turn your camera or move your cursor by turning your controller side to side.\n\tUse PLAYER_LEAN if you'd rather lean your controller to turn the camera."));

	auto trackball_decay = new JSMSetting<float>(SettingID::TRACKBALL_DECAY, 1.0f);
	trackball_decay->setFilter(&filterPositive);
	SettingsManager::add(trackball_decay);
	commandRegistry->add((new JSMAssignment<float>(*trackball_decay))
	                       ->setHelp("Choose the rate at which trackball gyro slows down. 0 means no decay, 1 means it'll halve each second, 2 to halve each 1/2 seconds, etc."));

	auto screen_resolution_x = new JSMSetting<float>(SettingID::SCREEN_RESOLUTION_X, 1920.0f);
	screen_resolution_x->setFilter(&filterPositive);
	SettingsManager::add(screen_resolution_x);
	commandRegistry->add((new JSMAssignment<float>(*screen_resolution_x))
	                       ->setHelp("Indicate your monitor's horizontal resolution when using the stick mode MOUSE_RING."));

	auto screen_resolution_y = new JSMSetting<float>(SettingID::SCREEN_RESOLUTION_Y, 1080.0f);
	screen_resolution_y->setFilter(&filterPositive);
	SettingsManager::add(screen_resolution_y);
	commandRegistry->add((new JSMAssignment<float>(*screen_resolution_y))
	                       ->setHelp("Indicate your monitor's vertical resolution when using the stick mode MOUSE_RING."));

	auto mouse_ring_radius = new JSMSetting<float>(SettingID::MOUSE_RING_RADIUS, 128.0f);
	mouse_ring_radius->setFilter([](float c, float n) -> float
	  { return n <= SettingsManager::get<float>(SettingID::SCREEN_RESOLUTION_Y)->value() ? floorf(n) : c; });
	SettingsManager::add(mouse_ring_radius);
	commandRegistry->add((new JSMAssignment<float>(*mouse_ring_radius))
	                       ->setHelp("Pick a radius on which the cursor will be allowed to move. This value is used for stick mode MOUSE_RING and MOUSE_AREA."));

	auto rotate_smooth_override = new JSMSetting<float>(SettingID::ROTATE_SMOOTH_OVERRIDE, -1.0f);
	// No filtering needed for rotate_smooth_override
	SettingsManager::add(rotate_smooth_override);
	commandRegistry->add((new JSMAssignment<float>(*rotate_smooth_override))
	                       ->setHelp("Some smoothing is applied to flick stick rotations to account for the controller's stick resolution. This value overrides the smoothing threshold."));

	auto flick_snap_strength = new JSMSetting<float>(SettingID::FLICK_SNAP_STRENGTH, 01.0f);
	flick_snap_strength->setFilter(&filterClamp01);
	SettingsManager::add(flick_snap_strength);
	commandRegistry->add((new JSMAssignment<float>(*flick_snap_strength))
	                       ->setHelp("If FLICK_SNAP_MODE is set to something other than NONE, this sets the degree of snapping -- 0 for none, 1 for full snapping to the nearest direction, and values in between will bias you towards the nearest direction instead of snapping."));

	auto trigger_skip_delay = new JSMSetting<float>(SettingID::TRIGGER_SKIP_DELAY, 150.0f);
	trigger_skip_delay->setFilter(&filterPositive);
	SettingsManager::add(trigger_skip_delay);
	commandRegistry->add((new JSMAssignment<float>(*trigger_skip_delay))
	                       ->setHelp("Sets the amount of time in milliseconds within which the user needs to reach the full press to skip the soft pull binding of the trigger."));

	auto turbo_period = new JSMSetting<float>(SettingID::TURBO_PERIOD, 80.0f);
	turbo_period->setFilter(&filterPositive);
	SettingsManager::add(turbo_period);
	commandRegistry->add((new JSMAssignment<float>(*turbo_period))
	                       ->setHelp("Sets the time in milliseconds to wait between each turbo activation."));

	auto hold_press_time = new JSMSetting<float>(SettingID::HOLD_PRESS_TIME, 150.0f);
	hold_press_time->setFilter(&filterHoldPressDelay);
	SettingsManager::add(hold_press_time);
	commandRegistry->add((new JSMAssignment<float>(*hold_press_time))
	                       ->setHelp("Sets the amount of time in milliseconds to hold a button before the hold press is enabled. Releasing the button before this time will trigger the tap press. Turbo press only starts after this delay."));

	auto sim_press_window = new JSMVariable<float>(50.0f);
	sim_press_window->setFilter(&filterPositive);
	SettingsManager::add(SettingID::SIM_PRESS_WINDOW, sim_press_window);
	commandRegistry->add((new JSMAssignment<float>("SIM_PRESS_WINDOW", *sim_press_window))
	                       ->setHelp("Sets the amount of time in milliseconds within which both buttons of a simultaneous press needs to be pressed before enabling the sim press mappings. This setting does not support modeshift."));

	auto dbl_press_window = new JSMSetting<float>(SettingID::DBL_PRESS_WINDOW, 150.0f);
	dbl_press_window->setFilter(&filterPositive);
	SettingsManager::add(dbl_press_window);
	commandRegistry->add((new JSMAssignment<float>("DBL_PRESS_WINDOW", *dbl_press_window))
	                       ->setHelp("Sets the amount of time in milliseconds within which the user needs to press a button twice before enabling the double press mappings. This setting does not support modeshift."));

	auto tick_time = new JSMSetting<float>(SettingID::TICK_TIME, 3);
	tick_time->setFilter(&filterTickTime);
	SettingsManager::add(tick_time);
	commandRegistry->add((new JSMAssignment<float>("TICK_TIME", *tick_time))
	                       ->setHelp("Sets the time in milliseconds that JoyShockMaper waits before reading from each controller again."));

	auto light_bar = new JSMSetting<Color>(SettingID::LIGHT_BAR, 0xFFFFFF);
	// light_bar needs no filter or listener. The callback polls and updates the color.
	SettingsManager::add(light_bar);
	commandRegistry->add((new JSMAssignment<Color>(*light_bar))
	                       ->setHelp("Changes the color bar of the DS4. Either enter as a hex code (xRRGGBB), as three decimal values between 0 and 255 (RRR GGG BBB), or as a common color name in all caps and underscores."));

	auto scroll_sens = new JSMSetting<FloatXY>(SettingID::SCROLL_SENS, { 30.f, 30.f });
	scroll_sens->setFilter(&filterFloatPair);
	SettingsManager::add(scroll_sens);
	commandRegistry->add((new JSMAssignment<FloatXY>(*scroll_sens))
	                       ->setHelp("Scrolling sensitivity for sticks."));

	auto autoloadSwitch = new JSMVariable<Switch>(Switch::ON);
	autoLoadThread.reset(new JSM::AutoLoad(commandRegistry, autoloadSwitch->value() == Switch::ON)); // Start by default
	autoloadSwitch->setFilter(&filterInvalidValue<Switch, Switch::INVALID>)->addOnChangeListener(bind(&updateThread, autoLoadThread.get(), placeholders::_1));
	SettingsManager::add(SettingID::AUTOLOAD, autoloadSwitch);
	auto *autoloadCmd = new JSMAssignment<Switch>("AUTOLOAD", *autoloadSwitch);
	commandRegistry->add(autoloadCmd);

	auto autoConnectSwitch = new JSMVariable<Switch>(Switch::ON);
	autoConnectThread.reset(new JSM::AutoConnect(jsl, autoConnectSwitch->value() == Switch::ON)); // Start by default
	autoConnectSwitch->setFilter(&filterInvalidValue<Switch, Switch::INVALID>)->addOnChangeListener(bind(&updateThread, autoConnectThread.get(), placeholders::_1));
	SettingsManager::add(SettingID::AUTOCONNECT, autoConnectSwitch);
	commandRegistry->add((new JSMAssignment<Switch>("AUTOCONNECT", *autoConnectSwitch))->setHelp("Enable or disable device hotplugging. Valid values are ON and OFF."));

	auto grid_size = new JSMVariable(FloatXY{ 2.f, 1.f });
	grid_size->setFilter([](auto current, auto next)
	  {
		float floorX = floorf(next.x());
		float floorY = floorf(next.y());
		return floorX * floorY >= 1 && floorX * floorY <= 25 ? FloatXY{ floorX, floorY } : current; });
	grid_size->addOnChangeListener(bind(&onNewGridDimensions, commandRegistry, placeholders::_1), true); // Call the listener now
	SettingsManager::add(SettingID::GRID_SIZE, grid_size);
	commandRegistry->add((new JSMAssignment<FloatXY>("GRID_SIZE", *grid_size))
	                       ->setHelp("When TOUCHPAD_MODE is set to GRID_AND_STICK, this variable sets the number of rows and columns in the grid. The product of the two numbers need to be between 1 and 25."));

	auto touchpad_mode = new JSMSetting<TouchpadMode>(SettingID::TOUCHPAD_MODE, TouchpadMode::GRID_AND_STICK);
	touchpad_mode->setFilter(&filterInvalidValue<TouchpadMode, TouchpadMode::INVALID>);
	SettingsManager::add(touchpad_mode);
	commandRegistry->add((new JSMAssignment<TouchpadMode>("TOUCHPAD_MODE", *touchpad_mode))
	                       ->setHelp("Assign a mode to the touchpad. Valid values are GRID_AND_STICK or MOUSE."));

	auto touch_ring_mode = new JSMSetting<RingMode>(SettingID::TOUCH_RING_MODE, RingMode::OUTER);
	touch_ring_mode->setFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	SettingsManager::add(touch_ring_mode);
	commandRegistry->add((new JSMAssignment<RingMode>(*touch_ring_mode))
	                       ->setHelp("Sets the ring mode for the touch stick. Valid values are INNER and OUTER"));

	auto touch_stick_mode = new JSMSetting<StickMode>(SettingID::TOUCH_STICK_MODE, StickMode::NO_MOUSE);
	touch_stick_mode->setFilter(&filterInvalidValue<StickMode, StickMode::INVALID>)->addOnChangeListener(bind(&updateRingModeFromStickMode, touch_ring_mode, ::placeholders::_1));
	SettingsManager::add(touch_stick_mode);
	commandRegistry->add((new JSMAssignment<StickMode>(*touch_stick_mode))
	                       ->setHelp("Set a mouse mode for the touchpad stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING"));

	auto touch_deadzone_inner = new JSMSetting<float>(SettingID::TOUCH_DEADZONE_INNER, 0.3f);
	touch_deadzone_inner->setFilter(&filterPositive);
	SettingsManager::add(touch_deadzone_inner);
	commandRegistry->add((new JSMAssignment<float>(*touch_deadzone_inner))
	                       ->setHelp("Sets the radius of the circle in which a touch stick input sends no output."));

	auto touch_stick_radius = new JSMSetting<float>(SettingID::TOUCH_STICK_RADIUS, 300.f);
	touch_stick_radius->setFilter([](auto current, auto next)
	  { return filterPositive(current, floorf(next)); });
	SettingsManager::add(touch_stick_radius);
	commandRegistry->add((new JSMAssignment<float>(*touch_stick_radius))
	                       ->setHelp("Set the radius of the touchpad stick. The center of the stick is always the first point of contact. Use a very large value (ex: 800) to use it as swipe gesture."));

	auto touchpad_sens = new JSMSetting<FloatXY>(SettingID::TOUCHPAD_SENS, { 1.f, 1.f });
	touchpad_sens->setFilter(filterFloatPair);
	SettingsManager::add(touchpad_sens);
	commandRegistry->add((new JSMAssignment<FloatXY>(*touchpad_sens))
	                       ->setHelp("Changes the sensitivity of the touchpad when set as a mouse. Enter a second value for a different vertical sensitivity."));

	auto hide_minimized = new JSMVariable<Switch>(Switch::OFF);
	minimizeThread.reset(new PollingThread( "Minimize thread", [] (void *param)
		{
			if (isConsoleMinimized())
			{
				HideConsole();
			}
			return true; 
		}, nullptr, 1000, hide_minimized->value() == Switch::ON)); // Start by default
	hide_minimized->setFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	hide_minimized->addOnChangeListener(bind(&updateThread, minimizeThread.get(), placeholders::_1));
	SettingsManager::add(SettingID::HIDE_MINIMIZED, hide_minimized);
	commandRegistry->add((new JSMAssignment<Switch>("HIDE_MINIMIZED", *hide_minimized))
	                       ->setHelp("JSM will be hidden in the notification area when minimized if this setting is ON. Otherwise it stays in the taskbar."));

	auto virtual_controller = new JSMVariable<ControllerScheme>(ControllerScheme::NONE);
	virtual_controller->setFilter(&updateVirtualController);
	virtual_controller->addOnChangeListener(&onVirtualControllerChange);
	SettingsManager::add(SettingID::VIRTUAL_CONTROLLER, virtual_controller);
	commandRegistry->add((new JSMAssignment<ControllerScheme>(magic_enum::enum_name(SettingID::VIRTUAL_CONTROLLER).data(), *virtual_controller))
	                       ->setHelp("Sets the vigem virtual controller type. Can be NONE (default), XBOX (360) or DS4 (PS4)."));

	auto touch_ds_mode = new JSMSetting<TriggerMode>(SettingID::TOUCHPAD_DUAL_STAGE_MODE, TriggerMode::NO_SKIP);
	;
	touch_ds_mode->setFilter(&filterTouchpadDualStageMode);
	SettingsManager::add(touch_ds_mode);
	commandRegistry->add((new JSMAssignment<TriggerMode>(*touch_ds_mode))
	                       ->setHelp("Dual stage mode for the touchpad TOUCH and CAPTURE (i.e. click) bindings."));

	auto rumble_enable = new JSMVariable<Switch>(Switch::ON);
	rumble_enable->setFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	SettingsManager::add(SettingID::RUMBLE, rumble_enable);
	commandRegistry->add((new JSMAssignment<Switch>(magic_enum::enum_name(SettingID::RUMBLE).data(), *rumble_enable))
	                       ->setHelp("Disable the rumbling feature from vigem. Valid values are ON and OFF."));

	auto adaptive_trigger = new JSMSetting<Switch>(SettingID::ADAPTIVE_TRIGGER, Switch::ON);
	adaptive_trigger->setFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	SettingsManager::add(adaptive_trigger);
	commandRegistry->add((new JSMAssignment<Switch>(*adaptive_trigger))
	                       ->setHelp("Control the adaptive trigger feature of the DualSense. Valid values are ON and OFF."));

	auto left_trigger_effect = new JSMSetting<AdaptiveTriggerSetting>(SettingID::LEFT_TRIGGER_EFFECT, AdaptiveTriggerSetting{});
	left_trigger_effect->setFilter(static_cast<AdaptiveTriggerSetting (*)(AdaptiveTriggerSetting, AdaptiveTriggerSetting)>(&filterInvalidValue));
	SettingsManager::add(left_trigger_effect);
	commandRegistry->add((new JSMAssignment<AdaptiveTriggerSetting>(*left_trigger_effect))
	                       ->setHelp("Sets the adaptive trigger effect on the left trigger:\n"
	                                 "OFF: No effect\n"
	                                 "ON: Use effect generated by JSM depending on ZL_MODE\n"
	                                 "RESISTANCE start[0 9] force[0 8]: Some resistance starting at point\n"
	                                 "BOW start[0 8] end[0 8] forceStart[0 8] forceEnd[0 8]: increasingly strong resistance\n"
	                                 "GALLOPING start[0 8] end[0 9] foot1[0 6] foot2[0 7] freq[Hz]: Two pulses repeated periodically\n"
	                                 "SEMI_AUTOMATIC start[2 7] end[0 8] force[0 8]: Trigger effect\n"
	                                 "AUTOMATIC start[0 9] strength[0 8] freq[Hz]: Regular pulse effect\n"
	                                 "MACHINE start[0 9] end[0 9] force1[0 7] force2[0 7] freq[Hz] period: Irregular pulsing"));

	auto right_trigger_effect = new JSMSetting<AdaptiveTriggerSetting>(SettingID::RIGHT_TRIGGER_EFFECT, AdaptiveTriggerSetting{});
	right_trigger_effect->setFilter(static_cast<AdaptiveTriggerSetting (*)(AdaptiveTriggerSetting, AdaptiveTriggerSetting)>(&filterInvalidValue));
	SettingsManager::add(right_trigger_effect);
	commandRegistry->add((new JSMAssignment<AdaptiveTriggerSetting>(*right_trigger_effect))
	                       ->setHelp("Sets the adaptive trigger effect on the right trigger:\n"
	                                 "OFF: No effect\n"
	                                 "ON: Use effect generated by JSM depending on ZR_MODE\n"
	                                 "RESISTANCE start[0 9] force[0 8]: Some resistance starting at point\n"
	                                 "BOW start[0 8] end[0 8] forceStart[0 8] forceEnd[0 8]: increasingly strong resistance\n"
	                                 "GALLOPING start[0 8] end[0 9] foot1[0 6] foot2[0 7] freq[Hz]: Two pulses repeated periodically\n"
	                                 "SEMI_AUTOMATIC start[2 7] end[0 8] force[0 8]: Trigger effect\n"
	                                 "AUTOMATIC start[0 9] strength[0 8] freq[Hz]: Regular pulse effect\n"
	                                 "MACHINE start[0 9] end[0 9] force1[0 7] force2[0 7] freq[Hz] period: Irregular pulsing"));

	auto right_trigger_offset = new JSMVariable<int>(25);
	right_trigger_offset->setFilter(&filterClampByte);
	SettingsManager::add(SettingID::RIGHT_TRIGGER_OFFSET, right_trigger_offset);
	commandRegistry->add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::RIGHT_TRIGGER_OFFSET).data(), *right_trigger_offset)));

	auto left_trigger_offset = new JSMVariable<int>(25);
	left_trigger_offset->setFilter(&filterClampByte);
	SettingsManager::add(SettingID::LEFT_TRIGGER_OFFSET, left_trigger_offset);
	commandRegistry->add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::LEFT_TRIGGER_OFFSET).data(), *left_trigger_offset)));

	auto right_trigger_range = new JSMVariable<int>(150);
	right_trigger_range->setFilter(&filterClampByte);
	SettingsManager::add(SettingID::RIGHT_TRIGGER_RANGE, right_trigger_range);
	commandRegistry->add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::RIGHT_TRIGGER_RANGE).data(), *right_trigger_range)));

	auto left_trigger_range = new JSMVariable<int>(150);
	left_trigger_range->setFilter(&filterClampByte);
	SettingsManager::add(SettingID::LEFT_TRIGGER_RANGE, left_trigger_range);
	commandRegistry->add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::LEFT_TRIGGER_RANGE).data(), *left_trigger_range)));

	auto auto_calibrate_gyro = new JSMVariable<Switch>(Switch::OFF);
	auto_calibrate_gyro->setFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	SettingsManager::add(SettingID::AUTO_CALIBRATE_GYRO, auto_calibrate_gyro);
	commandRegistry->add((new JSMAssignment<Switch>("AUTO_CALIBRATE_GYRO", *auto_calibrate_gyro))
	                       ->setHelp("Gyro calibration happens automatically when this setting is ON. Otherwise you'll need to calibrate the gyro manually when using gyro aiming."));

	auto left_stick_undeadzone_inner = new JSMSetting<float>(SettingID::LEFT_STICK_UNDEADZONE_INNER, 0.f);
	left_stick_undeadzone_inner->setFilter(&filterClamp01);
	SettingsManager::add(left_stick_undeadzone_inner);
	commandRegistry->add((new JSMAssignment<float>(*left_stick_undeadzone_inner))
	                       ->setHelp("When outputting as a virtual controller, account for this much inner deadzone being applied in the target game. This value can only be between 0 and 1 but it should be small."));

	auto left_stick_undeadzone_outer = new JSMSetting<float>(SettingID::LEFT_STICK_UNDEADZONE_OUTER, 0.f);
	left_stick_undeadzone_outer->setFilter(&filterClamp01);
	SettingsManager::add(left_stick_undeadzone_outer);
	commandRegistry->add((new JSMAssignment<float>(*left_stick_undeadzone_outer))
	                       ->setHelp("When outputting as a virtual controller, account for this much outer deadzone being applied in the target game. This value can only be between 0 and 1 but it should be small."));

	auto left_stick_unpower = new JSMSetting<float>(SettingID::LEFT_STICK_UNPOWER, 0.f);
	left_stick_unpower->setFilter(&filterFloat);
	SettingsManager::add(left_stick_unpower);
	commandRegistry->add((new JSMAssignment<float>(*left_stick_unpower))
	                       ->setHelp("When outputting as a virtual controller, account for this power curve being applied in the target game."));

	auto right_stick_undeadzone_inner = new JSMSetting<float>(SettingID::RIGHT_STICK_UNDEADZONE_INNER, 0.f);
	right_stick_undeadzone_inner->setFilter(&filterClamp01);
	SettingsManager::add(right_stick_undeadzone_inner);
	commandRegistry->add((new JSMAssignment<float>(*right_stick_undeadzone_inner))
	                       ->setHelp("When outputting as a virtual controller, account for this much inner deadzone being applied in the target game. This value can only be between 0 and 1 but it should be small."));

	auto right_stick_undeadzone_outer = new JSMSetting<float>(SettingID::RIGHT_STICK_UNDEADZONE_OUTER, 0.f);
	right_stick_undeadzone_outer->setFilter(&filterClamp01);
	SettingsManager::add(right_stick_undeadzone_outer);
	commandRegistry->add((new JSMAssignment<float>(*right_stick_undeadzone_outer))
	                       ->setHelp("When outputting as a virtual controller, account for this much outer deadzone being applied in the target game. This value can only be between 0 and 1 but it should be small."));

	auto right_stick_unpower = new JSMSetting<float>(SettingID::RIGHT_STICK_UNPOWER, 0.f);
	right_stick_unpower->setFilter(&filterFloat);
	SettingsManager::add(right_stick_unpower);
	commandRegistry->add((new JSMAssignment<float>(*right_stick_unpower))
	                       ->setHelp("When outputting as a virtual controller, account for this power curve being applied in the target game."));

	auto left_stick_virtual_scale = new JSMSetting<float>(SettingID::LEFT_STICK_VIRTUAL_SCALE, 1.f);
	left_stick_virtual_scale->setFilter(&filterFloat);
	SettingsManager::add(left_stick_virtual_scale);
	commandRegistry->add((new JSMAssignment<float>(*left_stick_virtual_scale))
	                       ->setHelp("When outputting as a virtual controller, use this to adjust the scale of the left stick output. This does not affect the gyro->stick conversion."));

	auto right_stick_virtual_scale = new JSMSetting<float>(SettingID::RIGHT_STICK_VIRTUAL_SCALE, 1.f);
	right_stick_virtual_scale->setFilter(&filterFloat);
	SettingsManager::add(right_stick_virtual_scale);
	commandRegistry->add((new JSMAssignment<float>(*right_stick_virtual_scale))
	                       ->setHelp("When outputting as a virtual controller, use this to adjust the scale of the right stick output. This does not affect the gyro->stick conversion."));

	auto wind_stick_range = new JSMSetting<float>(SettingID::WIND_STICK_RANGE, 900.f);
	wind_stick_range->setFilter(&filterPositive);
	SettingsManager::add(wind_stick_range);
	commandRegistry->add((new JSMAssignment<float>(*wind_stick_range))
	                       ->setHelp("When using the WIND stick modes, this is how many degrees the stick has to be wound to cover the full range of the ouptut, from minimum value to maximum value."));

	auto wind_stick_power = new JSMSetting<float>(SettingID::WIND_STICK_POWER, 1.f);
	wind_stick_power->setFilter(&filterPositive);
	SettingsManager::add(wind_stick_power);
	commandRegistry->add((new JSMAssignment<float>(*wind_stick_power))
	                       ->setHelp("Power curve for WIND stick modes, letting you have more or less sensitivity towards the neutral position."));

	auto unwind_rate = new JSMSetting<float>(SettingID::UNWIND_RATE, 1800.f);
	unwind_rate->setFilter(&filterPositive);
	SettingsManager::add(unwind_rate);
	commandRegistry->add((new JSMAssignment<float>(*unwind_rate))
	                       ->setHelp("How quickly the WIND sticks unwind on their own when the relevant stick isn't engaged (in degrees per second)."));

	auto gyro_output = new JSMSetting<GyroOutput>(SettingID::GYRO_OUTPUT, GyroOutput::MOUSE);
	gyro_output->setFilter(&filterGyroOutput);
	SettingsManager::add(gyro_output);
	commandRegistry->add((new JSMAssignment<GyroOutput>(*gyro_output))
	                       ->setHelp("Whether gyro should be converted to mouse, left stick, or right stick movement. If you don't want to use gyro aiming, simply leave GYRO_SENS set to 0."));

	auto flick_stick_output = new JSMSetting<GyroOutput>(SettingID::FLICK_STICK_OUTPUT, GyroOutput::MOUSE);
	flick_stick_output->setFilter(&filterInvalidValue<GyroOutput, GyroOutput::INVALID>);
	SettingsManager::add(flick_stick_output);
	commandRegistry->add((new JSMAssignment<GyroOutput>(*flick_stick_output))
	                       ->setHelp("Whether flick stick should be converted to a mouse, left stick, or right stick movement."));

	auto currentWorkingDir = new JSMVariable<PathString>(GetCWD());
	currentWorkingDir->setFilter([](PathString current, PathString next) -> PathString
	  { return SetCWD(string(next)) ? next : current; });
	currentWorkingDir->addOnChangeListener(bind(&refreshAutoLoadHelp, autoloadCmd), true);
	SettingsManager::add(SettingID::JSM_DIRECTORY, currentWorkingDir);
	commandRegistry->add((new JSMAssignment<PathString>("JSM_DIRECTORY", *currentWorkingDir))
	                       ->setHelp("If AUTOLOAD doesn't work properly, set this value to the path to the directory holding the JoyShockMapper.exe file. Make sure a folder named \"AutoLoad\" exists there."));

	auto mouselike_factor = new JSMSetting<FloatXY>(SettingID::MOUSELIKE_FACTOR, {90.f, 90.f});
	mouselike_factor->setFilter(&filterFloatPair);
	SettingsManager::add(SettingID::MOUSELIKE_FACTOR, mouselike_factor);
	commandRegistry->add((new JSMAssignment<FloatXY>(*mouselike_factor))
		->setHelp("Stick sensitivity of the relative movement when in HYBRID_AIM mode. Like the sensitivity of a mouse."));

	auto return_deadzone_is_active = new JSMSetting<Switch>(SettingID::RETURN_DEADZONE_IS_ACTIVE, Switch::ON);
	return_deadzone_is_active->setFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	SettingsManager::add(SettingID::RETURN_DEADZONE_IS_ACTIVE, return_deadzone_is_active);
	commandRegistry->add((new JSMAssignment<Switch>(*return_deadzone_is_active))
		->setHelp("In HYBRID_AIM stick mode, select the mode's behaviour in the deadzone.\n"\
			"This deadzone is determined by the angle of the output from the stick position to the center.\n"
			"It is fully active up to RETURN_DEADZONE_ANGLE and tapers off until RETURN_DEADZONE_CUTOFF_ANGLE.\n"
			"When in DEADZONE_INNER it transitions to an output deadzone based on the distance to the center so the relative part of the input smoothly fades back in."));
	
	auto edge_push_is_active = new JSMSetting<Switch>(SettingID::EDGE_PUSH_IS_ACTIVE, Switch::ON);
	edge_push_is_active->setFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	SettingsManager::add(SettingID::EDGE_PUSH_IS_ACTIVE, edge_push_is_active);
	commandRegistry->add((new JSMAssignment<Switch>(*edge_push_is_active))
	        ->setHelp("In HYBRID_AIM stick mode, enables continuous travelling when the stick is at the edge."));
		

	auto return_deadzone_angle = new JSMSetting<float>(SettingID::RETURN_DEADZONE_ANGLE, 45.f);
	return_deadzone_angle->setFilter([](float c, float n)
	  { return clamp(n, 0.f, 90.f); });
	SettingsManager::add(SettingID::RETURN_DEADZONE_ANGLE, return_deadzone_angle);
	commandRegistry->add((new JSMAssignment<float>(*return_deadzone_angle))
		->setHelp("In HYBRID_AIM stick mode, angle to the center in which the return deadzone is still partially active.\n"\
				  "Valid values range from 0 to 90"));

	auto return_deadzone_cutoff_angle = new JSMSetting<float>(SettingID::RETURN_DEADZONE_ANGLE_CUTOFF, 90.f);
	return_deadzone_cutoff_angle->setFilter(&filterFloat);
	SettingsManager::add(SettingID::RETURN_DEADZONE_ANGLE_CUTOFF, return_deadzone_cutoff_angle);
	commandRegistry->add((new JSMAssignment<float>(*return_deadzone_cutoff_angle))
	    ->setHelp("In HYBRID_AIM stick mode, angle to the center in which the return deadzone is fully active.\n"\
			      "Valid values range from 0 to 90"));
		

}

#ifdef _WIN32
int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR cmdLine, int cmdShow)
{
	auto trayIconData = hInstance;
	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(cmdLine, &argc);
	unsigned long length = 256;
	wstring wmodule(length, '\0');
	auto handle = GetCurrentProcess();
	QueryFullProcessImageNameW(handle, 0, &wmodule[0], &length);
	string module(wmodule.begin(), wmodule.begin() + length);

#else
int main(int argc, char *argv[])
{
#if !defined(_WIN32)
	if (pipe(input_pipe_fd) == -1)
	{
		perror("pipe");
		exit(EXIT_FAILURE);
	}
	if (dup2(input_pipe_fd[0], STDIN_FILENO) == -1)
	{
		perror("dup2");
		exit(EXIT_FAILURE);
	}
#endif
	static_cast<void>(argc);
	static_cast<void>(argv);
	void *trayIconData = nullptr;
	string module(argv[0]);
#endif // _WIN32
	jsl.reset(JslWrapper::getNew());
	whitelister.reset(Whitelister::getNew(false));

	grid_mappings.reserve(int(ButtonID::T25) - FIRST_TOUCH_BUTTON); // This makes sure the items will never get copied and cause crashes
	mappings.reserve(MAPPING_SIZE);
	for (int id = 0; id < MAPPING_SIZE; ++id)
	{
		JSMButton newButton(ButtonID(id), Mapping::NO_MAPPING);
		newButton.setFilter(&filterMapping);
		mappings.push_back(newButton);
	}
	// console
	initConsole();
	#ifndef _WIN32
	// Set up the console to receive commands from the pipe
	// This is only needed on non-Windows platforms
	// The pipe is created in the main function
	// and the console is set up in initConsole()
	// The pipe is used to receive commands from the console
	// to the main thread
	initFifoCommandListener();
	#endif
	COUT_BOLD << "Welcome to JoyShockMapper version " << version << "!\n";
	// if (whitelister) COUT << "JoyShockMapper was successfully whitelisted!\n";
	//  Threads need to be created before listeners
	CmdRegistry commandRegistry;
	initJsmSettings(&commandRegistry);

	for (int i = argc - 1; i >= 0; --i)
	{
#if _WIN32
		string arg(&argv[i][0], &argv[i][wcslen(argv[i])]);
#else
		string arg = string(argv[0]);
#endif
		if (filesystem::is_directory(filesystem::status(arg)) &&
		  SettingsManager::getV<PathString>(SettingID::JSM_DIRECTORY)->set(arg).compare(arg) == 0)
		{
			break;
		}
	}

	if (autoLoadThread && autoLoadThread->isRunning())
	{
		COUT << "AUTOLOAD is available. Files in ";
		COUT_INFO << AUTOLOAD_FOLDER();
		COUT << " folder will get loaded automatically when a matching application is in focus.\n";
	}
	else
	{
		CERR << "AutoLoad is unavailable\n";
	}

	// Add all button mappings as commands
	assert(MAPPING_SIZE == buttonHelpMap.size() && "Please update the button help map in ButtonHelp.cpp");
	for (auto &mapping : mappings)
	{
		commandRegistry.add((new JSMAssignment<Mapping>(mapping.getName(), mapping))->setHelp(buttonHelpMap.at(mapping._id)));
	}
	// SL and SR are shorthand for two different mappings
	commandRegistry.add(new JSMAssignment<Mapping>("SL", "LSL", mappings[(int)ButtonID::LSL], true));
	commandRegistry.add(new JSMAssignment<Mapping>("SL", "RSL", mappings[(int)ButtonID::RSL], true));
	commandRegistry.add(new JSMAssignment<Mapping>("SR", "LSR", mappings[(int)ButtonID::LSR], true));
	commandRegistry.add(new JSMAssignment<Mapping>("SR", "RSR", mappings[(int)ButtonID::RSR], true));

	// Add Macro commands
	commandRegistry.add((new JSMMacro("RESET_MAPPINGS"))->SetMacro(bind(&do_RESET_MAPPINGS, &commandRegistry))->setHelp("Delete all custom bindings and reset to default,\nand run script OnReset.txt in JSM_DIRECTORY."));
	commandRegistry.add((new JSMMacro("NO_GYRO_BUTTON"))->SetMacro(bind(&do_NO_GYRO_BUTTON))->setHelp("Enable gyro at all times, without any GYRO_OFF binding."));
	commandRegistry.add((new JSMMacro("RECONNECT_CONTROLLERS"))->SetMacro(bind(&do_RECONNECT_CONTROLLERS, placeholders::_2, [&commandRegistry]()
		{
			if (!commandRegistry.loadConfigFile("OnReconnect.txt"))
			{
				COUT << "There is no ";
				COUT_INFO << "OnReconnect.txt";
				COUT << " file to load.\n";
			}
		}))->setHelp("Look for newly connected controllers. Specify MERGE (default) or SPLIT whether you want to consider joycons as a single or separate controllers."));
	commandRegistry.add((new JSMMacro("COUNTER_OS_MOUSE_SPEED"))->SetMacro(bind(do_COUNTER_OS_MOUSE_SPEED))->setHelp("JoyShockMapper will load the user's OS mouse sensitivity value to consider it in its calculations."));
	commandRegistry.add((new JSMMacro("IGNORE_OS_MOUSE_SPEED"))->SetMacro(bind(do_IGNORE_OS_MOUSE_SPEED))->setHelp("Disable JoyShockMapper's consideration of the the user's OS mouse sensitivity value."));
	commandRegistry.add((new JSMMacro("CALCULATE_REAL_WORLD_CALIBRATION"))->SetMacro(bind(&do_CALCULATE_REAL_WORLD_CALIBRATION, placeholders::_2))->setHelp("Get JoyShockMapper to recommend you a REAL_WORLD_CALIBRATION value after performing the calibration sequence. Visit GyroWiki for details:\nhttp://gyrowiki.jibbsmart.com/blog:joyshockmapper-guide#calibrating"));
	commandRegistry.add((new JSMMacro("SLEEP"))->SetMacro(bind(&do_SLEEP, placeholders::_2))->setHelp("Sleep for the given number of seconds, or one second if no number is given. Can't sleep more than 10 seconds per command."));
	commandRegistry.add((new JSMMacro("FINISH_GYRO_CALIBRATION"))->SetMacro(bind(&do_FINISH_GYRO_CALIBRATION))->setHelp("Finish calibrating the gyro in all controllers."));
	commandRegistry.add((new JSMMacro("RESTART_GYRO_CALIBRATION"))->SetMacro(bind(&do_RESTART_GYRO_CALIBRATION))->setHelp("Start calibrating the gyro in all controllers."));
	commandRegistry.add((new JSMMacro("SET_MOTION_STICK_NEUTRAL"))->SetMacro(bind(&do_SET_MOTION_STICK_NEUTRAL))->setHelp("Set the neutral orientation for motion stick to whatever the orientation of the controller is."));
	commandRegistry.add((new JSMMacro("README"))->SetMacro(bind(&do_README))->setHelp("Open the latest JoyShockMapper README in your browser."));
	commandRegistry.add((new JSMMacro("WHITELIST_SHOW"))->SetMacro(bind(&do_WHITELIST_SHOW))->setHelp("Open the whitelister application"));
	commandRegistry.add((new JSMMacro("WHITELIST_ADD"))->SetMacro(bind(&do_WHITELIST_ADD))->setHelp("Add JoyShockMapper to the whitelisted applications."));
	commandRegistry.add((new JSMMacro("WHITELIST_REMOVE"))->SetMacro(bind(&do_WHITELIST_REMOVE))->setHelp("Remove JoyShockMapper from whitelisted applications."));
	commandRegistry.add(new HelpCmd(commandRegistry));
	commandRegistry.add((new JSMMacro("CLEAR"))->SetMacro(bind(&ClearConsole))->setHelp("Removes all text in the console screen"));
	commandRegistry.add((new JSMMacro("CALIBRATE_TRIGGERS"))->SetMacro([](JSMMacro *, string_view)
	                                                          {
		                                                        triggerCalibrationStep = 1;
		                                                        return true; })
	                      ->setHelp("Starts the trigger calibration procedure for the dualsense triggers."));
	bool quit = false;
	commandRegistry.add((new JSMMacro("QUIT"))
	                      ->SetMacro([&quit](JSMMacro *, string_view)
	                        {
		                      quit = true;
		                      WriteToConsole(""); // If ran from autoload thread, you need to send RETURN to resume the main loop and check the quit flag.
		                      return true; })
	                      ->setHelp("Close the application."));

	Mapping::_isCommandValid = bind(&CmdRegistry::isCommandValid, &commandRegistry, placeholders::_1);

	connectDevices();
	jsl->SetCallback(&joyShockPollCallback);
	jsl->SetTouchCallback(&touchCallback);
	tray.reset(TrayIcon::getNew(trayIconData, &beforeShowTrayMenu));
	if (tray)
	{
		tray->Show();
	}

	do_RESET_MAPPINGS(&commandRegistry); // OnReset.txt
	if (commandRegistry.loadConfigFile("OnStartup.txt"))
	{
		COUT << "Finished executing startup file.\n";
	}
	else
	{
		COUT << "There is no ";
		COUT_INFO << "OnStartup.txt";
		COUT << " file to load.\n";
	}

	for (int i = 0; i < argc; ++i)
	{
#if _WIN32
		string arg(&argv[i][0], &argv[i][wcslen(argv[i])]);
#else
		string arg = string(argv[0]);
#endif
		if (filesystem::is_regular_file(filesystem::status(arg)) && arg != module)
		{
			commandRegistry.loadConfigFile(arg);
			SettingsManager::getV<Switch>(SettingID::AUTOLOAD)->set(Switch::OFF);
		}
	}
	// The main loop is simple and reads like pseudocode
	string enteredCommand;
	while (!quit)
	{
		#if _WIN32
			getline(cin, enteredCommand);
        #else
			std::unique_lock<std::mutex> lock(commandQueueMutex);
			commandQueueCV.wait(lock, []{ return !commandQueue.empty(); });

			Command cmd = commandQueue.front();
			commandQueue.pop();
			lock.unlock();
			enteredCommand = cmd.text;
        #endif
		

		commandRegistry.processLine(enteredCommand);
	}
#ifdef _WIN32
	LocalFree(argv);
#endif
	cleanUp();
	return 0;
}
