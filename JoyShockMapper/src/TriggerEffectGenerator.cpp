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

#include "TriggerEffectGenerator.h"
#include <cstdint>
#include <cmath>

namespace ExtendInput::DataTools::DualSense
{

bool TriggerEffectGenerator::Off(byte *destinationArray, int destinationIndex)
{
	destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Off;
	destinationArray[destinationIndex + 1] = 0x00;
	destinationArray[destinationIndex + 2] = 0x00;
	destinationArray[destinationIndex + 3] = 0x00;
	destinationArray[destinationIndex + 4] = 0x00;
	destinationArray[destinationIndex + 5] = 0x00;
	destinationArray[destinationIndex + 6] = 0x00;
	destinationArray[destinationIndex + 7] = 0x00;
	destinationArray[destinationIndex + 8] = 0x00;
	destinationArray[destinationIndex + 9] = 0x00;
	destinationArray[destinationIndex + 10] = 0x00;
	return true;
}

bool TriggerEffectGenerator::Feedback(byte *destinationArray, int destinationIndex, byte position, byte strength)
{
	if (position > 9)
		return false;
	if (strength > 8)
		return false;
	if (strength > 0)
	{
		byte forceValue = (byte)((strength - 1) & 0x07);
		uint32_t forceZones = 0;
		uint16_t activeZones = 0;
		for (int i = position; i < 10; i++)
		{
			forceZones |= (uint32_t)(forceValue << (3 * i));
			activeZones |= (uint16_t)(1 << i);
		}

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Feedback;
		destinationArray[destinationIndex + 1] = (byte)((activeZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((activeZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)((forceZones >> 0) & 0xff);
		destinationArray[destinationIndex + 4] = (byte)((forceZones >> 8) & 0xff);
		destinationArray[destinationIndex + 5] = (byte)((forceZones >> 16) & 0xff);
		destinationArray[destinationIndex + 6] = (byte)((forceZones >> 24) & 0xff);
		destinationArray[destinationIndex + 7] = 0x00; // (byte)((forceZones >> 32) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 8] = 0x00; // (byte)((forceZones >> 40) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Weapon(byte *destinationArray, int destinationIndex, byte startPosition, byte endPosition, byte strength)
{
	if (startPosition > 7 || startPosition < 2)
		return false;
	if (endPosition > 8)
		return false;
	if (endPosition <= startPosition)
		return false;
	if (strength > 8)
		return false;
	if (strength > 0)
	{
		uint16_t startAndStopZones = (uint16_t)((1 << startPosition) | (1 << endPosition));

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Weapon;
		destinationArray[destinationIndex + 1] = (byte)((startAndStopZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((startAndStopZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)(strength - 1); // this is actually packed into 3 bits, but since it's only one why bother with the fancy code?
		destinationArray[destinationIndex + 4] = 0x00;
		destinationArray[destinationIndex + 5] = 0x00;
		destinationArray[destinationIndex + 6] = 0x00;
		destinationArray[destinationIndex + 7] = 0x00;
		destinationArray[destinationIndex + 8] = 0x00;
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Vibration(byte *destinationArray, int destinationIndex, byte position, byte amplitude, byte frequency)
{
	if (position > 9)
		return false;
	if (amplitude > 8)
		return false;
	if (amplitude > 0 && frequency > 0)
	{
		byte strengthValue = (byte)((amplitude - 1) & 0x07);
		uint32_t amplitudeZones = 0;
		uint16_t activeZones = 0;
		for (int i = position; i < 10; i++)
		{
			amplitudeZones |= (uint32_t)(strengthValue << (3 * i));
			activeZones |= (uint16_t)(1 << i);
		}

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Vibration;
		destinationArray[destinationIndex + 1] = (byte)((activeZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((activeZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)((amplitudeZones >> 0) & 0xff);
		destinationArray[destinationIndex + 4] = (byte)((amplitudeZones >> 8) & 0xff);
		destinationArray[destinationIndex + 5] = (byte)((amplitudeZones >> 16) & 0xff);
		destinationArray[destinationIndex + 6] = (byte)((amplitudeZones >> 24) & 0xff);
		destinationArray[destinationIndex + 7] = 0x00; // (byte)((strengthZones >> 32) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 8] = 0x00; // (byte)((strengthZones >> 40) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 9] = frequency;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::MultiplePositionFeedback(byte *destinationArray, int destinationIndex, std::vector<byte> &strength)
{
	if (strength.size() != 10)
		return false;

	static const auto isInvalid = [](byte amp)
	{
		return amp > 8;
	};
	if (std::find_if(strength.begin(), strength.end(), isInvalid) == strength.end())
	{
		uint32_t forceZones = 0;
		uint16_t activeZones = 0;
		for (int i = 0; i < 10; i++)
		{
			if (strength[i] > 0)
			{
				byte forceValue = (byte)((strength[i] - 1) & 0x07);
				forceZones |= (uint32_t)(forceValue << (3 * i));
				activeZones |= (uint16_t)(1 << i);
			}
		}

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Feedback;
		destinationArray[destinationIndex + 1] = (byte)((activeZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((activeZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)((forceZones >> 0) & 0xff);
		destinationArray[destinationIndex + 4] = (byte)((forceZones >> 8) & 0xff);
		destinationArray[destinationIndex + 5] = (byte)((forceZones >> 16) & 0xff);
		destinationArray[destinationIndex + 6] = (byte)((forceZones >> 24) & 0xff);
		destinationArray[destinationIndex + 7] = 0x00; // (byte)((forceZones >> 32) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 8] = 0x00; // (byte)((forceZones >> 40) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::SlopeFeedback(byte *destinationArray, int destinationIndex, byte startPosition, byte endPosition, byte startStrength, byte endStrength)
{
	if (startPosition > 8 || startPosition < 0)
		return false;
	if (endPosition > 9)
		return false;
	if (endPosition <= startPosition)
		return false;
	if (startStrength > 8)
		return false;
	if (startStrength < 1)
		return false;
	if (endStrength > 8)
		return false;
	if (endStrength < 1)
		return false;

	std::vector<byte> strength(10, '\0');
	float slope = 1.0f * (endStrength - startStrength) / (endPosition - startPosition);
	for (int i = (int)startPosition; i < 10; i++)
		if (i <= endPosition)
			strength[i] = (byte)std::round(startStrength + slope * (i - startPosition));
		else
			strength[i] = endStrength;

	return MultiplePositionFeedback(destinationArray, destinationIndex, strength);
}

bool TriggerEffectGenerator::MultiplePositionVibration(byte *destinationArray, int destinationIndex, byte frequency, std::vector<byte> &amplitude)
{
	if (amplitude.size() != 10)
		return false;
	static const auto isInvalid = [](byte amp)
	{
		return amp > 8;
	};
	if (frequency > 0 && std::find_if(amplitude.begin(), amplitude.end(), isInvalid) == amplitude.end())
	{
		uint32_t strengthZones = 0;
		uint16_t activeZones = 0;
		for (int i = 0; i < 10; i++)
		{
			if (amplitude[i] > 0)
			{
				byte strengthValue = (byte)((amplitude[i] - 1) & 0x07);
				strengthZones |= (uint32_t)(strengthValue << (3 * i));
				activeZones |= (uint16_t)(1 << i);
			}
		}

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Vibration;
		destinationArray[destinationIndex + 1] = (byte)((activeZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((activeZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)((strengthZones >> 0) & 0xff);
		destinationArray[destinationIndex + 4] = (byte)((strengthZones >> 8) & 0xff);
		destinationArray[destinationIndex + 5] = (byte)((strengthZones >> 16) & 0xff);
		destinationArray[destinationIndex + 6] = (byte)((strengthZones >> 24) & 0xff);
		destinationArray[destinationIndex + 7] = 0x00; // (byte)((forceZones >> 32) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 8] = 0x00; // (byte)((forceZones >> 40) & 0xff); // need 64bit for this, but we already have enough space
		destinationArray[destinationIndex + 9] = frequency;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Bow(byte *destinationArray, int destinationIndex, byte startPosition, byte endPosition, byte strength, byte snapForce)
{
	if (startPosition > 8)
		return false;
	if (endPosition > 8)
		return false;
	if (startPosition >= endPosition)
		return false;
	if (strength > 8)
		return false;
	if (snapForce > 8)
		return false;
	if (endPosition > 0 && strength > 0 && snapForce > 0)
	{
		uint16_t startAndStopZones = (uint16_t)((1 << startPosition) | (1 << endPosition));
		uint32_t forcePair = (uint32_t)((((strength - 1) & 0x07) << (3 * 0)) | (((snapForce - 1) & 0x07) << (3 * 1)));

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Bow;
		destinationArray[destinationIndex + 1] = (byte)((startAndStopZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((startAndStopZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)((forcePair >> 0) & 0xff);
		destinationArray[destinationIndex + 4] = (byte)((forcePair >> 8) & 0xff);
		destinationArray[destinationIndex + 5] = 0x00;
		destinationArray[destinationIndex + 6] = 0x00;
		destinationArray[destinationIndex + 7] = 0x00;
		destinationArray[destinationIndex + 8] = 0x00;
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Galloping(byte *destinationArray, int destinationIndex, byte startPosition, byte endPosition, byte firstFoot, byte secondFoot, byte frequency)
{
	if (startPosition > 8)
		return false;
	if (endPosition > 9)
		return false;
	if (startPosition >= endPosition)
		return false;
	if (secondFoot > 7)
		return false;
	if (firstFoot > 6)
		return false;
	if (firstFoot >= secondFoot)
		return false;
	if (frequency > 0)
	{
		uint16_t startAndStopZones = (uint16_t)((1 << startPosition) | (1 << endPosition));
		uint32_t timeAndRatio = (uint32_t)(((secondFoot & 0x07) << (3 * 0)) | ((firstFoot & 0x07) << (3 * 1)));

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Galloping;
		destinationArray[destinationIndex + 1] = (byte)((startAndStopZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((startAndStopZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)((timeAndRatio >> 0) & 0xff);
		destinationArray[destinationIndex + 4] = frequency; // this is actually packed into 3 bits, but since it's only one why bother with the fancy code?
		destinationArray[destinationIndex + 5] = 0x00;
		destinationArray[destinationIndex + 6] = 0x00;
		destinationArray[destinationIndex + 7] = 0x00;
		destinationArray[destinationIndex + 8] = 0x00;
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Machine(byte *destinationArray, int destinationIndex, byte startPosition, byte endPosition, byte amplitudeA, byte amplitudeB, byte frequency, byte period)
{
	if (startPosition > 8)
		return false;
	if (endPosition > 9)
		return false;
	if (endPosition <= startPosition)
		return false;
	if (amplitudeA > 7)
		return false;
	if (amplitudeB > 7)
		return false;
	if (frequency > 0)
	{
		uint16_t startAndStopZones = (uint16_t)((1 << startPosition) | (1 << endPosition));
		uint32_t strengthPair = (uint32_t)(((amplitudeA & 0x07) << (3 * 0)) | ((amplitudeB & 0x07) << (3 * 1)));

		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Machine;
		destinationArray[destinationIndex + 1] = (byte)((startAndStopZones >> 0) & 0xff);
		destinationArray[destinationIndex + 2] = (byte)((startAndStopZones >> 8) & 0xff);
		destinationArray[destinationIndex + 3] = (byte)((strengthPair >> 0) & 0xff);
		destinationArray[destinationIndex + 4] = frequency;
		destinationArray[destinationIndex + 5] = period;
		destinationArray[destinationIndex + 6] = 0x00;
		destinationArray[destinationIndex + 7] = 0x00;
		destinationArray[destinationIndex + 8] = 0x00;
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Simple_Feedback(byte *destinationArray, int destinationIndex, byte position, byte strength)
{
	destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Simple_Feedback;
	destinationArray[destinationIndex + 1] = position;
	destinationArray[destinationIndex + 2] = strength;
	destinationArray[destinationIndex + 3] = 0x00;
	destinationArray[destinationIndex + 4] = 0x00;
	destinationArray[destinationIndex + 5] = 0x00;
	destinationArray[destinationIndex + 6] = 0x00;
	destinationArray[destinationIndex + 7] = 0x00;
	destinationArray[destinationIndex + 8] = 0x00;
	destinationArray[destinationIndex + 9] = 0x00;
	destinationArray[destinationIndex + 10] = 0x00;
	return true;
}

bool TriggerEffectGenerator::Simple_Weapon(byte *destinationArray, int destinationIndex, byte startPosition, byte endPosition, byte strength)
{
	destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Simple_Weapon;
	destinationArray[destinationIndex + 1] = startPosition;
	destinationArray[destinationIndex + 2] = endPosition;
	destinationArray[destinationIndex + 3] = strength;
	destinationArray[destinationIndex + 4] = 0x00;
	destinationArray[destinationIndex + 5] = 0x00;
	destinationArray[destinationIndex + 6] = 0x00;
	destinationArray[destinationIndex + 7] = 0x00;
	destinationArray[destinationIndex + 8] = 0x00;
	destinationArray[destinationIndex + 9] = 0x00;
	destinationArray[destinationIndex + 10] = 0x00;
	return true;
}

bool TriggerEffectGenerator::Simple_Vibration(byte *destinationArray, int destinationIndex, byte position, byte amplitude, byte frequency)
{
	if (frequency > 0 && amplitude > 0)
	{
		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Simple_Vibration;
		destinationArray[destinationIndex + 1] = frequency;
		destinationArray[destinationIndex + 2] = amplitude;
		destinationArray[destinationIndex + 3] = position;
		destinationArray[destinationIndex + 4] = 0x00;
		destinationArray[destinationIndex + 5] = 0x00;
		destinationArray[destinationIndex + 6] = 0x00;
		destinationArray[destinationIndex + 7] = 0x00;
		destinationArray[destinationIndex + 8] = 0x00;
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Limited_Feedback(byte *destinationArray, int destinationIndex, byte position, byte strength)
{
	if (strength > 10)
		return false;
	if (strength > 0)
	{
		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Limited_Feedback;
		destinationArray[destinationIndex + 1] = position;
		destinationArray[destinationIndex + 2] = strength;
		destinationArray[destinationIndex + 3] = 0x00;
		destinationArray[destinationIndex + 4] = 0x00;
		destinationArray[destinationIndex + 5] = 0x00;
		destinationArray[destinationIndex + 6] = 0x00;
		destinationArray[destinationIndex + 7] = 0x00;
		destinationArray[destinationIndex + 8] = 0x00;
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}

bool TriggerEffectGenerator::Limited_Weapon(byte *destinationArray, int destinationIndex, byte startPosition, byte endPosition, byte strength)
{
	if (startPosition < 0x10)
		return false;
	if (endPosition < startPosition || (startPosition + 100) < endPosition)
		return false;
	if (strength > 10)
		return false;
	if (strength > 0)
	{
		destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Limited_Weapon;
		destinationArray[destinationIndex + 1] = startPosition;
		destinationArray[destinationIndex + 2] = endPosition;
		destinationArray[destinationIndex + 3] = strength;
		destinationArray[destinationIndex + 4] = 0x00;
		destinationArray[destinationIndex + 5] = 0x00;
		destinationArray[destinationIndex + 6] = 0x00;
		destinationArray[destinationIndex + 7] = 0x00;
		destinationArray[destinationIndex + 8] = 0x00;
		destinationArray[destinationIndex + 9] = 0x00;
		destinationArray[destinationIndex + 10] = 0x00;
		return true;
	}
	return Off(destinationArray, destinationIndex);
}


bool TriggerEffectGenerator::Apple::SetModeFeedbackWithStartPosition(byte *destinationArray, int destinationIndex, float startPosition, float resistiveStrength)
{
	startPosition = (float)std::round(startPosition * 9.0f);
	resistiveStrength = (float)std::round(resistiveStrength * 8.0f);
	return Feedback(destinationArray, destinationIndex, (byte)startPosition, (byte)resistiveStrength);
}

bool TriggerEffectGenerator::Apple::SetModeWeaponWithStartPosition(byte *destinationArray, int destinationIndex, float startPosition, float endPosition, float resistiveStrength)
{
	startPosition = (float)std::round(startPosition * 9.0f);
	endPosition = (float)std::round(endPosition * 9.0f);
	resistiveStrength = (float)std::round(resistiveStrength * 8.0f);
	return Weapon(destinationArray, destinationIndex, (byte)startPosition, (byte)endPosition, (byte)resistiveStrength);
}

bool TriggerEffectGenerator::Apple::SetModeVibrationWithStartPosition(byte *destinationArray, int destinationIndex, float startPosition, float amplitude, float frequency)
{
	startPosition = (float)std::round(startPosition * 9.0f);
	amplitude = (float)std::round(amplitude * 8.0f);
	frequency = (float)std::round(frequency * 255.0f);
	return Vibration(destinationArray, destinationIndex, (byte)startPosition, (byte)amplitude, (byte)frequency);
}

bool TriggerEffectGenerator::Apple::SetModeFeedback(byte *destinationArray, int destinationIndex, std::vector<float> &positionalResistiveStrengths)
{
	if (positionalResistiveStrengths.size() != 10)
		return false;

	std::vector<byte> force(10, '\0');
	for (int i = 0; i < 10; i++)
		force[i] = (byte)std::round(positionalResistiveStrengths[i] * 8.0f);

	return MultiplePositionFeedback(destinationArray, destinationIndex, force);
}

bool TriggerEffectGenerator::Apple::setModeSlopeFeedback(byte *destinationArray, int destinationIndex, float startPosition, float endPosition, float startStrength, float endStrength)
{
	startPosition = (float)std::round(startPosition * 9.0f);
	endPosition = (float)std::round(endPosition * 9.0f);
	startStrength = (float)std::round(startStrength * 8.0f);
	endStrength = (float)std::round(endStrength * 8.0f);

	return SlopeFeedback(destinationArray, destinationIndex, (byte)startPosition, (byte)endPosition, (byte)startStrength, (byte)endStrength);
}

bool TriggerEffectGenerator::Apple::setModeVibration(byte *destinationArray, int destinationIndex, std::vector<float> &positionalAmplitudes, float frequency)
{
	if (positionalAmplitudes.size() != 10)
		return false;

	frequency = (float)std::round(frequency * 255.0f);

	std::vector<byte> strength(10, '\0');
	for (int i = 0; i < 10; i++)
		strength[i] = (byte)std::round(positionalAmplitudes[i] * 8.0f);

	return MultiplePositionVibration(destinationArray, destinationIndex, (byte)frequency, strength);
}

bool TriggerEffectGenerator::ReWASD::Choppy(byte *destinationArray, int destinationIndex)
{
	destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Feedback;
	destinationArray[destinationIndex + 1] = (byte)0x02; // region enables
	destinationArray[destinationIndex + 2] = (byte)0x27; // region enables
	destinationArray[destinationIndex + 3] = (byte)0x18; // reWASD uses 0x1f here, but some bits apply to regions not enabled above
	destinationArray[destinationIndex + 4] = (byte)0x00;
	destinationArray[destinationIndex + 5] = (byte)0x00; // reWASD uses 0x27 here, but some bits apply to regions not enabled above
	destinationArray[destinationIndex + 6] = (byte)0x26;
	destinationArray[destinationIndex + 7] = (byte)0x00;
	destinationArray[destinationIndex + 8] = (byte)0x00;
	destinationArray[destinationIndex + 9] = (byte)0x00;
	destinationArray[destinationIndex + 10] = (byte)0x00;
	return true;
}

bool TriggerEffectGenerator::ReWASD::Rifle(byte *destinationArray, int destinationIndex, byte frequency)
{
	if (frequency < 2)
		return false;
	if (frequency > 20)
		return false;

	destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Vibration;
	destinationArray[destinationIndex + 1] = (byte)0x00;
	destinationArray[destinationIndex + 2] = (byte)0x03; // reWASD uses 0xFF here but the top 6 bits are unused
	destinationArray[destinationIndex + 3] = (byte)0x00;
	destinationArray[destinationIndex + 4] = (byte)0x00;
	destinationArray[destinationIndex + 5] = (byte)0x00;
	destinationArray[destinationIndex + 6] = (byte)0x3F; // reWASD uses 0xFF here but the top 2 bits are unused
	destinationArray[destinationIndex + 7] = (byte)0x00;
	destinationArray[destinationIndex + 8] = (byte)0x00;
	destinationArray[destinationIndex + 9] = frequency;
	destinationArray[destinationIndex + 10] = (byte)0x00;
	return true;
}

bool TriggerEffectGenerator::ReWASD::Vibration(byte *destinationArray, int destinationIndex, byte strength, byte frequency)
{
	if (strength < 1)
		return false;
	if (frequency < 1)
		return false;

	destinationArray[destinationIndex + 0] = (byte)TriggerEffectType::Vibration;
	destinationArray[destinationIndex + 1] = (byte)0x00; // reWASD uses 0x1E here but this is invalid and is ignored save for minor glitches
	destinationArray[destinationIndex + 2] = (byte)0x03; // reWASD uses 0xFF here but the top 6 bits are unused
	destinationArray[destinationIndex + 3] = (byte)0x00;
	destinationArray[destinationIndex + 4] = (byte)0x00;
	destinationArray[destinationIndex + 5] = (byte)0x00;
	destinationArray[destinationIndex + 6] = strength; // reWASD maxes at 0xFF here but the top 2 bits are unused
	destinationArray[destinationIndex + 7] = (byte)0x00;
	destinationArray[destinationIndex + 8] = (byte)0x00;
	destinationArray[destinationIndex + 9] = frequency;
	destinationArray[destinationIndex + 10] = (byte)0x00;
	return true;
}
} // namespace ExtendInput.DataTools. DualSense