#include "dimgui/Application.h"
#include "InputHelpers.h"
#include "JSMVariable.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "implot.h"
#include "SettingsManager.h"
#include "CmdRegistry.h"
#include "InputHelpers.h"
#include "JslWrapper.h"
#include <regex>
#include <span>

#define ImTextureID SDL_Texture

extern CmdRegistry commandRegistry;
extern vector<JSMButton> mappings;

using namespace magic_enum;
using namespace ImGui;
#define EndMenu() ImGui::EndMenu(); // Resolve symbol conflict with windows

InputSelector Application::BindingTab::_inputSelector;
AppIf* Application::BindingTab::_app = nullptr;

ImVec2 operator+(ImVec2 lhs, ImVec2 rhs)
{
	return ImVec2{ lhs.x + rhs.x, lhs.y + rhs.y };
}

ButtonID operator+(ButtonID lhs, int rhs)
{
	auto newEnum = enum_cast<ButtonID>(enum_integer(lhs) + rhs);
	return newEnum ? *newEnum : ButtonID::INVALID;
}

Application::Application(JslWrapper* jsl)
  : _jsl(jsl)
{
	BindingTab::_app = this;
	_tabs.try_emplace(ButtonID::NONE, "Base Layer", jsl);
}

void Application::HelpMarker(string_view cmd)
{
	SameLine();
	TextDisabled("(?)");
	if (IsItemHovered())
	{
		BeginTooltip();
		PushTextWrapPos(GetFontSize() * 35.0f);
		TextUnformatted(commandRegistry.getHelp(cmd).data());
		PopTextWrapPos();
		EndTooltip();
	}
}

template<typename T>
void Application::drawCombo(SettingID stg, ButtonID chord, ImGuiComboFlags flags, bool label)
{
	JSMVariable<T>* variable = nullptr;
	T value;
	auto setting = SettingsManager::get<T>(stg);
	if (setting)
	{
		variable = setting->atChord(chord);
		value = variable ? variable->value() : setting->value();
	}
	else
	{
		variable = SettingsManager::getV<T>(stg);
		value = variable->value();
	}

	stringstream name;
	if (!label)
		name << "##";
	name << enum_name(stg);
	stringstream selectedEnum;
	variable ? selectedEnum << value : selectedEnum << '[' << value << ']';
	if (BeginCombo(name.str().c_str(), selectedEnum.str().c_str(), flags))
	{
		for (auto [enumVal, enumStr] : enum_entries<T>())
		{
			if (enumVal == T::INVALID)
				continue;

			bool disabled = false;
			if constexpr (is_same_v<T, TriggerMode>)
			{
				if (enumVal == TriggerMode::X_LT || enumVal == TriggerMode::X_RT)
				{
					auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER)->value();
					disabled = virtual_controller == ControllerScheme::NONE;
					if (virtual_controller == ControllerScheme::DS4)
					{
						enumStr = enumVal == TriggerMode::X_LT ? "PS_L2" : "PS_R2";
					}
				}
			}
			if constexpr (is_same_v<T, StickMode>)
			{
				auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER)->value();
				if (enumVal >= StickMode::LEFT_STICK && enumVal <= StickMode::RIGHT_STEER_X)
				{
					disabled = virtual_controller == ControllerScheme::NONE;
				}
				if (setting && setting->_id != SettingID::MOTION_STICK_MODE &&
				  (enumVal == StickMode::LEFT_STEER_X || enumVal == StickMode::RIGHT_STEER_X))
				{
					// steer stick mode is only valid for the motion stick, don't add to other combo boxes
					continue;
				}
				if (enumVal == StickMode::INNER_RING || enumVal == StickMode::OUTER_RING)
				{
					// Don't add legacy commands to UI. Use setting "XXXX_RING_MODE = [INNER|OUTER]" instead
					continue;
				}
			}
			if constexpr (is_same_v<T, GyroOutput>)
			{
				auto virtual_controller = SettingsManager::getV<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER)->value();
				if (enumVal == GyroOutput::PS_MOTION)
				{
					disabled = virtual_controller == ControllerScheme::DS4;
				}
			}
			if (disabled)
				BeginDisabled();
			bool isSelected = enumVal == value;
			if (Selectable(enumStr.data(), isSelected))
			{
				if (!variable)
					variable = &setting->createChord(chord);

				variable->set(enumVal);
			}

			// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
			if (isSelected)
				SetItemDefaultFocus();
			if (disabled)
				EndDisabled();
		}
		EndCombo();
	}
}

void Application::BindingTab::drawButton(ButtonID btn, ImVec2 size)
{
	std::stringstream labelID;
	auto mapping = mappings[enum_integer(btn)].atChord(_chord);
	string desc;
	if (mapping)
		desc = mapping->value().description();
	else
	{
		stringstream ss;
		ss << '[' << mappings[enum_integer(btn)].value().description() << ']';
		desc = ss.str();
	}

	for (auto pos = desc.find("and"); pos != string::npos; pos = desc.find("and", pos))
	{
		desc.insert(pos, "\n");
		pos += 2;
	}
	labelID << desc;                    // Button label to display
	labelID << "###" << enum_name(btn); // Button ID for ImGui
	if (Button(labelID.str().data(), size))
	{
		_showPopup = btn;
	}

	string_view label = mapping ? mapping->label().data() : string_view("Set a chorded button");
	if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
	{
		if (!label.empty())
		{
			SetTooltip(label.data());
		}
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
		{
			_app->createChord(btn);
		}
	}
	if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight))
	{
		if (MenuItem("Set", "Left Click"))
		{
			_showPopup = btn;
		}
		if (MenuItem("Clear", ""))
		{
			if (mapping)
			{
				mapping->set(Mapping::NO_MAPPING);
				mappings[enum_integer(btn)].processChordRemoval(_chord, mapping);
				mappings[enum_integer(btn)].updateLabel("");
			}
		}
		if (MenuItem("Set Double Press", "", false, false))
		{
			auto simMap = mappings[enum_integer(btn)].atSimPress(btn);
			// TODO: set simMap to some value using the input selector and create a display for it somewhere in the UI
		}
		if (MenuItem("Chord this button", "Middle Click"))
		{
			_app->createChord(btn);
		}
		if (BeginMenu("Simultaneous Press with"))
		{
			for (auto pair = enum_entries<ButtonID>().begin(); pair->first < ButtonID::SIZE; ++pair)
			{

				if (pair->first != btn)
				{
					if (MenuItem(pair->second.data(), nullptr, false, false))
					{
						auto simMap = mappings[enum_integer(btn)].atSimPress(pair->first);
						// TODO: set simMap to some value using the input selector and create a display for it somewhere in the UI
					}
				}
			}
			EndMenu();
		}
		drawAnyFloat(SettingID::HOLD_PRESS_TIME, true);
		drawAnyFloat(SettingID::TURBO_PERIOD, true);
		drawAnyFloat(SettingID::DBL_PRESS_WINDOW, true);
		drawAnyFloat(SettingID::SIM_PRESS_WINDOW, true);
		if (btn == ButtonID::LEAN_LEFT || btn == ButtonID::LEAN_RIGHT)
		{
			drawAnyFloat(SettingID::LEAN_THRESHOLD, true);
		}
		EndPopup();
	}
}

void Application::init()
{
	HideConsole();
	// Setup window
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY /*| SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_TRANSPARENT*/);
	window = SDL_CreateWindow("JoyShockMapper", 1280, 720, window_flags);

	// Setup SDL_Renderer instance
	renderer = SDL_CreateRenderer(window, nullptr); // -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	if (!renderer || !window)
		exit(0);
	// SDL_RendererInfo info;
	// SDL_GetRendererInfo(renderer, &info);
	// SDL_Log("Current SDL_Renderer: %s", info.name);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = GetIO();
	(void)io;
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	StyleColorsDark();
	// StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer3_Init(renderer);

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	// io.Fonts->AddFontDefault();
	// io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	// ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	// IM_ASSERT(font != NULL);

	GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Add window transparency
	//MakeWindowTransparent(window, RGB(Uint8(clear_color.x * 255), Uint8(clear_color.y * 255), Uint8(clear_color.z * 255)), Uint8(clear_color.w * 255));

	// int (SDLCALL * SDL_EventFilter) (void *userdata, SDL_Event * event);
	SDL_SetEventFilter([](void* userdata, SDL_Event* evt) -> bool
	  { return evt->type >= SDL_EVENT_JOYSTICK_AXIS_MOTION && evt->type <= SDL_EVENT_GAMEPAD_SENSOR_UPDATE ? false : true; },
	  nullptr);
}

void Application::cleanUp()
{
	// Cleanup
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	DestroyContext();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}

void Application::draw(SDL_Gamepad* controller)
{
	// Poll and handle events (inputs, window resize, etc.)
	// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
	// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
	// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
	// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
	SDL_Event event;
	bool done = false;
	while (!done && SDL_PollEvent(&event) != 0)
	{
		ImGui_ImplSDL3_ProcessEvent(&event);
		if (event.type == SDL_EVENT_QUIT)
			done = true;
		if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
			done = true;
	}

	if (done)
		WriteToConsole("QUIT");

	auto& style = GetStyle();
	style.Alpha = 1.0f;

	// Start the Dear ImGui frame
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	PushStyleColor(ImGuiCol_WindowBg, { 0.f, 0.f, 0.f, 1.f });
	NewFrame();

	DockSpaceOverViewport(ImGui::GetWindowDockID(), GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	PopStyleColor();

	// 1. Show the big demo window (Most of the sample code is in ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	if (show_demo_window)
		ShowDemoWindow(&show_demo_window);

	if (show_plot_demo_window)
		ImPlot::ShowDemoWindow(&show_plot_demo_window);

	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
	// static float f = 0.0f;
	using namespace ImGui;
	static bool openUsingTheGui = false;
	if (BeginMainMenuBar())
	{
		if (BeginMenu("File"))
		{
			if (MenuItem("New", "CTRL+N", nullptr, false))
			{
			}
			if (MenuItem("Open", "CTRL+O", nullptr, false))
			{
			}
			if (MenuItem("Save", "CTRL+S", nullptr, false))
			{
			}
			if (MenuItem("Save As...", "SHIFT+CTRL+S", nullptr, false))
			{
			}
			Separator();
			if (MenuItem("On Startup"))
			{
				WriteToConsole("OnStartup.txt");
			}
			if (MenuItem("On Reset"))
			{
				WriteToConsole("OnReset.txt");
			}
			if (BeginMenu("Templates"))
			{
				string gyroConfigsFolder{ GYRO_CONFIGS_FOLDER() };
				for (auto file : ListDirectory(gyroConfigsFolder.c_str()))
				{
					string fullPathName = ".\\GyroConfigs\\" + file;
					auto noext = file.substr(0, file.find_last_of('.'));
					if (MenuItem(noext.c_str()))
					{
						WriteToConsole(string(fullPathName.begin(), fullPathName.end()));
						SettingsManager::getV<Switch>(SettingID::AUTOLOAD)->set(Switch::OFF);
					}
				}
				EndMenu();
			}
			if (BeginMenu("AutoLoad"))
			{
				string autoloadFolder{ AUTOLOAD_FOLDER() };
				for (auto file : ListDirectory(autoloadFolder.c_str()))
				{
					string fullPathName = ".\\AutoLoad\\" + file;
					auto noext = file.substr(0, file.find_last_of('.'));
					if (MenuItem(noext.c_str()))
					{
						WriteToConsole(string(fullPathName.begin(), fullPathName.end()));
						SettingsManager::getV<Switch>(SettingID::AUTOLOAD)->set(Switch::OFF);
					}
				}
				EndMenu();
			}
			Separator();
			if (MenuItem("Quit"))
			{
				WriteToConsole("QUIT");
			}
			EndMenu();
		}
		if (BeginMenu("Commands"))
		{
			if (MenuItem("Reconnect Controllers"))
			{
				WriteToConsole("RECONNECT_CONTROLLERS");
			}
			HelpMarker("RECONNECT_CONTROLLERS");
			
			if (MenuItem("Reset Mappings"))
			{
				WriteToConsole("RESET_MAPPINGS");
			}
			HelpMarker("RESET_MAPPINGS");

			Separator();

			static float duration = 3.f;
			SliderFloat("Calibration duration", &duration, 0.5f, 5.0f);
			if (MenuItem("Calibrate All Controllers"))
			{
				auto t = std::thread([]()
				  {
						WriteToConsole("RESTART_GYRO_CALIBRATION");
						Sleep(int32_t(duration * 1000.f)); // ms
						WriteToConsole("FINISH_GYRO_CALIBRATION"); });
				t.detach();
			}
			HelpMarker("RESTART_GYRO_CALIBRATION");

			auto autocalibrateSetting = SettingsManager::getV<Switch>(SettingID::AUTO_CALIBRATE_GYRO);
			bool value = autocalibrateSetting->value() == Switch::ON;
			if (Checkbox("AUTO_CALIBRATE_GYRO", &value))
			{
				autocalibrateSetting->set(value ? Switch::ON : Switch::OFF);
			}
			HelpMarker("AUTO_CALIBRATE_GYRO");

			if (MenuItem("Calculate Real World Calibration"))
			{
				WriteToConsole("CALCULATE_REAL_WORLD_CALIBRATION");
			}
			HelpMarker("CALCULATE_REAL_WORLD_CALIBRATION");

			if (MenuItem("Set Motion Stick Center"))
			{
				WriteToConsole("SET_MOTION_STICK_NEUTRAL");
			}
			HelpMarker("SET_MOTION_STICK_NEUTRAL");

			if (MenuItem("Calibrate adaptive Triggers"))
			{
				WriteToConsole("CALIBRATE_TRIGGERS");
				ShowConsole();
			}
			HelpMarker("CALIBRATE_TRIGGERS");

			Separator();

			static bool whitelistAdd = false;
			if (Checkbox("Add to whitelister application", &whitelistAdd))
			{
				if (whitelistAdd)
					WriteToConsole("WHITELIST_ADD");
				else
					WriteToConsole("WHITELIST_REMOVE");
			}
			HelpMarker("WHITELIST_ADD");

			if (MenuItem("Show whitelister"))
			{
				WriteToConsole("WHITELIST_SHOW");
			}
			HelpMarker("WHITELIST_SHOW");
			EndMenu();
		}
		if (BeginMenu("Settings"))
		{
			// TODO: HIDE_MINIMIZED
			auto tickTime = SettingsManager::getV<float>(SettingID::TICK_TIME);
			float tt = tickTime->value();
			InputFloat(enum_name(SettingID::TICK_TIME).data(), &tt, 0.f, 0.f, "%.3f");
			if (IsItemDeactivatedAfterEdit())
			{
				tickTime->set(tt);
			}
			HelpMarker(enum_name(SettingID::TICK_TIME).data());

			string dir = SettingsManager::getV<PathString>(SettingID::JSM_DIRECTORY)->value();
			dir.resize(256, '\0');
			InputText("JSM_DIRECTORY", dir.data(), dir.size(), ImGuiInputTextFlags_EnterReturnsTrue);
			if (IsItemDeactivatedAfterEdit())
			{
				dir.resize(strlen(dir.c_str()));
				SettingsManager::getV<PathString>(SettingID::JSM_DIRECTORY)->set(dir);
			}
			HelpMarker("JSM_DIRECTORY");

			drawCombo<ControllerScheme>(SettingID::VIRTUAL_CONTROLLER, ButtonID::NONE, 0, "VIRTUAL_CONTROLLER");
			HelpMarker("VIRTUAL_CONTROLLER");

			auto rumbleEnable = SettingsManager::getV<Switch>(SettingID::RUMBLE);
			bool value = rumbleEnable->value() == Switch::ON;
			if (Checkbox("RUMBLE", &value))
			{
				rumbleEnable->set(value ? Switch::ON : Switch::OFF);
			}
			HelpMarker("RUMBLE");

			auto adaptiveTriggers = SettingsManager::get<Switch>(SettingID::ADAPTIVE_TRIGGER);
			value = rumbleEnable->value() == Switch::ON;
			if (Checkbox("ADAPTIVE_TRIGGER", &value))
			{
				adaptiveTriggers->set(value ? Switch::ON : Switch::OFF);
			}
			HelpMarker("ADAPTIVE_TRIGGER");

			auto autoload = SettingsManager::getV<Switch>(SettingID::AUTOLOAD);
			bool al = autoload->value() == Switch::ON;
			if (Checkbox(enum_name(SettingID::AUTOLOAD).data(), &al))
			{
				autoload->set(al ? Switch::ON : Switch::OFF);
			}
			HelpMarker(enum_name(SettingID::AUTOLOAD).data());

			float rwc = *SettingsManager::get<float>(SettingID::REAL_WORLD_CALIBRATION);
			InputFloat("REAL_WORLD_CALIBRATION", &rwc, 0.f, 0.f, "%.3f", ImGuiInputTextFlags_None);
			if (IsItemDeactivatedAfterEdit())
			{
				SettingsManager::get<float>(SettingID::REAL_WORLD_CALIBRATION)->set(rwc);
			}
			HelpMarker(enum_name(SettingID::REAL_WORLD_CALIBRATION).data());

			float igs = *SettingsManager::get<float>(SettingID::IN_GAME_SENS);
			InputFloat("IN_GAME_SENS", &rwc, 0.f, 0.f, "%.3f", ImGuiInputTextFlags_None);
			if (IsItemDeactivatedAfterEdit())
			{
				SettingsManager::get<float>(SettingID::IN_GAME_SENS)->set(igs);
			}
			HelpMarker(enum_name(SettingID::IN_GAME_SENS).data());

			EndMenu();
		}
		if (BeginMenu("Debug"))
		{
			Checkbox("Show ImGui demo", &show_demo_window);
			Checkbox("Show ImPlot demo", &show_plot_demo_window);
			MenuItem("Record a bug", nullptr, false, false);
			EndMenu();
		}
		if (BeginMenu("Help"))
		{
			if (MenuItem("Using the GUI"))
			{
				openUsingTheGui = true;
			}
			MenuItem("Read Me", nullptr, false, false);
			MenuItem("Check For Updates", nullptr, false, false);
			MenuItem("About", nullptr, false, false);
			EndMenu();
		}
		EndMainMenuBar();
	}

	if (openUsingTheGui)
	{
		OpenPopup("Using The GUI");
		openUsingTheGui = false;
	}
	if (BeginPopup("Using The GUI", ImGuiWindowFlags_Modal))
	{
		BulletText("Left click to change the mapping or setting value");
		BulletText("Right click to see more settings related to the button or setting");
		BulletText("Middle Click to open a layer when the button is pressed");
		if (Button("OK"))
		{
			CloseCurrentPopup();
		}
		EndPopup();
	}

	ImVec2 renderingAreaPos;
	ImVec2 renderingAreaSize;
	SetNextWindowBgAlpha(0.0f);
	Begin("MainWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | /*ImGuiWindowFlags_NoMouseInputs |*/ ImGuiWindowFlags_NoTitleBar);

	BeginTabBar("BindingsTab");
	// Draw all existing tabs
	for (auto tabIt = _tabs.begin() ; tabIt != _tabs.end() ;)
	{
		auto close = tabIt->second.draw(renderingAreaPos, renderingAreaSize) == false;
		if (close && tabIt->first != ButtonID::NONE)
		{
			tabIt = _tabs.erase(tabIt);
		}
		else
			tabIt++;
	}
	// Create and draw a new tab if one was requested through the GUI
	if (_newTab != ButtonID::NONE && _tabs.find(_newTab) == _tabs.end())
	{
		stringstream ss;
		ss << "Chorded " << _newTab;
		auto [tab, inserted] = _tabs.try_emplace(_newTab, ss.str(), _jsl, _newTab);
		if (inserted)
		{
			tab->second.draw(renderingAreaPos, renderingAreaSize, true);
		}
	}
	_newTab = ButtonID::NONE;
	// Create and draw new tabs if one was added through command line
	for (auto& mapping : mappings)
	{
		for (auto chords = mapping.getChords() ; *chords ; ++*chords)
		{
			auto chord = **chords;
			if (_tabs.find(chord) == _tabs.end())
			{
				stringstream ss;
				ss << "Chorded " << chord;
				auto [tab, inserted] = _tabs.try_emplace(chord, ss.str(), _jsl, chord);
				if (inserted)
				{
					tab->second.draw(renderingAreaPos, renderingAreaSize);
				}
			}
		}
	}
	for (auto& [_, settingBase] : SettingsManager::getSettings())
	{
		for (auto chords = settingBase->getChords(); *chords; ++*chords)
		{
			auto chord = **chords;
			if (_tabs.find(chord) == _tabs.end())
			{
				stringstream ss;
				ss << "Chorded " << chord;
				auto [tab, inserted] = _tabs.try_emplace(chord, ss.str(), _jsl, chord);
				if (inserted)
				{
					tab->second.draw(renderingAreaPos, renderingAreaSize);
				}
			}
		}
	}
	//TODO: delete chord tabs that have no modeshift or chords and are not in focus.
	EndTabBar(); // BindingsTab
	End();       // MainWindow

	SDL_Rect bgDims{
		renderingAreaPos.x,
		renderingAreaPos.y,
		renderingAreaSize.x,
		renderingAreaSize.y
	};

	// Rendering
	SDL_RenderClear(renderer);
	Render();
	ImGui_ImplSDLRenderer3_RenderDrawData(GetDrawData(), renderer);
	SDL_RenderPresent(renderer);
}

void Application::createChord(ButtonID chord)
{
	_newTab = chord;
}

Application::BindingTab::BindingTab(string_view name, JslWrapper* jsl, ButtonID chord)
  : _name(name)
  , _chord(chord)
  , _jsl(jsl)
{
}

void Application::BindingTab::drawLabel(ButtonID btn)
{
	AlignTextToFramePadding();
	Text(enum_name(btn).data());
	if (IsItemHovered(ImGuiHoveredFlags_DelayNormal))
	{
		SetTooltip(commandRegistry.getHelp(enum_name(btn)).data());
	}
};
void Application::BindingTab::drawLabel(SettingID stg)
{
	AlignTextToFramePadding();
	Text(enum_name(stg).data());
	if (IsItemHovered(ImGuiHoveredFlags_DelayNormal))
	{
		SetTooltip(commandRegistry.getHelp(enum_name(stg)).data());
	}
};
void Application::BindingTab::drawLabel(string_view cmd)
{
	AlignTextToFramePadding();
	Text(cmd.data());
	if (IsItemHovered(ImGuiHoveredFlags_DelayNormal))
	{
		SetTooltip(commandRegistry.getHelp(cmd).data());
	}
}

void Application::BindingTab::drawAnyFloat(SettingID stg, bool labeled)
{
	JSMVariable<float>* variable = nullptr;
	float value;
	auto setting = SettingsManager::get<float>(stg);
	if (setting)
	{
		variable = setting->atChord(_chord);
		value = variable ? variable->value() : setting->value();
	}
	else
	{
		variable = SettingsManager::getV<float>(stg);
		value = variable->value();
	}

	stringstream ss;
	if (!labeled)
		ss << "##";
	ss << enum_name(stg);
	InputFloat(ss.str().data(), &value, 0.f, 0.f, variable ? "%.3f" : "[%.3f]", ImGuiInputTextFlags_None);
	if (IsItemDeactivatedAfterEdit())
	{
		if (!variable)
			variable = &setting->createChord(_chord);

		variable->set(value);
	}
	if (labeled && IsItemHovered(ImGuiHoveredFlags_DelayNormal))
	{
		SetTooltip(commandRegistry.getHelp(enum_name(stg)).data());
	}
}

void Application::BindingTab::drawPercentFloat(SettingID stg, bool labeled)
{
	JSMVariable<float>* variable = nullptr;
	float value;
	auto setting = SettingsManager::get<float>(stg);
	if (setting)
	{
		variable = setting->atChord(_chord);
		value = variable ? variable->value() : setting->value();
	}
	else
	{
		variable = SettingsManager::getV<float>(stg);
		value = variable->value();
	}

	stringstream ss;
	if (!labeled)
		ss << "##";
	ss << enum_name(stg);
	SliderFloat(ss.str().data(), &value, 0.f, 1.f, variable ? "%.2f" : "[%.2f]", ImGuiInputTextFlags_None);
	if (IsItemDeactivatedAfterEdit())
	{
		if (!variable)
			variable = &setting->createChord(_chord);

		variable->set(value);
	}
}

template<typename T>
T Application::BindingTab::getSettingValue(SettingID stg, JSMVariable<T>** outVariable)
{
	T value;
	JSMVariable<T> *variable = nullptr;
	auto setting = SettingsManager::get<T>(stg);
	if (setting)
	{
		variable = setting->atChord(_chord);
		value = variable ? variable->value() : setting->value();
	}
	else
	{
		variable = SettingsManager::getV<T>(stg);
		value = variable->value();
	}
	if (outVariable)
	{
		*outVariable = variable;
	}
	return value;
}

void Application::BindingTab::drawAny2Floats(SettingID stg, bool labeled)
{

	JSMVariable<FloatXY>* variable = nullptr;
	FloatXY value;
	auto setting = SettingsManager::get<FloatXY>(stg);
	if (setting)
	{
		variable = setting->atChord(_chord);
		value = variable ? variable->value() : setting->value();
	}
	else
	{
		variable = SettingsManager::getV<FloatXY>(stg);
		value = variable->value();
	}

	stringstream ss;
	if (!labeled)
		ss << "##";
	ss << enum_name(stg);
	InputFloat2(ss.str().data(), &value.first, variable ? "%.0f" : "[%.0f]", ImGuiInputTextFlags_None);
	if (IsItemDeactivatedAfterEdit())
	{
		if (!variable)
			variable = &setting->createChord(_chord);

		variable->set(value);
	}
}

bool Application::BindingTab::draw(ImVec2& renderingAreaPos, ImVec2& renderingAreaSize, bool setFocus)
{
	static constexpr float barSize = 75.f;
	ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
	if (ButtonID::NONE == _chord)
		flags |= ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;
	if (setFocus)
		flags |= ImGuiTabItemFlags_SetSelected;
	bool open = true;
	if (ImGui::BeginTabItem(_name.data(), &open, flags))
	{
		// SliderFloat("barwidth", &barSize, 20.f, 500.f);
		auto mainWindowSize = ImGui::GetContentRegionAvail();
		// static int sizingPolicy = 0;
		// int sizingPolicies[] = { ImGuiTableFlags_SizingFixedFit, ImGuiTableFlags_SizingFixedSame, ImGuiTableFlags_SizingStretchProp, ImGuiTableFlags_SizingStretchSame };
		// Combo("Sizing policy", &sizingPolicy, "FixedFit\0FixedSame\0StretchProp\0StretchSame");

		// Left
		BeginChild("Left Bindings", { mainWindowSize.x * 1.f / 5.f, mainWindowSize.y - barSize }, true, ImGuiChildFlags_None);
		if (BeginTable("LeftTable", 2, ImGuiTableFlags_SizingStretchSame))
		{
			TableNextRow();
			TableNextColumn();
			drawLabel("Top buttons");
			TableNextColumn();

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::ZLF);
			TableNextColumn();
			bool disabled = getSettingValue<TriggerMode>(SettingID::ZL_MODE) == TriggerMode::NO_FULL;
			if (disabled)
				BeginDisabled();
			drawButton(ButtonID::ZLF);
			if (disabled)
				EndDisabled();

			TableNextRow();
			TableNextColumn();
			drawLabel(SettingID::ZL_MODE);
			TableNextColumn();
			drawCombo<TriggerMode>(SettingID::ZL_MODE, _chord);
			if (IsItemClicked(ImGuiMouseButton_Right))
			{
				_stickConfigPopup = SettingID::ZL_MODE;
			}

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::ZL);
			TableNextColumn();
			drawButton(ButtonID::ZL);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::L);
			TableNextColumn();
			drawButton(ButtonID::L);

			TableNextRow();
			TableNextColumn();
			drawLabel("Face buttons");

			TableNextRow();
			TableNextColumn();
			drawLabel("-");
			TableNextColumn();
			drawButton(ButtonID::MINUS);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::UP);
			TableNextColumn();
			drawButton(ButtonID::UP);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::LEFT);
			TableNextColumn();
			drawButton(ButtonID::LEFT);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::RIGHT);
			TableNextColumn();
			drawButton(ButtonID::RIGHT);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::DOWN);
			TableNextColumn();
			drawButton(ButtonID::DOWN);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::CAPTURE);
			disabled = getSettingValue<TriggerMode>(SettingID::TOUCHPAD_DUAL_STAGE_MODE) == TriggerMode::NO_FULL;
			if (disabled)
				BeginDisabled();
			TableNextColumn();
			drawButton(ButtonID::CAPTURE);
			if (disabled)
				EndDisabled();

			TableNextRow();
			TableNextColumn();
			drawLabel("Back buttons");

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::LSL);
			TableNextColumn();
			drawButton(ButtonID::LSL);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::LSR);
			TableNextColumn();
			drawButton(ButtonID::LSR);

			TableNextRow();
			TableNextColumn();
			drawLabel("Left stick");

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::L3);
			TableNextColumn();
			drawButton(ButtonID::L3);

			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::LRING);
			SameLine();
			drawCombo<RingMode>(SettingID::LEFT_RING_MODE, _chord, ImGuiComboFlags_NoPreview);
			if (IsItemHovered())
				SetTooltip(commandRegistry.getHelp(enum_name(SettingID::LEFT_RING_MODE)).data());
			TableNextColumn();
			drawButton(ButtonID::LRING);

			TableNextRow();
			TableNextColumn();
			drawLabel(SettingID::LEFT_STICK_MODE);
			TableNextColumn();
			drawCombo<StickMode>(SettingID::LEFT_STICK_MODE, _chord);
			if (IsItemClicked(ImGuiMouseButton_Right))
			{
				_stickConfigPopup = SettingID::RIGHT_STICK_MODE;
			}
			// SameLine();
			// if (Button("..."))
			//	_stickConfigPopup = SettingID::LEFT_STICK_MODE;
			// if (IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			//	SetTooltip("More left stick settings");

			StickMode leftStickMode = getSettingValue<StickMode>(SettingID::LEFT_STICK_MODE);
			if (leftStickMode == StickMode::NO_MOUSE || leftStickMode == StickMode::OUTER_RING || leftStickMode == StickMode::INNER_RING)
			{
				TableNextRow();
				TableNextColumn();
				drawLabel(ButtonID::LUP);
				TableNextColumn();
				drawButton(ButtonID::LUP);

				TableNextRow();
				TableNextColumn();
				drawLabel(ButtonID::LLEFT);
				TableNextColumn();
				drawButton(ButtonID::LLEFT);

				TableNextRow();
				TableNextColumn();
				drawLabel(ButtonID::LRIGHT);
				TableNextColumn();
				drawButton(ButtonID::LRIGHT);

				TableNextRow();
				TableNextColumn();
				drawLabel(ButtonID::LDOWN);
				TableNextColumn();
				drawButton(ButtonID::LDOWN);
			}
			else if (leftStickMode == StickMode::AIM)
			{
				TableNextRow();
				TableNextColumn();
				drawLabel(SettingID::STICK_SENS);
				TableNextColumn();
				drawAny2Floats(SettingID::STICK_SENS);
			}
			else if (leftStickMode == StickMode::FLICK || leftStickMode == StickMode::FLICK_ONLY || leftStickMode == StickMode::ROTATE_ONLY)
			{
				TableNextRow();
				TableNextColumn();
				drawLabel(SettingID::FLICK_STICK_OUTPUT);
				TableNextColumn();
				drawCombo<GyroOutput>(SettingID::FLICK_STICK_OUTPUT, _chord);
			}
			else if (leftStickMode == StickMode::MOUSE_AREA || leftStickMode == StickMode::MOUSE_RING)
			{
				TableNextRow();
				TableNextColumn();
				drawLabel(SettingID::MOUSE_RING_RADIUS);
				TableNextColumn();
				drawAnyFloat(SettingID::MOUSE_RING_RADIUS);
			}
			else if (leftStickMode == StickMode::SCROLL_WHEEL)
			{
				TableNextRow();
				TableNextColumn();
				drawLabel(ButtonID::LLEFT);
				TableNextColumn();
				drawButton(ButtonID::LLEFT);

				TableNextRow();
				TableNextColumn();
				drawLabel(ButtonID::LRIGHT);
				TableNextColumn();
				drawButton(ButtonID::LRIGHT);
			}
			EndTable();
		}
		EndChild();

		ImGui::SameLine();

		// Right
		BeginGroup();
		BeginChild("Top buttons", { mainWindowSize.x * 3.f / 5.f, barSize }, true);
		if (BeginTable("TopTable", 6, ImGuiTableFlags_SizingStretchSame))
		{
			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::TOUCH);
			SameLine();
			drawCombo<TriggerMode>(SettingID::TOUCHPAD_DUAL_STAGE_MODE, _chord, ImGuiComboFlags_NoPreview);
			if (IsItemHovered())
				SetTooltip(commandRegistry.getHelp(enum_name(SettingID::TOUCHPAD_DUAL_STAGE_MODE)).data());
			TableSetColumnIndex(4);
			drawLabel(ButtonID::MIC);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::TOUCH);
			TableSetColumnIndex(4);
			drawButton(ButtonID::MIC);

			EndTable();
		}
		EndChild();

		renderingAreaSize = { mainWindowSize.x * 3.f / 5.f, GetContentRegionAvail().y - barSize };
		BeginChild("Rendering window", renderingAreaSize, true);
		renderingAreaPos = ImGui::GetWindowPos();
		EndChild(); // Top buttons
		EndGroup();

		SameLine();
		BeginChild("Right Bindings", { 0.f, mainWindowSize.y - barSize }, true);
		if (BeginTable("RightTable", 2, ImGuiTableFlags_SizingStretchSame))
		{
			TableNextRow();
			TableNextColumn();
			drawLabel("Top buttons");

			TableNextRow();
			TableNextColumn();
			bool disabled = getSettingValue<TriggerMode>(SettingID::ZL_MODE) == TriggerMode::NO_FULL;
			if (disabled)
				BeginDisabled();
			drawButton(ButtonID::ZRF);
			if (disabled)
				EndDisabled();
			TableNextColumn();
			drawLabel(ButtonID::ZRF);

			TableNextRow();
			TableNextColumn();
			drawCombo<TriggerMode>(SettingID::ZR_MODE, _chord);
			if (IsItemClicked(ImGuiMouseButton_Right))
			{
				_stickConfigPopup = SettingID::ZL_MODE;
			}
			TableNextColumn();
			drawLabel(SettingID::ZR_MODE);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::ZR);
			TableNextColumn();
			drawLabel(ButtonID::ZR);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::R);
			TableNextColumn();
			drawLabel(ButtonID::R);

			TableNextRow();
			TableNextColumn();
			drawLabel("Face buttons");

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::PLUS);
			TableNextColumn();
			drawLabel("+");

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::N);
			TableNextColumn();
			drawLabel(ButtonID::N);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::E);
			TableNextColumn();
			drawLabel(ButtonID::E);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::W);
			TableNextColumn();
			drawLabel(ButtonID::W);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::S);
			TableNextColumn();
			drawLabel(ButtonID::S);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::HOME);
			TableNextColumn();
			drawLabel(ButtonID::HOME);

			TableNextRow();
			TableNextColumn();
			drawLabel("Back buttons");

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::RSR);
			TableNextColumn();
			drawLabel(ButtonID::RSR);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::RSL);
			TableNextColumn();
			drawLabel(ButtonID::RSL);

			TableNextRow();
			TableNextColumn();
			drawLabel("Right Stick");

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::R3);
			TableNextColumn();
			drawLabel(ButtonID::R3);

			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::RRING);
			TableNextColumn();
			drawLabel(ButtonID::RRING);
			SameLine();
			drawCombo<RingMode>(SettingID::RIGHT_RING_MODE, _chord, ImGuiComboFlags_NoPreview);
			if (IsItemHovered())
				SetTooltip(commandRegistry.getHelp(enum_name(SettingID::RIGHT_RING_MODE)).data());

			TableNextRow();
			TableNextColumn();
			drawCombo<StickMode>(SettingID::RIGHT_STICK_MODE, _chord);
			if (IsItemClicked(ImGuiMouseButton_Right))
			{
				_stickConfigPopup = SettingID::RIGHT_STICK_MODE;
			}
			// SameLine();
			// if (Button("..."))
			//	_stickConfigPopup = SettingID::RIGHT_STICK_MODE;
			// if (IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			//	SetTooltip("More right stick settings");
			TableNextColumn();
			drawLabel(SettingID::RIGHT_STICK_MODE);

			StickMode rightStickMode = getSettingValue<StickMode>(SettingID::RIGHT_STICK_MODE);
			if (rightStickMode == StickMode::NO_MOUSE || rightStickMode == StickMode::OUTER_RING || rightStickMode == StickMode::INNER_RING)
			{
				TableNextRow();
				TableNextColumn();
				drawButton(ButtonID::RUP);
				TableNextColumn();
				drawLabel(ButtonID::RUP);

				TableNextRow();
				TableNextColumn();
				drawButton(ButtonID::RLEFT);
				TableNextColumn();
				drawLabel(ButtonID::RLEFT);

				TableNextRow();
				TableNextColumn();
				drawButton(ButtonID::RRIGHT);
				TableNextColumn();
				drawLabel(ButtonID::RRIGHT);

				TableNextRow();
				TableNextColumn();
				drawButton(ButtonID::RDOWN);
				TableNextColumn();
				drawLabel(ButtonID::RDOWN);
			}
			else if (rightStickMode == StickMode::AIM)
			{
				TableNextRow();
				TableNextColumn();
				drawAny2Floats(SettingID::STICK_SENS);
				TableNextColumn();
				drawLabel(SettingID::STICK_SENS);
			}
			else if (rightStickMode == StickMode::FLICK || rightStickMode == StickMode::FLICK_ONLY || rightStickMode == StickMode::ROTATE_ONLY)
			{
				TableNextRow();
				TableNextColumn();
				drawCombo<GyroOutput>(SettingID::FLICK_STICK_OUTPUT, _chord);
				TableNextColumn();
				drawLabel(SettingID::FLICK_STICK_OUTPUT);
			}
			else if (rightStickMode == StickMode::MOUSE_AREA || rightStickMode == StickMode::MOUSE_RING)
			{
				TableNextRow();
				TableNextColumn();
				drawAnyFloat(SettingID::MOUSE_RING_RADIUS);
				TableNextColumn();
				drawLabel(SettingID::MOUSE_RING_RADIUS);
			}
			else if (rightStickMode == StickMode::SCROLL_WHEEL)
			{
				TableNextRow();
				TableNextColumn();
				drawButton(ButtonID::RLEFT);
				TableNextColumn();
				drawLabel(ButtonID::RLEFT);

				TableNextRow();
				TableNextColumn();
				drawButton(ButtonID::RRIGHT);
				TableNextColumn();
				drawLabel(ButtonID::RRIGHT);
			}
			EndTable();
		}
		EndChild();

		BeginChild("Bottom Bindings", { 0.f, barSize }, true);
		if (BeginTable("BottomTable", 11, ImGuiTableFlags_SizingStretchSame))
		{
			// TODO: GYRO_OUTPUT, AUTO_CALIBRATE_GYRO
			TableNextRow();
			TableNextColumn();
			drawButton(ButtonID::LEAN_LEFT);
			TableNextColumn();
			drawButton(ButtonID::LEAN_RIGHT);
			TableNextColumn();
			drawCombo<GyroOutput>(SettingID::GYRO_OUTPUT, _chord);
			if (IsItemClicked(ImGuiMouseButton_Right))
			{
				OpenPopup("GyroSensContext");
			}
			TableNextColumn();
			drawCombo<GyroSpace>(SettingID::GYRO_SPACE, _chord);
			TableNextColumn();
			JSMVariable<GyroSettings>* gyroSettingsVar = nullptr;
			auto gyroSettingsVal = getSettingValue<GyroSettings>(SettingID::GYRO_ON, &gyroSettingsVar);
			stringstream ss;
			ss << gyroSettingsVal;
			if (BeginCombo("##GyroButton", ss.str().c_str(), ImGuiComboFlags_NoArrowButton))
			{
				bool isSelected = gyroSettingsVal.ignore_mode == GyroIgnoreMode::LEFT_STICK;
				if (Selectable(enum_name(GyroIgnoreMode::LEFT_STICK).data(), isSelected))
				{
					gyroSettingsVal.ignore_mode = GyroIgnoreMode::LEFT_STICK;
					gyroSettingsVal.button = ButtonID::NONE;
					gyroSettingsVar->set(gyroSettingsVal);
				}
				if (isSelected)
					SetItemDefaultFocus();
				isSelected = gyroSettingsVal.ignore_mode == GyroIgnoreMode::RIGHT_STICK;
				if (Selectable(enum_name(GyroIgnoreMode::RIGHT_STICK).data(), isSelected))
				{
					gyroSettingsVal.ignore_mode = GyroIgnoreMode::RIGHT_STICK;
					gyroSettingsVal.button = ButtonID::NONE;
					gyroSettingsVar->set(gyroSettingsVal);
				}
				if (isSelected)
					SetItemDefaultFocus();
				for (ButtonID id = ButtonID::NONE; id < ButtonID::SIZE; id = id + 1)
				{
					isSelected = gyroSettingsVal.button == id && gyroSettingsVal.ignore_mode == GyroIgnoreMode::BUTTON;
					if (Selectable(enum_name(id).data(), isSelected))
					{
						gyroSettingsVal.ignore_mode = GyroIgnoreMode::BUTTON;
						gyroSettingsVal.button = id;
						gyroSettingsVar->set(gyroSettingsVal);
					}
					if (isSelected)
						SetItemDefaultFocus();
				}
				EndCombo();
			}
			TableNextColumn();
			drawButton(ButtonID::MRING);
			TableNextColumn();
			drawCombo<StickMode>(SettingID::MOTION_STICK_MODE, _chord);
			if (IsItemClicked(ImGuiMouseButton_Right))
			{
				_stickConfigPopup = SettingID::MOTION_STICK_MODE;
			}

			StickMode motionStickMode = getSettingValue<StickMode>(SettingID::MOTION_STICK_MODE);
			if (motionStickMode == StickMode::NO_MOUSE || motionStickMode == StickMode::OUTER_RING || motionStickMode == StickMode::INNER_RING)
			{
				TableNextColumn();
				drawButton(ButtonID::MUP);

				TableNextColumn();
				drawButton(ButtonID::MLEFT);

				TableNextColumn();
				drawButton(ButtonID::MRIGHT);

				TableNextColumn();
				drawButton(ButtonID::MDOWN);
			}
			else if (motionStickMode == StickMode::AIM)
			{
				TableNextColumn();
				drawAny2Floats(SettingID::STICK_SENS);
			}
			else if (motionStickMode == StickMode::FLICK || motionStickMode == StickMode::FLICK_ONLY || motionStickMode == StickMode::ROTATE_ONLY)
			{
				TableNextColumn();
				drawCombo<GyroOutput>(SettingID::FLICK_STICK_OUTPUT, _chord);
			}
			else if (motionStickMode == StickMode::MOUSE_AREA || motionStickMode == StickMode::MOUSE_RING)
			{
				TableNextColumn();
				drawAnyFloat(SettingID::MOUSE_RING_RADIUS);
			}
			else if (motionStickMode == StickMode::SCROLL_WHEEL)
			{
				TableNextColumn();
				drawButton(ButtonID::MLEFT);

				TableNextColumn();
				drawButton(ButtonID::MRIGHT);
			}

			// TODO: LEAN_THRESHOLD in context menu
			// TODO: TRACKBALL_DECAY TRIGGER_SKIP_DELAY
			TableNextRow();
			TableNextColumn();
			drawLabel(ButtonID::LEAN_LEFT);
			TableNextColumn();
			drawLabel(ButtonID::LEAN_RIGHT);
			TableNextColumn();
			drawLabel(SettingID::GYRO_OUTPUT);
			TableNextColumn();
			drawLabel(SettingID::GYRO_SPACE);
			TableNextColumn();
			AlignTextToFramePadding();
			Text("GYRO_");
			if (IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			{
				SetTooltip(commandRegistry.getHelp(gyroSettingsVal.always_off ? "GYRO_ON" : "GYRO_OFF").data());
			}
			SameLine();
			Switch enableButton = *enum_cast<Switch>(gyroSettingsVal.always_off);
			if (BeginCombo("##GyroMode", enum_name(enableButton).data(), ImGuiComboFlags_NoArrowButton))
			{
				if (Selectable("ON", enableButton == Switch::ON))
				{
					gyroSettingsVal.always_off = true;
					gyroSettingsVar->set(gyroSettingsVal);
				}
				if (Selectable("OFF", enableButton == Switch::OFF))
				{
					gyroSettingsVal.always_off = false;
					gyroSettingsVar->set(gyroSettingsVal);
				}
				EndCombo();
			}
			TableNextColumn();
			drawLabel(ButtonID::MRING);
			SameLine();
			drawCombo<RingMode>(SettingID::MOTION_RING_MODE, _chord, ImGuiComboFlags_NoPreview);
			TableNextColumn();
			drawLabel(SettingID::MOTION_STICK_MODE);

			if (motionStickMode == StickMode::NO_MOUSE || motionStickMode == StickMode::OUTER_RING || motionStickMode == StickMode::INNER_RING)
			{
				TableNextColumn();
				drawLabel(ButtonID::MUP);

				TableNextColumn();
				drawLabel(ButtonID::MLEFT);

				TableNextColumn();
				drawLabel(ButtonID::MRIGHT);

				TableNextColumn();
				drawLabel(ButtonID::MDOWN);
			}
			else if (motionStickMode == StickMode::AIM)
			{
				TableNextColumn();
				drawLabel(SettingID::STICK_SENS);
			}
			else if (motionStickMode == StickMode::FLICK || motionStickMode == StickMode::FLICK_ONLY || motionStickMode == StickMode::ROTATE_ONLY)
			{
				TableNextColumn();
				drawLabel(SettingID::FLICK_STICK_OUTPUT);
			}
			else if (motionStickMode == StickMode::MOUSE_AREA || motionStickMode == StickMode::MOUSE_RING)
			{
				TableNextColumn();
				drawLabel(SettingID::MOUSE_RING_RADIUS);
			}
			else if (motionStickMode == StickMode::SCROLL_WHEEL)
			{
				TableNextColumn();
				drawLabel(ButtonID::MLEFT);

				TableNextColumn();
				drawLabel(ButtonID::MRIGHT);
			}

			if (BeginPopup("GyroSensContext"))
			{
				drawAny2Floats(SettingID::MIN_GYRO_SENS, true);
				drawAny2Floats(SettingID::MAX_GYRO_SENS, true);
				drawAnyFloat(SettingID::MIN_GYRO_THRESHOLD, true);
				drawAnyFloat(SettingID::MAX_GYRO_THRESHOLD, true);
				drawAnyFloat(SettingID::GYRO_SMOOTH_THRESHOLD, true);
				drawAnyFloat(SettingID::GYRO_SMOOTH_TIME, true);
				drawAnyFloat(SettingID::GYRO_CUTOFF_SPEED, true);
				drawAnyFloat(SettingID::GYRO_CUTOFF_RECOVERY, true);
				drawCombo<GyroAxisMask>(SettingID::MOUSE_X_FROM_GYRO_AXIS, _chord, ImGuiComboFlags_NoArrowButton, true);
				drawCombo<GyroAxisMask>(SettingID::MOUSE_Y_FROM_GYRO_AXIS, _chord, ImGuiComboFlags_NoArrowButton, true);
				drawCombo<JoyconMask>(SettingID::JOYCON_GYRO_MASK, _chord, ImGuiComboFlags_NoArrowButton, true);
				EndPopup();
			}

			EndTable();
		}
		EndChild();

		if (_showPopup != ButtonID::INVALID)
		{
			stringstream ss;
			JSMVariable<Mapping>* variable = nullptr;
			if (_chord != ButtonID::NONE)
			{
				ss << _chord << ',';
				variable = &mappings[int(_showPopup)].createChord(_chord);
			}
			else
			{
				variable = &mappings[int(_showPopup)];
			}
			ss << _showPopup;
			_inputSelector.show(variable, ss.str());
			_showPopup = ButtonID::INVALID;
		}
		_inputSelector.draw();

		if (ImGui::BeginPopup("StickConfig"))
		{
			// TODO: LEFT_STICK_VIRTUAL_SCALE, RIGHT_STICK_VIRTUAL_SCALE
			if (_stickConfigPopup == SettingID::RIGHT_STICK_MODE)
			{
				drawLabel(SettingID::RIGHT_STICK_DEADZONE_INNER);
				SameLine();
				drawPercentFloat(SettingID::RIGHT_STICK_DEADZONE_INNER);

				drawLabel(SettingID::RIGHT_STICK_DEADZONE_OUTER);
				SameLine();
				drawPercentFloat(SettingID::RIGHT_STICK_DEADZONE_OUTER);
			}
			else if (_stickConfigPopup == SettingID::LEFT_STICK_MODE)
			{
				drawLabel(SettingID::LEFT_STICK_DEADZONE_INNER);
				SameLine();
				drawPercentFloat(SettingID::LEFT_STICK_DEADZONE_INNER);

				drawLabel(SettingID::LEFT_STICK_DEADZONE_OUTER);
				SameLine();
				drawPercentFloat(SettingID::LEFT_STICK_DEADZONE_OUTER);
			}
			else if (_stickConfigPopup == SettingID::MOTION_STICK_MODE)
			{
				drawLabel(SettingID::MOTION_DEADZONE_INNER);
				SameLine();
				drawAnyFloat(SettingID::MOTION_DEADZONE_INNER);

				drawLabel(SettingID::MOTION_DEADZONE_OUTER);
				SameLine();
				drawAnyFloat(SettingID::MOTION_DEADZONE_OUTER);
			}
			else if (_stickConfigPopup == SettingID::ZL_MODE)
			{
				bool hairTrigger = SettingsManager::get<float>(SettingID::TRIGGER_THRESHOLD)->value() == -1.f;
				if (Checkbox("Hair Trigger", &hairTrigger))
				{
					if (hairTrigger)
						SettingsManager::get<float>(SettingID::TRIGGER_THRESHOLD)->set(-1.f);
					else
						SettingsManager::get<float>(SettingID::TRIGGER_THRESHOLD)->reset();
				}
				if (!hairTrigger)
					drawPercentFloat(SettingID::TRIGGER_THRESHOLD, TRUE);
			}

			auto stickMode = getSettingValue<StickMode>(_stickConfigPopup);
			if (stickMode == StickMode::FLICK || stickMode == StickMode::FLICK_ONLY || stickMode == StickMode::ROTATE_ONLY)
			{
				drawLabel(SettingID::FLICK_SNAP_MODE);
				SameLine();
				drawCombo<FlickSnapMode>(SettingID::FLICK_SNAP_MODE, _chord);

				drawLabel(SettingID::FLICK_SNAP_STRENGTH);
				SameLine();
				drawPercentFloat(SettingID::FLICK_SNAP_STRENGTH);

				drawLabel(SettingID::FLICK_DEADZONE_ANGLE);
				SameLine();
				drawAnyFloat(SettingID::FLICK_DEADZONE_ANGLE);

				auto& fsOut = *SettingsManager::get<GyroOutput>(SettingID::FLICK_STICK_OUTPUT);
				if (fsOut == GyroOutput::MOUSE)
				{
					drawLabel(SettingID::FLICK_TIME);
					SameLine();
					drawAnyFloat(SettingID::FLICK_TIME);

					drawLabel(SettingID::FLICK_TIME_EXPONENT);
					SameLine();
					drawAnyFloat(SettingID::FLICK_TIME_EXPONENT);
				}
				else // LEFT_STICK, RIGHT_STICK, PS_MOTION
				{
					drawLabel(SettingID::VIRTUAL_STICK_CALIBRATION);
					SameLine();
					drawAnyFloat(SettingID::VIRTUAL_STICK_CALIBRATION);
				}
			}
			else if (stickMode == StickMode::AIM)
			{
				drawLabel(SettingID::STICK_POWER);
				SameLine();
				drawAnyFloat(SettingID::STICK_POWER);

				drawLabel(SettingID::STICK_ACCELERATION_RATE);
				SameLine();
				drawAnyFloat(SettingID::STICK_ACCELERATION_RATE);

				drawLabel(SettingID::STICK_ACCELERATION_CAP);
				SameLine();
				drawAnyFloat(SettingID::STICK_ACCELERATION_CAP);
			}

			else if (stickMode == StickMode::MOUSE_RING)
			{
				drawLabel(SettingID::SCREEN_RESOLUTION_X);
				SameLine();
				drawAnyFloat(SettingID::SCREEN_RESOLUTION_X);

				drawLabel(SettingID::SCREEN_RESOLUTION_Y);
				SameLine();
				drawAnyFloat(SettingID::SCREEN_RESOLUTION_Y);
			}
			else if (stickMode == StickMode::SCROLL_WHEEL)
			{
				drawLabel(SettingID::SCROLL_SENS);
				SameLine();
				drawAny2Floats(SettingID::SCROLL_SENS);
			}
			else if (stickMode == StickMode::LEFT_STICK)
			{
				drawLabel(SettingID::LEFT_STICK_UNDEADZONE_INNER);
				SameLine();
				drawAnyFloat(SettingID::LEFT_STICK_UNDEADZONE_INNER);

				drawLabel(SettingID::LEFT_STICK_UNDEADZONE_OUTER);
				SameLine();
				drawAnyFloat(SettingID::LEFT_STICK_UNDEADZONE_OUTER);

				drawLabel(SettingID::VIRTUAL_STICK_CALIBRATION);
				SameLine();
				drawAnyFloat(SettingID::VIRTUAL_STICK_CALIBRATION);
			}
			else if (stickMode == StickMode::RIGHT_STICK)
			{
				drawLabel(SettingID::RIGHT_STICK_UNDEADZONE_INNER);
				SameLine();
				drawAnyFloat(SettingID::RIGHT_STICK_UNDEADZONE_INNER);

				drawLabel(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);
				SameLine();
				drawAnyFloat(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);

				drawLabel(SettingID::VIRTUAL_STICK_CALIBRATION);
				SameLine();
				drawAnyFloat(SettingID::VIRTUAL_STICK_CALIBRATION);
			}
			else if (stickMode >= StickMode::LEFT_ANGLE_TO_X && stickMode <= StickMode::RIGHT_ANGLE_TO_Y)
			{
				drawLabel(SettingID::ANGLE_TO_AXIS_DEADZONE_INNER);
				SameLine();
				drawAnyFloat(SettingID::ANGLE_TO_AXIS_DEADZONE_INNER);

				drawLabel(SettingID::ANGLE_TO_AXIS_DEADZONE_OUTER);
				SameLine();
				drawAnyFloat(SettingID::ANGLE_TO_AXIS_DEADZONE_OUTER);

				if (stickMode == StickMode::LEFT_ANGLE_TO_X || stickMode == StickMode::LEFT_ANGLE_TO_Y) // isLeft
				{
					drawLabel(SettingID::LEFT_STICK_UNDEADZONE_INNER);
					SameLine();
					drawAnyFloat(SettingID::LEFT_STICK_UNDEADZONE_INNER);

					drawLabel(SettingID::LEFT_STICK_UNDEADZONE_OUTER);
					SameLine();
					drawAnyFloat(SettingID::LEFT_STICK_UNDEADZONE_OUTER);

					drawLabel(SettingID::LEFT_STICK_UNPOWER);
					SameLine();
					drawAnyFloat(SettingID::LEFT_STICK_UNPOWER);
				}
				else // isRight!
				{
					drawLabel(SettingID::RIGHT_STICK_UNDEADZONE_INNER);
					SameLine();
					drawAnyFloat(SettingID::RIGHT_STICK_UNDEADZONE_INNER);

					drawLabel(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);
					SameLine();
					drawAnyFloat(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);

					drawLabel(SettingID::RIGHT_STICK_UNPOWER);
					SameLine();
					drawAnyFloat(SettingID::RIGHT_STICK_UNPOWER);
				}
			}
			else if (stickMode == StickMode::LEFT_WIND_X || stickMode == StickMode::RIGHT_WIND_X)
			{
				drawLabel(SettingID::WIND_STICK_RANGE);
				SameLine();
				drawAnyFloat(SettingID::WIND_STICK_RANGE);

				drawLabel(SettingID::WIND_STICK_POWER);
				SameLine();
				drawAnyFloat(SettingID::WIND_STICK_POWER);

				drawLabel(SettingID::UNWIND_RATE);
				SameLine();
				drawAnyFloat(SettingID::UNWIND_RATE);

				if (stickMode == StickMode::LEFT_WIND_X) // isLeft
				{
					drawLabel(SettingID::LEFT_STICK_UNDEADZONE_INNER);
					SameLine();
					drawAnyFloat(SettingID::LEFT_STICK_UNDEADZONE_INNER);

					drawLabel(SettingID::LEFT_STICK_UNDEADZONE_OUTER);
					SameLine();
					drawAnyFloat(SettingID::LEFT_STICK_UNDEADZONE_OUTER);

					drawLabel(SettingID::LEFT_STICK_UNPOWER);
					SameLine();
					drawAnyFloat(SettingID::LEFT_STICK_UNPOWER);
				}
				else // isRight!
				{
					drawLabel(SettingID::RIGHT_STICK_UNDEADZONE_INNER);
					SameLine();
					drawAnyFloat(SettingID::RIGHT_STICK_UNDEADZONE_INNER);

					drawLabel(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);
					SameLine();
					drawAnyFloat(SettingID::RIGHT_STICK_UNDEADZONE_OUTER);

					drawLabel(SettingID::RIGHT_STICK_UNPOWER);
					SameLine();
					drawAnyFloat(SettingID::RIGHT_STICK_UNPOWER);
				}
			}
			EndPopup();
		}

		if (!IsPopupOpen("StickConfig") && _stickConfigPopup != SettingID::INVALID)
		{
			static bool openOrClear = true;
			if (openOrClear)
				OpenPopup("StickConfig");
			else
				_stickConfigPopup = SettingID::INVALID;
			openOrClear = !openOrClear;
		}
		ImGui::EndTabItem();
	}

	return open;
}