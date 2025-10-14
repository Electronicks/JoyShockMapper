/*
 * MIT License
 *
 * Copyright (c) 2021-2022 John "Nielk1" Klein
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace ExtendInput::DataTools::DualSense
{

using byte = unsigned char;
/// <remarks>
/// Actual effect byte values sent to the controller. More complex effects may be build through the combination of these
/// values and specific paramaters.
/// </remarks>
enum class TriggerEffectType : byte
{
	// Offically recognized modes
	// These are 100% safe and are the only effects that modify the trigger status nybble
	Off = 0x05,       // 00 00 0 101
	Feedback = 0x21,  // 00 10 0 001
	Weapon = 0x25,    // 00 10 0 101
	Vibration = 0x26, // 00 10 0 110

	// Unofficial but unique effects left in the firmware
	// These might be removed in the future
	Bow = 0x22,       // 00 10 0 010
	Galloping = 0x23, // 00 10 0 011
	Machine = 0x27,   // 00 10 0 111

	// Leftover versions of offical modes with simpler logic and no paramater protections
	// These should not be used
	Simple_Feedback = 0x01,  // 00 00 0 001
	Simple_Weapon = 0x02,    // 00 00 0 010
	Simple_Vibration = 0x06, // 00 00 0 110

	// Leftover versions of offical modes with limited paramater ranges
	// These should not be used
	Limited_Feedback = 0x11, // 00 01 0 001
	Limited_Weapon = 0x12,   // 00 01 0 010

	// Debug or Calibration functions
	// Don't use these as they will courrupt the trigger state until the reset button is pressed
	DebugFC = 0xFC, // 11 11 1 100
	DebugFD = 0xFD, // 11 11 1 101
	DebugFE = 0xFE, // 11 11 1 110
};

/**
 * Changelog
 * Revision 1: Initial
 * Revision 2: Added Apple approximated adapter factories. (This may not be correct, please test if you have access to Apple APIs.)
 *             Added Sony factories that use Sony's names.
 *             Added Raw factories for Resistance and AutomaticGun that give direct access to bit-packed region data.
 *             Added ReWASD factories that replicate reWASD effects, warts and all.
 *             Trigger enumerations now and wrapper classes static.
 *             Minor documentation fixes.
 * Revision 3: Corrected Apple factories based on new capture log tests that show only simple rounding was needed.
 * Revision 4: Added 3 new Apple factories based on documentation and capture logs.
 *             These effects are not fully confirmed and are poorly documented even in Apple's docs.
 *             Two of these new effects are similar to our existing raw effect functions.
 * Revision 5: Reorganized and renamed functions and paramaters to be more inline with Sony's API.
 *             Information on the API was exposed by Apple and now further Steamworks version 1.55.
 *             Information is offically source from Apple documentation and Steamworks via logging
 *             HID writes to device based in inputs to new Steamworks functions. Interestingly, my
 *             Raw factories now have equivilents in Sony's offical API and will also be renamed.
 *             Full change list:
 *               TriggerEffectType Enum is re-organized for clarity and comment typoes corrected
 *               TriggerEffectType.Reset is now TriggerEffectType.Off
 *               TriggerEffectType.Resistance is now TriggerEffectType.Feedback
 *               TriggerEffectType.SemiAutomaticGun is now TriggerEffectType.Weapon
 *               TriggerEffectType.AutomaticGun is now TriggerEffectType.Vibration
 *               TriggerEffectType.SimpleResistance is now TriggerEffectType.Simple_Feedback
 *               TriggerEffectType.SimpleSemiAutomaticGun is now TriggerEffectType.Simple_Weapon
 *               TriggerEffectType.SimpleAutomaticGun is now TriggerEffectType.Simple_Vibration
 *               TriggerEffectType.LimitedResistance is now TriggerEffectType.Limited_Feedback
 *               TriggerEffectType.LimitedSemiAutomaticGun is now TriggerEffectType.Limited_Weapon
 *               -----------------------------------------------------------------------------------
 *               TriggerEffectGenerator.Reset(byte *destinationArray, int destinationIndex) is now TriggerEffectGenerator.Off(byte *destinationArray, int destinationIndex)
 *               TriggerEffectGenerator.Resistance(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t force) is now TriggerEffectGenerator.Feedback(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t strength)
 *               TriggerEffectGenerator.SemiAutomaticGun(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t end, uint16_t force) is now TriggerEffectGenerator.Weapon(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t strength)
 *               TriggerEffectGenerator.AutomaticGun(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t strength, uint16_t frequency) is now TriggerEffectGenerator.Vibration(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t amplitude, uint16_t frequency)
 *               -----------------------------------------------------------------------------------
 *               TriggerEffectGenerator.Bow(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t end, uint16_t force, uint16_t snapForce) is now TriggerEffectGenerator.
 *               TriggerEffectGenerator.Galloping(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t end, uint16_t firstFoot, uint16_t secondFoot, uint16_t frequency) is now TriggerEffectGenerator.Galloping(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t firstFoot, uint16_t secondFoot, uint16_t frequency)
 *               TriggerEffectGenerator.Machine(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t end, uint16_t strengthA, uint16_t strengthB, uint16_t frequency, uint16_t period) is now TriggerEffectGenerator.Machine(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t amplitudeA, uint16_t amplitudeB, uint16_t frequency, uint16_t period)
 *               -----------------------------------------------------------------------------------
 *               TriggerEffectGenerator.SimpleResistance(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t force) is now TriggerEffectGenerator.Simple_Feedback(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t strength)
 *               TriggerEffectGenerator.SimpleSemiAutomaticGun(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t end, uint16_t force) is now TriggerEffectGenerator.Simple_Weapon(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t strength)
 *               TriggerEffectGenerator.SimpleAutomaticGun(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t strength, uint16_t frequency) is now TriggerEffectGenerator.Simple_Vibration(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t amplitude, uint16_t frequency)
 *               -----------------------------------------------------------------------------------
 *               TriggerEffectGenerator.LimitedResistance(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t force) is now TriggerEffectGenerator.Limited_Feedback(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t strength)
 *               TriggerEffectGenerator.LimitedSemiAutomaticGun(byte *destinationArray, int destinationIndex, uint16_t start, uint16_t end, uint16_t force) is now TriggerEffectGenerator.Limited_Weapon(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t strength)
 *               -----------------------------------------------------------------------------------
 *               TriggerEffectGenerator.Raw.ResistanceRaw(byte *destinationArray, int destinationIndex, uint16_t *force) is now TriggerEffectGenerator.MultiplePositionFeedback(byte *destinationArray, int destinationIndex, uint16_t *strength)
 *               TriggerEffectGenerator.Raw.AutomaticGunRaw(byte *destinationArray, int destinationIndex, uint16_t *strength, uint16_t frequency) is now TriggerEffectGenerator.MultiplePositionVibration(byte *destinationArray, int destinationIndex, uint16_t frequency, uint16_t *amplitude)
 * Revision 6: Fixed MultiplePositionVibration not using frequency paramater.
 */

/// <summary>
/// DualSense controller trigger effect generators.
/// Revision: 6
///
/// If you are converting from offical Sony code you will need to convert your chosen effect enum to its chosen factory
/// function and your paramater struct to paramaters for that function. Please also note that you will need to track the
/// controller's currently set effect yourself. Note that all effect factories will return false and not modify the
/// destinationArray if invalid paramaters are used. If paramaters that would result in zero effect are used, the
/// <see cref="TriggerEffectType.Off">Off</see> effect is applied instead in line with Sony's offical behavior.
/// All Unofficial, simple, and limited effects are defined as close to the offical effect implementations as possible.
/// </summary>
class TriggerEffectGenerator
{
public:
// Offical Effects

	/// <summary>
	/// Turn the trigger effect off and return the trigger stop to the neutral position.
	/// This is an offical effect and is expected to be present in future DualSense firmware.
	/// </summary>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Off(byte *destinationArray, int destinationIndex);

	/// <summary>
	/// Trigger will resist movement beyond the start position.
	/// The trigger status nybble will report 0 before the effect and 1 when in the effect.
	/// This is an offical effect and is expected to be present in future DualSense firmware.
	/// </summary>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="position">The starting zone of the trigger effect. Must be between 0 and 9 inclusive.</param>
	/// <param name="strength">The force of the resistance. Must be between 0 and 8 inclusive.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Feedback(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t strength);

	/// <summary>
	/// Trigger will resist movement beyond the start position until the end position.
	/// The trigger status nybble will report 0 before the effect and 1 when in the effect,
	/// and 2 after until again before the start position.
	/// This is an offical effect and is expected to be present in future DualSense firmware.
	/// </summary>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="startPosition">The starting zone of the trigger effect. Must be between 2 and 7 inclusive.</param>
	/// <param name="endPosition">The ending zone of the trigger effect. Must be between <paramref name="startPosition"/>+1 and 8 inclusive.</param>
	/// <param name="strength">The force of the resistance. Must be between 0 and 8 inclusive.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Weapon(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t strength);

	/// <summary>
	/// Trigger will vibrate with the input amplitude and frequency beyond the start position.
	/// The trigger status nybble will report 0 before the effect and 1 when in the effect.
	/// This is an offical effect and is expected to be present in future DualSense firmware.
	/// </summary>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="position">The starting zone of the trigger effect. Must be between 0 and 9 inclusive.</param>
	/// <param name="amplitude">Strength of the automatic cycling action. Must be between 0 and 8 inclusive.</param>
	/// <param name="frequency">Frequency of the automatic cycling action in hertz.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Vibration(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t amplitude, uint16_t frequency);

	/// <summary>
	/// Trigger will resist movement at varrying strengths in 10 regions.
	/// This is an offical effect and is expected to be present in future DualSense firmware.
	/// </summary>
	/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="strength">Array of 10 resistance values for zones 0 through 9. Must be between 0 and 8 inclusive.</param>
	/// <returns>The success of the effect write.</returns>
	static bool MultiplePositionFeedback(byte *destinationArray, int destinationIndex, std::vector<uint16_t> &strength);

	/// <summary>
	/// Trigger will resist movement at a linear range of strengths.
	/// This is an offical effect and is expected to be present in future DualSense firmware.
	/// </summary>
	/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="startPosition">The starting zone of the trigger effect. Must be between 0 and 8 inclusive.</param>
	/// <param name="endPosition">The ending zone of the trigger effect. Must be between <paramref name="startPosition"/>+1 and 9 inclusive.</param>
	/// <param name="startStrength">The force of the resistance at the start. Must be between 1 and 8 inclusive.</param>
	/// <param name="endStrength">The force of the resistance at the end. Must be between 1 and 8 inclusive.</param>
	/// <returns>The success of the effect write.</returns>
	static bool SlopeFeedback(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t startStrength, uint16_t endStrength);

	/// <summary>
	/// Trigger will vibrate movement at varrying amplitudes and one frequency in 10 regions.
	/// This is an offical effect and is expected to be present in future DualSense firmware.
	/// </summary>
	/// <remarks>
	/// Note this factory's results may not perform as expected.
	/// </remarks>
	/// <seealso cref="Vibration(byte[], int, uint16_t, uint16_t, uint16_t)"/>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="amplitude">Array of 10 strength values for zones 0 through 9. Must be between 0 and 8 inclusive.</param>
	/// <param name="frequency">Frequency of the automatic cycling action in hertz.</param>
	/// <returns>The success of the effect write.</returns>
	static bool MultiplePositionVibration(byte *destinationArray, int destinationIndex, uint16_t frequency, std::vector<uint16_t> &amplitude);


// Unofficial but Unique Effects


	/// <summary>
	/// The effect resembles the <see cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)">Weapon</see>
	/// effect, however there is a snap-back force that attempts to reset the trigger.
	/// This is not an offical effect and may be removed in a future DualSense firmware.
	/// </summary>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="startPosition">The starting zone of the trigger effect. Must be between 0 and 8 inclusive.</param>
	/// <param name="endPosition">The ending zone of the trigger effect. Must be between <paramref name="startPosition"/>+1 and 8 inclusive.</param>
	/// <param name="strength">The force of the resistance. Must be between 0 and 8 inclusive.</param>
	/// <param name="snapForce">The force of the snap-back. Must be between 0 and 8 inclusive.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Bow(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t strength, uint16_t snapForce);

	/// <summary>
	/// Trigger will oscillate in a rythmic pattern resembling galloping. Note that the
	/// effect is only discernable at low frequency values.
	/// This is not an offical effect and may be removed in a future DualSense firmware.
	/// </summary>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="startPosition">The starting zone of the trigger effect. Must be between 0 and 8 inclusive.</param>
	/// <param name="endPosition">The ending zone of the trigger effect. Must be between <paramref name="startPosition"/>+1 and 9 inclusive.</param>
	/// <param name="firstFoot">Position of second foot in cycle. Must be between 0 and 6 inclusive.</param>
	/// <param name="secondFoot">Position of second foot in cycle. Must be between <paramref name="firstFoot"/>+1 and 7 inclusive.</param>
	/// <param name="frequency">Frequency of the automatic cycling action in hertz.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Galloping(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t firstFoot, uint16_t secondFoot, uint16_t frequency);

	/// <summary>
	/// This effect resembles <see cref="Vibration(byte[], int, uint16_t, uint16_t, uint16_t)">Vibration</see>
	/// but will oscilate between two amplitudes.
	/// This is not an offical effect and may be removed in a future DualSense firmware.
	/// </summary>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="startPosition">The starting zone of the trigger effect. Must be between 0 and 8 inclusive.</param>
	/// <param name="endPosition">The ending zone of the trigger effect. Must be between <paramref name="startPosition"/> and 9 inclusive.</param>
	/// <param name="amplitudeA">Primary strength of cycling action. Must be between 0 and 7 inclusive.</param>
	/// <param name="amplitudeB">Secondary strength of cycling action. Must be between 0 and 7 inclusive.</param>
	/// <param name="frequency">Frequency of the automatic cycling action in hertz.</param>
	/// <param name="period">Period of the oscillation between <paramref name="amplitudeA"/> and <paramref name="amplitudeB"/> in tenths of a second.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Machine(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t amplitudeA, uint16_t amplitudeB, uint16_t frequency, uint16_t period);


// Simple Effects


	/// <summary>
	/// Simplistic Feedback effect data generator.
	/// This is not an offical effect and has an offical alternative. It may be removed in a future DualSense firmware.
	/// </summary>
	/// <remarks>
	/// Use <see cref="Feedback(byte[], int, uint16_t, uint16_t)"/> instead.
	/// </remarks>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="position">The starting zone of the trigger effect.</param>
	/// <param name="strength">The force of the resistance.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Simple_Feedback(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t strength);

	/// <summary>
	/// Simplistic Weapon effect data generator.
	/// This is not an offical effect and has an offical alternative. It may be removed in a future DualSense firmware.
	/// </summary>
	/// <remarks>
	/// Use <see cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/> instead.
	/// </remarks>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="startPosition">The starting zone of the trigger effect.</param>
	/// <param name="endPosition">The ending zone of the trigger effect.</param>
	/// <param name="strength">The force of the resistance.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Simple_Weapon(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t strength);

	/// <summary>
	/// Simplistic Vibration effect data generator.
	/// This is not an offical effect and has an offical alternative. It may be removed in a future DualSense firmware.
	/// </summary>
	/// <remarks>
	/// Use <see cref="Vibration(byte[], int, uint16_t, uint16_t, uint16_t)"/> instead.
	/// </remarks>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="position">The starting zone of the trigger effect.</param>
	/// <param name="amplitude">Strength of the automatic cycling action.</param>
	/// <param name="frequency">Frequency of the automatic cycling action in hertz.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Simple_Vibration(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t amplitude, uint16_t frequency);


// Limited Effects


	/// <summary>
	/// Simplistic Feedback effect data generator with stricter paramater limits.
	/// This is not an offical effect and has an offical alternative. It may be removed in a future DualSense firmware.
	/// </summary>
	/// <remarks>
	/// Use <see cref="Feedback(byte[], int, uint16_t, uint16_t)"/> instead.
	/// </remarks>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="position">The starting zone of the trigger effect.</param>
	/// <param name="strength">The force of the resistance. Must be between 0 and 10 inclusive.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Limited_Feedback(byte *destinationArray, int destinationIndex, uint16_t position, uint16_t strength);

	/// <summary>
	/// Simplistic Weapon effect data generator with stricter paramater limits.
	/// This is not an offical effect and has an offical alternative. It may be removed in a future DualSense firmware.
	/// </summary>
	/// <remarks>
	/// Use <see cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/> instead.
	/// </remarks>
	/// <param name="destinationArray">The byte *that receives the data.</param>
	/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
	/// <param name="startPosition">The starting zone of the trigger effect. Must be 16 or higher.</param>
	/// <param name="endPosition">The ending zone of the trigger effect. Must be between <paramref name="startPosition"/> and <paramref name="startPosition"/>+100 inclusive.</param>
	/// <param name="strength">The force of the resistance. Must be between 0 and 10 inclusive.</param>
	/// <returns>The success of the effect write.</returns>
	static bool Limited_Weapon(byte *destinationArray, int destinationIndex, uint16_t startPosition, uint16_t endPosition, uint16_t strength);

	/// <summary>
	/// Interface adapaters patterned after Apple's GCDualSenseAdaptiveTrigger classs.
	/// </summary>
	class Apple
	{
	public:
		/// <summary>
		/// Sets the adaptive trigger to feedback mode. The start position and strength of the effect can be set arbitrarily. The trigger arm will continue to provide a
		/// constant degree of feedback whenever it is depressed further than the start position.
		/// </summary>
		/// <remarks>
		/// Documentation ported from Apple's API Docs.
		/// </remarks>
		/// <seealso cref="Off(byte[], int)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool SetModeOff(byte *destinationArray, int destinationIndex)
		{
			return Off(destinationArray, destinationIndex);
		}

		/// <summary>
		/// Sets the adaptive trigger to feedback mode. The start position and strength of the effect can be set arbitrarily. The trigger arm will continue to provide a
		/// constant degree of feedback whenever it is depressed further than the start position.
		/// </summary>
		/// <remarks>
		/// Documentation ported from Apple's API Docs.
		/// </remarks>
		/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="startPosition">A normalized float from [0-1], with 0 representing the smallest possible trigger depression and 1 representing the maximum trigger depression.</param>
		/// <param name="resistiveStrength">A normalized float from [0-1], with 0 representing the minimum effect strength (off entirely) and 1 representing the maximum effect strength.</param>
		/// <returns>The success of the effect write.</returns>
		static bool SetModeFeedbackWithStartPosition(byte *destinationArray, int destinationIndex, float startPosition, float resistiveStrength);

		/// <summary>
		/// Sets the adaptive trigger to weapon mode. The start position, end position, and strength of the effect can be set arbitrarily; however the end position must be larger than the start position.
		/// The trigger arm will continue to provide a constant degree of feedback whenever it is depressed further than the start position. Once the trigger arm has been depressed past the end
		/// position, the strength of the effect will immediately fall to zero, providing a "sense of release" similar to that provided by pulling the trigger of a weapon.
		/// </summary>
		/// <remarks>
		/// Documentation ported from Apple's API Docs.
		/// </remarks>
		/// <seealso cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="startPosition">A normalized float from [0-1], with 0 representing the smallest possible depression and 1 representing the maximum trigger depression. The effect will begin once the trigger is depressed beyond this point.</param>
		/// <param name="endPosition">A normalized float from [0-1], with 0 representing the smallest possible depression and 1 representing the maximum trigger depression. Must be greater than startPosition. The effect will end once the trigger is depressed beyond this point.</param>
		/// <param name="resistiveStrength">A normalized float from [0-1], with 0 representing the minimum effect strength (off entirely) and 1 representing the maximum effect strength.</param>
		/// <returns>The success of the effect write.</returns>
		static bool SetModeWeaponWithStartPosition(byte *destinationArray, int destinationIndex, float startPosition, float endPosition, float resistiveStrength);

		/// <summary>
		/// Sets the adaptive trigger to vibration mode. The start position, amplitude, and frequency of the effect can be set arbitrarily. The trigger arm will continue to strike against
		/// the trigger whenever it is depressed further than the start position, providing a "sense of vibration".
		/// </summary>
		/// <remarks>
		/// Documentation ported from Apple's API Docs.
		/// </remarks>
		/// <seealso cref="Vibration(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="startPosition">A normalized float from [0-1], with 0 representing the smallest possible depression and 1 representing the maximum trigger depression. The effect will begin once the trigger is depressed beyond this point.</param>
		/// <param name="amplitude">A normalized float from [0-1], with 0 representing the minimum effect strength (off entirely) and 1 representing the maximum effect strength.</param>
		/// <param name="frequency">A normalized float from [0-1], with 0 representing the minimum frequency and 1 representing the maximum frequency of the vibration effect.</param>
		/// <returns>The success of the effect write.</returns>
		static bool SetModeVibrationWithStartPosition(byte *destinationArray, int destinationIndex, float startPosition, float amplitude, float frequency);

		/// <summary>
		/// Sets the adaptive trigger to feedback mode. The strength of the effect can be set arbitrarily per zone.
		/// This implementation is not confirmed.
		/// </summary>
		/// <remarks>
		/// Documentation ported from Apple's API Docs.
		/// </remarks>
		/// <seealso cref="MultiplePositionFeedback(byte[], int, uint16_t[])"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="positionalResistiveStrengths">An array of 10 normalized floats from [0-1], with 0 representing the minimum effect strength (off entirely) and 1 representing the maximum effect strength.</param>
		/// <returns>The success of the effect write.</returns>
		static bool SetModeFeedback(byte *destinationArray, int destinationIndex, std::vector<float> &positionalResistiveStrengths);

		/// <summary>
		/// Sets the adaptive trigger to feedback mode. The strength of the effect will change across zones based on a slope.
		/// This implementation is not confirmed.
		/// </summary>
		/// <remarks>
		/// Documentation ported from Apple's API Docs.
		/// </remarks>
		/// <seealso cref="MultiplePositionFeedback(byte[], int, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="startPosition">A normalized float from [0-1], with 0 representing the smallest possible depression and 1 representing the maximum trigger depression. The effect will begin once the trigger is depressed beyond this point.</param>
		/// <param name="endPosition">A normalized float from [0-1], with 0 representing the smallest possible depression and 1 representing the maximum trigger depression. Must be greater than startPosition. The effect will end once the trigger is depressed beyond this point.</param>
		/// <param name="startStrength">A normalized float from [0-1], with 0 representing the minimum effect strength (off entirely) and 1 representing the maximum effect strength.</param>
		/// <param name="endStrength">A normalized float from [0-1], with 0 representing the minimum effect strength (off entirely) and 1 representing the maximum effect strength.</param>
		/// <returns>The success of the effect write.</returns>
		static bool setModeSlopeFeedback(byte *destinationArray, int destinationIndex, float startPosition, float endPosition, float startStrength, float endStrength);

		/// <summary>
		/// Sets the adaptive trigger to vibration mode. The frequency of the effect can be set arbitrarily and the amplitude arbitrarily per zone.
		/// This implementation is not confirmed.
		/// </summary>
		/// <remarks>
		/// Documentation ported from Apple's API Docs.
		/// </remarks>
		/// <seealso cref="MultiplePositionVibration(byte[], int, uint16_t, uint16_t[])"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="positionalAmplitudes">An array of 10 normalized floats from [0-1], with 0 representing the minimum effect strength (off entirely) and 1 representing the maximum effect strength.</param>
		/// <param name="frequency">A normalized float from [0-1], with 0 representing the minimum frequency and 1 representing the maximum frequency of the vibration effect.</param>
		/// <returns>The success of the effect write.</returns>
		static bool setModeVibration(byte *destinationArray, int destinationIndex, std::vector<float> &positionalAmplitudes, float frequency);
	};

	/// <summary>
	/// Interface adapaters patterned after reWASD's actual interface.
	/// </summary>
	/// <remarks>
	/// This information is based on sniffing the USB traffic from reWASD. Broken implementations are kept though immaterial inaccuracies are corrected.
	/// </remarks>
	class ReWASD
	{
	public:
		/// <summary>
		/// Full Press trigger stop effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Weapon with a start value of 0x90, end value of 0xa0, and a force of 0xff.
		/// </remarks>
		/// <seealso cref="Simple_Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <seealso cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool FullPress(byte *destinationArray, int destinationIndex)
		{
			return Simple_Weapon(destinationArray, destinationIndex, 0x90, 0xa0, 0xff);
		}

		/// <summary>
		/// Soft Press trigger stop effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Weapon with a start value of 0x70, end value of 0xa0, and a force of 0xff.
		/// </remarks>
		/// <seealso cref="Simple_Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <seealso cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool SoftPress(byte *destinationArray, int destinationIndex)
		{
			return Simple_Weapon(destinationArray, destinationIndex, 0x70, 0xa0, 0xff);
		}

		/// <summary>
		/// Medium Press trigger stop effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Weapon with a start value of 0x45, end value of 0xa0, and a force of 0xff.
		/// </remarks>
		/// <seealso cref="Simple_Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <seealso cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool MediumPress(byte *destinationArray, int destinationIndex)
		{
			return Simple_Weapon(destinationArray, destinationIndex, 0x45, 0xa0, 0xff);
		}

		/// <summary>
		/// Hard Press trigger stop effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Weapon with a start value of 0x20, end value of 0xa0, and a force of 0xff.
		/// </remarks>
		/// <seealso cref="Simple_Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <seealso cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool HardPress(byte *destinationArray, int destinationIndex)
		{
			return Simple_Weapon(destinationArray, destinationIndex, 0x20, 0xa0, 0xff);
		}

		/// <summary>
		/// Pulse trigger stop effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Weapon with a start value of 0x00, end value of 0x00, and a force of 0x00.
		/// </remarks>
		/// <seealso cref="Simple_Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <seealso cref="Weapon(byte[], int, uint16_t, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool Pulse(byte *destinationArray, int destinationIndex)
		{
			return Simple_Weapon(destinationArray, destinationIndex, 0x00, 0x00, 0x00);
		}

		/// <summary>
		/// Choppy resistance effect data generator.
		/// </summary>
		/// <remarks>
		/// Abuses Feedback effect to set a resistance in 3 of 10 trigger regions.
		/// </remarks>
		/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static bool Choppy(byte *destinationArray, int destinationIndex);

		/// <summary>
		/// Soft Rigidity feedback effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Feedback with a start value of 0x00 and a force of 0x00.
		/// </remarks>
		/// <seealso cref="Simple_Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool SoftRigidity(byte *destinationArray, int destinationIndex)
		{
			return Simple_Feedback(destinationArray, destinationIndex, 0x00, 0x00);
		}

		/// <summary>
		/// Medium Rigidity feedback effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Feedback with a start value of 0x00 and a force of 0x64.
		/// </remarks>
		/// <seealso cref="Simple_Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool MediumRigidity(byte *destinationArray, int destinationIndex)
		{
			return Simple_Feedback(destinationArray, destinationIndex, 0x00, 0x64);
		}

		/// <summary>
		/// Max Rigidity feedback effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Feedback with a start value of 0x00 and a force of 0xdc.
		/// </remarks>
		/// <seealso cref="Simple_Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool MaxRigidity(byte *destinationArray, int destinationIndex)
		{
			return Simple_Feedback(destinationArray, destinationIndex, 0x00, 0xdc);
		}

		/// <summary>
		/// Half Press feedback effect data generator.
		/// </summary>
		/// <remarks>
		/// Uses Simple_Feedback with a start value of 0x55 and a force of 0x64.
		/// </remarks>
		/// <seealso cref="Simple_Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <seealso cref="Feedback(byte[], int, uint16_t, uint16_t)"/>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <returns>The success of the effect write.</returns>
		static inline bool HalfPress(byte *destinationArray, int destinationIndex)
		{
			return Simple_Feedback(destinationArray, destinationIndex, 0x55, 0x64);
		}

		/// <summary>
		/// Rifle vibration effect data generator with some wasted bits.
		/// Bad coding from reWASD was faithfully replicated.
		/// </summary>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="frequency">Frequency of the automatic cycling action in hertz. Must be between 2 and 20 inclusive.</param>
		/// <returns>The success of the effect write.</returns>
		static bool Rifle(byte *destinationArray, int destinationIndex, uint16_t frequency = 10);

		/// <summary>
		/// Vibration vibration effect with incorrect strength handling.
		/// Bad coding from reWASD was faithfully replicated.
		/// </summary>
		/// <param name="destinationArray">The byte *that receives the data.</param>
		/// <param name="destinationIndex">A 32-bit integer that represents the index in the destinationArray at which storing begins.</param>
		/// <param name="strength">Strength of the automatic cycling action. Must be between 1 and 255 inclusive. This is two 3 bit numbers with the remaining 2 high bits unused. Yes, reWASD uses this value incorrectly.</param>
		/// <param name="frequency">Frequency of the automatic cycling action in hertz. Must be between 1 and 255 inclusive.</param>
		/// <returns>The success of the effect write.</returns>
		static bool Vibration(byte *destinationArray, int destinationIndex, uint16_t strength = 220, uint16_t frequency = 30);
	};
};
} // namespace ExtendInput.DataTools. DualSense