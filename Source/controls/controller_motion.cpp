#include "controls/controller_motion.h"

#include <cmath>

#include "controls/controller.h"
#ifndef USE_SDL1
#include "controls/devices/game_controller.h"
#endif
#include "controls/devices/joystick.h"
#include "controls/devices/kbcontroller.h"
#include "controls/game_controls.h"
#include "controls/plrctrls.h"
#include "controls/touch/gamepad.h"
#include "options.h"
#include "utils/log.hpp"

namespace devilution {

bool SimulatingMouseWithPadmapper;

namespace {

void ScaleJoystickAxes(float *x, float *y, float deadzone)
{
	// radial and scaled dead_zone
	// https://web.archive.org/web/20200130014626/www.third-helix.com:80/2013/04/12/doing-thumbstick-dead-zones-right.html
	// input values go from -32767.0...+32767.0, output values are from -1.0 to 1.0;

	if (deadzone == 0) {
		return;
	}
	if (deadzone >= 1.0) {
		*x = 0;
		*y = 0;
		return;
	}

	const float maximum = 32767.0;
	float analogX = *x;
	float analogY = *y;
	float deadZone = deadzone * maximum;

	float magnitude = std::sqrt(analogX * analogX + analogY * analogY);
	if (magnitude >= deadZone) {
		// find scaled axis values with magnitudes between zero and maximum
		float scalingFactor = 1.F / magnitude * (magnitude - deadZone) / (maximum - deadZone);
		analogX = (analogX * scalingFactor);
		analogY = (analogY * scalingFactor);

		// clamp to ensure results will never exceed the max_axis value
		float clampingFactor = 1.F;
		float absAnalogX = std::fabs(analogX);
		float absAnalogY = std::fabs(analogY);
		if (absAnalogX > 1.0 || absAnalogY > 1.0) {
			if (absAnalogX > absAnalogY) {
				clampingFactor = 1.F / absAnalogX;
			} else {
				clampingFactor = 1.F / absAnalogY;
			}
		}
		*x = (clampingFactor * analogX);
		*y = (clampingFactor * analogY);
	} else {
		*x = 0;
		*y = 0;
	}
}

void SetSimulatingMouseWithPadmapper(bool value)
{
	if (SimulatingMouseWithPadmapper == value)
		return;
	SimulatingMouseWithPadmapper = value;
	if (value) {
		LogVerbose("Control: begin simulating mouse with D-Pad");
	} else {
		LogVerbose("Control: end simulating mouse with D-Pad");
	}
}

void SimulateRightStickWithPadmapper(ControllerButtonEvent ctrlEvent)
{
	if (IsAnyOf(ctrlEvent.button, ControllerButton_NONE, ControllerButton_IGNORE))
		return;
	if (!ctrlEvent.up && ctrlEvent.button == SuppressedButton)
		return;

	string_view actionName = sgOptions.Padmapper.ActionNameTriggeredByButtonEvent(ctrlEvent);
	bool upTriggered = actionName == "MouseUp";
	bool downTriggered = actionName == "MouseDown";
	bool leftTriggered = actionName == "MouseLeft";
	bool rightTriggered = actionName == "MouseRight";
	if (!upTriggered && !downTriggered && !leftTriggered && !rightTriggered) {
		if (rightStickX == 0 && rightStickY == 0)
			SetSimulatingMouseWithPadmapper(false);
		return;
	}

	bool upActive = (upTriggered && !ctrlEvent.up) || (!upTriggered && sgOptions.Padmapper.IsActive("MouseUp"));
	bool downActive = (downTriggered && !ctrlEvent.up) || (!downTriggered && sgOptions.Padmapper.IsActive("MouseDown"));
	bool leftActive = (leftTriggered && !ctrlEvent.up) || (!leftTriggered && sgOptions.Padmapper.IsActive("MouseLeft"));
	bool rightActive = (rightTriggered && !ctrlEvent.up) || (!rightTriggered && sgOptions.Padmapper.IsActive("MouseRight"));

	rightStickX = 0;
	rightStickY = 0;
	if (upActive)
		rightStickY += 1.F;
	if (downActive)
		rightStickY -= 1.F;
	if (leftActive)
		rightStickX -= 1.F;
	if (rightActive)
		rightStickX += 1.F;
	SetSimulatingMouseWithPadmapper(true);
}

} // namespace

float leftStickX, leftStickY, rightStickX, rightStickY;
float leftStickXUnscaled, leftStickYUnscaled, rightStickXUnscaled, rightStickYUnscaled;
bool leftStickNeedsScaling, rightStickNeedsScaling;

namespace {

void ScaleJoysticks()
{
	const float rightDeadzone = sgOptions.Controller.fDeadzone;
	const float leftDeadzone = sgOptions.Controller.fDeadzone;

	if (leftStickNeedsScaling) {
		leftStickX = leftStickXUnscaled;
		leftStickY = leftStickYUnscaled;
		ScaleJoystickAxes(&leftStickX, &leftStickY, leftDeadzone);
		leftStickNeedsScaling = false;
	}

	if (rightStickNeedsScaling) {
		rightStickX = rightStickXUnscaled;
		rightStickY = rightStickYUnscaled;
		ScaleJoystickAxes(&rightStickX, &rightStickY, rightDeadzone);
		rightStickNeedsScaling = false;
	}
}

} // namespace

// Updates motion state for mouse and joystick sticks.
bool ProcessControllerMotion(const SDL_Event &event, ControllerButtonEvent ctrlEvent)
{
#ifndef USE_SDL1
	GameController *const controller = GameController::Get(event);
	if (controller != nullptr && devilution::GameController::ProcessAxisMotion(event)) {
		ScaleJoysticks();
		SetSimulatingMouseWithPadmapper(false);
		return true;
	}
#endif
	Joystick *const joystick = Joystick::Get(event);
	if (joystick != nullptr && devilution::Joystick::ProcessAxisMotion(event)) {
		ScaleJoysticks();
		SetSimulatingMouseWithPadmapper(false);
		return true;
	}
#if HAS_KBCTRL == 1
	if (ProcessKbCtrlAxisMotion(event)) {
		SetSimulatingMouseWithPadmapper(false);
		return true;
	}
#endif
	SimulateRightStickWithPadmapper(ctrlEvent);
	return false;
}

AxisDirection GetLeftStickOrDpadDirection(bool usePadmapper)
{
	const float stickX = leftStickX;
	const float stickY = leftStickY;

	AxisDirection result { AxisDirectionX_NONE, AxisDirectionY_NONE };

	bool isUpPressed = stickY >= 0.5;
	bool isDownPressed = stickY <= -0.5;
	bool isLeftPressed = stickX <= -0.5;
	bool isRightPressed = stickX >= 0.5;

	if (usePadmapper) {
		isUpPressed |= sgOptions.Padmapper.IsActive("MoveUp");
		isDownPressed |= sgOptions.Padmapper.IsActive("MoveDown");
		isLeftPressed |= sgOptions.Padmapper.IsActive("MoveLeft");
		isRightPressed |= sgOptions.Padmapper.IsActive("MoveRight");
	} else if (!SimulatingMouseWithPadmapper) {
		isUpPressed |= IsControllerButtonPressed(ControllerButton_BUTTON_DPAD_UP);
		isDownPressed |= IsControllerButtonPressed(ControllerButton_BUTTON_DPAD_DOWN);
		isLeftPressed |= IsControllerButtonPressed(ControllerButton_BUTTON_DPAD_LEFT);
		isRightPressed |= IsControllerButtonPressed(ControllerButton_BUTTON_DPAD_RIGHT);
	}

#ifndef USE_SDL1
	if (ControlMode == ControlTypes::VirtualGamepad) {
		isUpPressed |= VirtualGamepadState.isActive && VirtualGamepadState.directionPad.isUpPressed;
		isDownPressed |= VirtualGamepadState.isActive && VirtualGamepadState.directionPad.isDownPressed;
		isLeftPressed |= VirtualGamepadState.isActive && VirtualGamepadState.directionPad.isLeftPressed;
		isRightPressed |= VirtualGamepadState.isActive && VirtualGamepadState.directionPad.isRightPressed;
	}
#endif

	if (isUpPressed) {
		result.y = AxisDirectionY_UP;
	} else if (isDownPressed) {
		result.y = AxisDirectionY_DOWN;
	}

	if (isLeftPressed) {
		result.x = AxisDirectionX_LEFT;
	} else if (isRightPressed) {
		result.x = AxisDirectionX_RIGHT;
	}

	return result;
}

} // namespace devilution
