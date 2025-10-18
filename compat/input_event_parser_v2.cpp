#include "input_event_parser_v2.h"

#include "core/input/input_event.h"
#include "core/io/marshalls.h"
#include "core/variant/variant_parser.h"
using namespace V2InputEvent;
struct _KeyCodeText {
	Key code;
	const char *text;
};

static const _KeyCodeText _keycodes[] = {

	/* clang-format off */
		{Key::ESCAPE                        ,"Escape"}, //start special
		{Key::TAB                           ,"Tab"},
		{Key::BACKTAB                       ,"BackTab"}, //v2 "BackTab", v4 "Backtab"
		{Key::BACKSPACE                     ,"BackSpace"}, //v2 "BackSpace", v4 "Backspace"
		{Key::ENTER                         ,"Return"}, //v2 "Return", v4 "Enter"
		{Key::KP_ENTER                      ,"Enter"}, //v2 "Enter", v4 "Kp Enter"
		{Key::INSERT                        ,"Insert"},
		{Key::KEY_DELETE                    ,"Delete"},
		{Key::PAUSE                         ,"Pause"},
		{Key::PRINT                         ,"Print"},
		{Key::SYSREQ                        ,"SysReq"},
		{Key::CLEAR                         ,"Clear"},
		{Key::HOME                          ,"Home"},
		{Key::END                           ,"End"},
		{Key::LEFT                          ,"Left"},
		{Key::UP                            ,"Up"},
		{Key::RIGHT                         ,"Right"},
		{Key::DOWN                          ,"Down"},
		{Key::PAGEUP                        ,"PageUp"},
		{Key::PAGEDOWN                      ,"PageDown"},
		{Key::SHIFT                         ,"Shift"},
		{Key::CTRL                       	  ,"Control"}, //v2 "Control", v4 "Ctrl"
		{Key::META                          ,"Meta"},    //v2 "Meta", v4 "Windows" or "Command"
		{Key::ALT                           ,"Alt"},     //v2 "Alt", v4 "Alt" or "Option"
		{Key::CAPSLOCK                      ,"CapsLock"},
		{Key::NUMLOCK                       ,"NumLock"},
		{Key::SCROLLLOCK                    ,"ScrollLock"},
		{Key::F1                            ,"F1"},
		{Key::F2                            ,"F2"},
		{Key::F3                            ,"F3"},
		{Key::F4                            ,"F4"},
		{Key::F5                            ,"F5"},
		{Key::F6                            ,"F6"},
		{Key::F7                            ,"F7"},
		{Key::F8                            ,"F8"},
		{Key::F9                            ,"F9"},
		{Key::F10                           ,"F10"},
		{Key::F11                           ,"F11"},
		{Key::F12                           ,"F12"},
		{Key::F13                           ,"F13"},
		{Key::F14                           ,"F14"},
		{Key::F15                           ,"F15"},
		{Key::F16                           ,"F16"},
		{(Key::SPECIAL | (Key)0x80)              ,"Kp Enter"}, //the v2 value of this keycode doesn't exist in v4
		{Key::KP_MULTIPLY                   ,"Kp Multiply"},
		{Key::KP_DIVIDE                     ,"Kp Divide"},
		{Key::KP_SUBTRACT                   ,"Kp Subtract"},
		{Key::KP_PERIOD                     ,"Kp Period"},
		{Key::KP_ADD                        ,"Kp Add"},
		{Key::KP_0                          ,"Kp 0"},
		{Key::KP_1                          ,"Kp 1"},
		{Key::KP_2                          ,"Kp 2"},
		{Key::KP_3                          ,"Kp 3"},
		{Key::KP_4                          ,"Kp 4"},
		{Key::KP_5                          ,"Kp 5"},
		{Key::KP_6                          ,"Kp 6"},
		{Key::KP_7                          ,"Kp 7"},
		{Key::KP_8                          ,"Kp 8"},
		{Key::KP_9                          ,"Kp 9"},
		{(Key::SPECIAL | (Key)0x40)              ,"Super L"},
		{(Key::SPECIAL | (Key)0x41)              ,"Super R"},
		{Key::MENU                          ,"Menu"},
		{Key::HYPER                         ,"Hyper L"}, // v2 Hyper L, v4 Hyper
		{(Key::SPECIAL | (Key)0x44)              ,"Hyper R"},
		{Key::HELP                          ,"Help"},
		{(Key::SPECIAL | (Key)0x46)              ,"Direction L"},
		{(Key::SPECIAL | (Key)0x47)              ,"Direction R"},
		{Key::BACK                          ,"Back"},
		{Key::FORWARD                       ,"Forward"},
		{Key::STOP                          ,"Stop"},
		{Key::REFRESH                       ,"Refresh"},
		{Key::VOLUMEDOWN                    ,"VolumeDown"},
		{Key::VOLUMEMUTE                    ,"VolumeMute"},
		{Key::VOLUMEUP                      ,"VolumeUp"},
		{(Key::SPECIAL | (Key)0x4F)              ,"BassBoost"},
		{(Key::SPECIAL | (Key)0x50)              ,"BassUp"},
		{(Key::SPECIAL | (Key)0x51)              ,"BassDown"},
		{(Key::SPECIAL | (Key)0x52)              ,"TrebleUp"},
		{(Key::SPECIAL | (Key)0x53)              ,"TrebleDown"},
		{Key::MEDIAPLAY                     ,"MediaPlay"},
		{Key::MEDIASTOP                     ,"MediaStop"},
		{Key::MEDIAPREVIOUS                 ,"MediaPrevious"},
		{Key::MEDIANEXT                     ,"MediaNext"},
		{Key::MEDIARECORD                   ,"MediaRecord"},
		{Key::HOMEPAGE                      ,"HomePage"},
		{Key::FAVORITES                     ,"Favorites"},
		{Key::SEARCH                        ,"Search"},
		{Key::STANDBY                       ,"StandBy"},
		{Key::LAUNCHMAIL                    ,"LaunchMail"},
		{Key::LAUNCHMEDIA                   ,"LaunchMedia"},
		{Key::LAUNCH0                       ,"Launch0"},
		{Key::LAUNCH1                       ,"Launch1"},
		{Key::LAUNCH2                       ,"Launch2"},
		{Key::LAUNCH3                       ,"Launch3"},
		{Key::LAUNCH4                       ,"Launch4"},
		{Key::LAUNCH5                       ,"Launch5"},
		{Key::LAUNCH6                       ,"Launch6"},
		{Key::LAUNCH7                       ,"Launch7"},
		{Key::LAUNCH8                       ,"Launch8"},
		{Key::LAUNCH9                       ,"Launch9"},
		{Key::LAUNCHA                       ,"LaunchA"},
		{Key::LAUNCHB                       ,"LaunchB"},
		{Key::LAUNCHC                       ,"LaunchC"},
		{Key::LAUNCHD                       ,"LaunchD"},
		{Key::LAUNCHE                       ,"LaunchE"},
		{Key::LAUNCHF                       ,"LaunchF"}, // end special

		{Key::UNKNOWN                       ,"Unknown"},

		{Key::SPACE                         ,"Space"},
		{Key::EXCLAM                        ,"Exclam"},
		{Key::QUOTEDBL                      ,"QuoteDbl"},
		{Key::NUMBERSIGN                    ,"NumberSign"},
		{Key::DOLLAR                        ,"Dollar"},
		{Key::PERCENT                       ,"Percent"},
		{Key::AMPERSAND                     ,"Ampersand"},
		{Key::APOSTROPHE                    ,"Apostrophe"},
		{Key::PARENLEFT                     ,"ParenLeft"},
		{Key::PARENRIGHT                    ,"ParenRight"},
		{Key::ASTERISK                      ,"Asterisk"},
		{Key::PLUS                          ,"Plus"},
		{Key::COMMA                         ,"Comma"},
		{Key::MINUS                         ,"Minus"},
		{Key::PERIOD                        ,"Period"},
		{Key::SLASH                         ,"Slash"},
		{Key::KEY_0                         ,"0"},
		{Key::KEY_1                         ,"1"},
		{Key::KEY_2                         ,"2"},
		{Key::KEY_3                         ,"3"},
		{Key::KEY_4                         ,"4"},
		{Key::KEY_5                         ,"5"},
		{Key::KEY_6                         ,"6"},
		{Key::KEY_7                         ,"7"},
		{Key::KEY_8                         ,"8"},
		{Key::KEY_9                         ,"9"},
		{Key::COLON                         ,"Colon"},
		{Key::SEMICOLON                     ,"Semicolon"},
		{Key::LESS                          ,"Less"},
		{Key::EQUAL                         ,"Equal"},
		{Key::GREATER                       ,"Greater"},
		{Key::QUESTION                      ,"Question"},
		{Key::AT                            ,"At"},
		{Key::A                             ,"A"},
		{Key::B                             ,"B"},
		{Key::C                             ,"C"},
		{Key::D                             ,"D"},
		{Key::E                             ,"E"},
		{Key::F                             ,"F"},
		{Key::G                             ,"G"},
		{Key::H                             ,"H"},
		{Key::I                             ,"I"},
		{Key::J                             ,"J"},
		{Key::K                             ,"K"},
		{Key::L                             ,"L"},
		{Key::M                             ,"M"},
		{Key::N                             ,"N"},
		{Key::O                             ,"O"},
		{Key::P                             ,"P"},
		{Key::Q                             ,"Q"},
		{Key::R                             ,"R"},
		{Key::S                             ,"S"},
		{Key::T                             ,"T"},
		{Key::U                             ,"U"},
		{Key::V                             ,"V"},
		{Key::W                             ,"W"},
		{Key::X                             ,"X"},
		{Key::Y                             ,"Y"},
		{Key::Z                             ,"Z"},
		{Key::BRACKETLEFT                   ,"BracketLeft"},
		{Key::BACKSLASH                     ,"BackSlash"},
		{Key::BRACKETRIGHT                  ,"BracketRight"},
		{Key::ASCIICIRCUM                   ,"AsciiCircum"},
		{Key::UNDERSCORE                    ,"UnderScore"},
		{Key::QUOTELEFT                     ,"QuoteLeft"},
		{Key::BRACELEFT                     ,"BraceLeft"},
		{Key::BAR                           ,"Bar"},
		{Key::BRACERIGHT                    ,"BraceRight"},
		{Key::ASCIITILDE                    ,"AsciiTilde"},
		{(Key)0x00A0                        ,"NoBreakSpace"},
		{(Key)0x00A1                        ,"ExclamDown"},
		{(Key)0x00A2                        ,"Cent"},
		{(Key)0x00A3                        ,"Sterling"},
		{(Key)0x00A4                        ,"Currency"},
		{Key::YEN                           ,"Yen"},
		{(Key)0x00A6                        ,"BrokenBar"},
		{Key::SECTION                       ,"Section"},
		{(Key)0x00A8                        ,"Diaeresis"},
		{(Key)0x00A9                        ,"Copyright"},
		{(Key)0x00AA                        ,"Ordfeminine"},
		{(Key)0x00AB                        ,"GuillemotLeft"},
		{(Key)0x00AC                        ,"NotSign"},
		{(Key)0x00AD                        ,"Hyphen"},
		{(Key)0x00AE                        ,"Registered"},
		{(Key)0x00AF                        ,"Macron"},
		{(Key)0x00B0                        ,"Degree"},
		{(Key)0x00B1                        ,"PlusMinus"},
		{(Key)0x00B2                        ,"TwoSuperior"},
		{(Key)0x00B3                        ,"ThreeSuperior"},
		{(Key)0x00B4                        ,"Acute"},
		{(Key)0x00B5                        ,"Mu"},
		{(Key)0x00B6                        ,"Paragraph"},
		{(Key)0x00B7                        ,"PeriodCentered"},
		{(Key)0x00B8                        ,"Cedilla"},
		{(Key)0x00B9                        ,"OneSuperior"},
		{(Key)0x00BA                        ,"Masculine"},
		{(Key)0x00BB                        ,"GuillemotRight"},
		{(Key)0x00BC                        ,"OneQuarter"},
		{(Key)0x00BD                        ,"OneHalf"},
		{(Key)0x00BE                        ,"ThreeQuarters"},
		{(Key)0x00BF                        ,"QuestionDown"},
		{(Key)0x00C0                        ,"Agrave"},
		{(Key)0x00C1                        ,"Aacute"},
		{(Key)0x00C2                        ,"AcircumFlex"},
		{(Key)0x00C3                        ,"Atilde"},
		{(Key)0x00C4                        ,"Adiaeresis"},
		{(Key)0x00C5                        ,"Aring"},
		{(Key)0x00C6                        ,"Ae"},
		{(Key)0x00C7                        ,"Ccedilla"},
		{(Key)0x00C8                        ,"Egrave"},
		{(Key)0x00C9                        ,"Eacute"},
		{(Key)0x00CA                        ,"Ecircumflex"},
		{(Key)0x00CB                        ,"Ediaeresis"},
		{(Key)0x00CC                        ,"Igrave"},
		{(Key)0x00CD                        ,"Iacute"},
		{(Key)0x00CE                        ,"Icircumflex"},
		{(Key)0x00CF                        ,"Idiaeresis"},
		{(Key)0x00D0                        ,"Eth"},
		{(Key)0x00D1                        ,"Ntilde"},
		{(Key)0x00D2                        ,"Ograve"},
		{(Key)0x00D3                        ,"Oacute"},
		{(Key)0x00D4                        ,"Ocircumflex"},
		{(Key)0x00D5                        ,"Otilde"},
		{(Key)0x00D6                        ,"Odiaeresis"},
		{(Key)0x00D7                        ,"Multiply"},
		{(Key)0x00D8                        ,"Ooblique"},
		{(Key)0x00D9                        ,"Ugrave"},
		{(Key)0x00DA                        ,"Uacute"},
		{(Key)0x00DB                        ,"Ucircumflex"},
		{(Key)0x00DC                        ,"Udiaeresis"},
		{(Key)0x00DD                        ,"Yacute"},
		{(Key)0x00DE                        ,"Thorn"},
		{(Key)0x00DF                        ,"Ssharp"},

		{(Key)0x00F7                        ,"Division"},
		{(Key)0x00FF                        ,"Ydiaeresis"},
		{(Key)0x0100                        ,0}
	/* clang-format on */
};

static const HashSet<String> v2_keys_with_spaces_prefixes{
	"Direction", "Hyper", "Super", "Kp"
};

Key convert_v2_key_to_v4_key(V2KeyList spkey) {
	if (spkey & V2InputEvent::SPKEY) {
		return Key(spkey ^ V2InputEvent::SPKEY) | Key::SPECIAL;
	}
	return Key(spkey);
}

V2KeyList InputEventParserV2::convert_v4_key_to_v2_key(Key spkey) {
	if (((uint32_t)spkey & (uint32_t)Key::SPECIAL)) {
		return V2KeyList(((uint32_t)spkey ^ (uint32_t)Key::SPECIAL) | (uint32_t)V2InputEvent::SPKEY);
	}
	return V2KeyList(spkey);
}

V2KeyList InputEventParserV2::get_v2_key_from_iek(Ref<InputEventKey> iek) {
	return convert_v4_key_to_v2_key(iek->get_keycode());
}

String keycode_get_v2_string(Key p_code) {
	String codestr;

	p_code &= KeyModifierMask::CODE_MASK;

	const _KeyCodeText *kct = &_keycodes[0];

	while (kct->text) {
		if (kct->code == p_code) {
			codestr += kct->text;
			return codestr;
		}
		kct++;
	}
	// Couldn't find it in keycode mapping
	codestr += String::chr((int)p_code);

	return codestr;
}

String get_v2_string_from_iek(Ref<InputEventKey> iek) {
	return keycode_get_v2_string(iek->get_keycode());
}

String InputEventParserV2::v4_input_event_to_v2_string(const Ref<InputEvent> &p_v4_event) {
	// TODO: ID?? ID was an incrementing counter in v2 but that no longer exists in v4
	String str = "Device " + itos(p_v4_event->get_device()) + " ID " + itos(0) + " ";

	InputEventType type = p_v4_event->get_type();

	auto add_mod_str = [&](const Ref<InputEventWithModifiers> &p_v4_event) {
		if (p_v4_event->is_shift_pressed()) {
			str += "S";
		}
		if (p_v4_event->is_ctrl_pressed()) {
			str += "C";
		}
		if (p_v4_event->is_alt_pressed()) {
			str += "A";
		}
		if (p_v4_event->is_meta_pressed()) {
			str += "M";
		}
	};

	switch (type) {
		case InputEventType::MAX: {
			return "Event: None";
		} break;
		case InputEventType::KEY: {
			str += "Event: Key ";
			Ref<InputEventKey> iek = p_v4_event;
			auto scancode = get_v2_key_from_iek(iek);
			auto unicode = iek->get_unicode();
			auto echo = iek->is_echo();
			auto pressed = iek->is_pressed();

			str = str + "Unicode: " + String::chr(unicode) + " Scan: " + itos((int64_t)scancode) + " Echo: " + String(echo ? "True" : "False") + " Pressed: " + String(pressed ? "True" : "False") + " Mod: ";
			add_mod_str(iek);

			return str;
		} break;
		case InputEventType::MOUSE_MOTION: {
			str += "Event: Motion ";
			Ref<InputEventMouseMotion> iem = p_v4_event;
			auto pos = iem->get_position();
			auto rel = iem->get_relative();
			auto button_mask = iem->get_button_mask();

			str = str + " Pos: " + itos(pos.x) + "," + itos(pos.y) + " Rel: " + itos(rel.x) + "," + itos(rel.y) + " Mask: ";
			for (int i = 0; i < 8; i++) {
				if (button_mask.has_flag(MouseButtonMask(1 << i)))
					str += itos(i + 1);
			}
			str += " Mod: ";
			add_mod_str(iem);

			return str;
		} break;
		case InputEventType::MOUSE_BUTTON: {
			str += "Event: Button ";
			Ref<InputEventMouseButton> iem = p_v4_event;
			auto pressed = iem->is_pressed();
			auto pos = iem->get_position();
			auto button_index = iem->get_button_index();
			auto button_mask = iem->get_button_mask();

			str = str + "Pressed: " + itos(pressed) + " Pos: " + itos(pos.x) + "," + itos(pos.y) + " Button: " + itos((int64_t)button_index) + " Mask: ";
			for (int i = 0; i < 8; i++) {
				if (button_mask.has_flag(MouseButtonMask(1 << i)))
					str += itos(i + 1);
			}
			str += " Mod: ";
			add_mod_str(iem);

			str += String(" DoubleClick: ") + (iem->is_double_click() ? "Yes" : "No");

			return str;

		} break;
		case InputEventType::JOY_MOTION: {
			str += "Event: JoystickMotion ";
			Ref<InputEventJoypadMotion> iem = p_v4_event;
			auto axis = iem->get_axis();
			auto axis_value = iem->get_axis_value();

			str = str + "Axis: " + itos((int64_t)axis) + " Value: " + rtos(axis_value);
			return str;

		} break;
		case InputEventType::JOY_BUTTON: {
			str += "Event: JoystickButton ";
			Ref<InputEventJoypadButton> iem = p_v4_event;
			auto pressed = iem->is_pressed();
			auto button_index = iem->get_button_index();
			auto pressure = iem->get_pressure();

			str = str + "Pressed: " + itos(pressed) + " Index: " + itos((int64_t)button_index) + " pressure " + rtos(pressure);
			return str;

		} break;
		case InputEventType::SCREEN_TOUCH: {
			str += "Event: ScreenTouch ";
			Ref<InputEventScreenTouch> iem = p_v4_event;
			auto pressed = iem->is_pressed();
			auto index = iem->get_index();
			auto pos = iem->get_position();

			str = str + "Pressed: " + itos(pressed) + " Index: " + itos(index) + " pos " + rtos(pos.x) + "," + rtos(pos.y);
			return str;

		} break;
		case InputEventType::SCREEN_DRAG: {
			str += "Event: ScreenDrag ";
			Ref<InputEventScreenDrag> iem = p_v4_event;
			auto index = iem->get_index();
			auto pos = iem->get_position();

			str = str + " Index: " + itos(index) + " pos " + rtos(pos.x) + "," + rtos(pos.y);
			return str;

		} break;
		case InputEventType::ACTION: {
			Ref<InputEventAction> iem = p_v4_event;
			auto action = iem->get_action();
			auto pressed = iem->is_pressed();

			str += "Event: Action: " + action + " Pressed: " + itos(pressed);
			return str;

		} break;
		default: {
			return "Event: None";
		} break;
	}

	return "";
}

String InputEventParserV2::v4_input_event_to_v2_res_text(const Variant &r_v, bool is_pcfg) {
	Ref<InputEvent> ev = r_v;
	String prefix = is_pcfg ? "" : "InputEvent(";

	if (ev->is_class("InputEventKey")) {
		Ref<InputEventKey> evk = ev;
		String mods;
		if (evk->is_ctrl_pressed())
			mods += "C";
		if (evk->is_shift_pressed())
			mods += "S";
		if (evk->is_alt_pressed())
			mods += "A";
		if (evk->is_meta_pressed())
			mods += "M";
		if (mods != "")
			mods = (is_pcfg ? ", " : ",") + mods;
		if (is_pcfg) {
			prefix += "key(";
			prefix += get_v2_string_from_iek(evk);
		} else {
			prefix += "KEY,";
			prefix += itos((int64_t)get_v2_key_from_iek(evk));
		}
		return prefix + mods + ")";
	}
	if (ev->is_class("InputEventMouseButton")) {
		Ref<InputEventMouseButton> evk = ev;
		if (is_pcfg) {
			prefix += "mbutton(" + itos(evk->get_device()) + ", ";
		} else {
			prefix += "MBUTTON,";
		}
		return prefix + itos((int64_t)evk->get_button_index()) + ")";
	}
	if (ev->is_class("InputEventJoypadButton")) {
		Ref<InputEventJoypadButton> evk = ev;
		if (is_pcfg) {
			prefix += "jbutton(" + itos(evk->get_device()) + ", ";
		} else {
			prefix += "JBUTTON,";
		}
		return prefix + itos((int64_t)evk->get_button_index()) + ")";
	}
	if (ev->is_class("InputEventJoypadMotion")) {
		Ref<InputEventJoypadMotion> evk = ev;
		JoyAxis joyaxis = evk->get_axis();
		if (is_pcfg) {
			// pcfg is jaxis(<device>,<axis> * 2 + (<axis_value> < 0 ? 0 : 1))
			prefix += "jaxis(" + itos(evk->get_device()) + ", " + itos((int64_t)joyaxis * 2 + (evk->get_axis_value() < 0 ? 0 : 1));
		} else {
			// resource is JAXIS,axis,axis_value
			prefix += "JAXIS," + itos((int64_t)evk->get_axis()) + "," + itos((int64_t)evk->get_axis_value());
		}
		return prefix + ")";
	}
	ERR_FAIL_V_MSG("", "Cannot store input events of type " + ev->get_class_name() + " in v2 input event strings!");
}

Ref<InputEvent> convert_v2_joy_button_event_to_v4(uint32_t btn_index) {
	Ref<InputEventJoypadButton> iej;
	iej.instantiate();
	iej->set_button_index(JoyButton(btn_index));
	return iej;
}

Error InputEventParserV2::decode_input_event(Variant &r_variant, const uint8_t *p_buffer, int p_len, int *r_len) {
	ERR_FAIL_COND_V(p_len < 8, ERR_INVALID_DATA);
	Ref<InputEvent> ie;
	uint32_t ie_type = decode_uint32(&p_buffer[0]);
	uint32_t ie_device = decode_uint32(&p_buffer[4]);
	// 0 padding at byte 8
	if (r_len) {
		(*r_len) += 12;
	}

	switch ((V2Type)ie_type) {
		case V2Type::KEY: { // KEY
			ERR_FAIL_COND_V(p_len < 20, ERR_INVALID_DATA);
			uint32_t mods = decode_uint32(&p_buffer[12]);
			uint32_t scancode = decode_uint32(&p_buffer[16]);
			Ref<InputEventKey> iek;
			iek = InputEventKey::create_reference(convert_v2_key_to_v4_key(V2InputEvent::V2KeyList(scancode)));
			if (mods & V2InputEvent::KEY_MASK_SHIFT) {
				iek->set_shift_pressed(true);
			}
			if (mods & V2InputEvent::KEY_MASK_CTRL) {
				iek->set_ctrl_pressed(true);
			}
			if (mods & V2InputEvent::KEY_MASK_ALT) {
				iek->set_alt_pressed(true);
			}
			if (mods & V2InputEvent::KEY_MASK_META) {
				iek->set_meta_pressed(true);
			}
			// It looks like KPAD and GROUP_SWITCH were not actually used in v2?
			// In either case, their masks are the same in v2 and v4
			ie = iek;
			if (r_len) {
				(*r_len) += 8;
			}

		} break;
		case V2Type::MOUSE_BUTTON: { // MOUSE_BUTTON
			ERR_FAIL_COND_V(p_len < 16, ERR_INVALID_DATA);
			uint32_t btn_index = decode_uint32(&p_buffer[12]);
			Ref<InputEventMouseButton> iem;
			iem.instantiate();
			iem->set_button_index(MouseButton(btn_index));
			ie = iem;
			if (r_len) {
				(*r_len) += 4;
			}

		} break;
		case V2Type::JOYSTICK_BUTTON: { // JOYSTICK_BUTTON
			ERR_FAIL_COND_V(p_len < 16, ERR_INVALID_DATA);
			uint32_t btn_index = decode_uint32(&p_buffer[12]);
			ie = convert_v2_joy_button_event_to_v4(btn_index);
			if (r_len) {
				(*r_len) += 4;
			}
		} break;
		case V2Type::SCREEN_TOUCH: { // SCREEN_TOUCH
			ERR_FAIL_COND_V(p_len < 16, ERR_INVALID_DATA);
			uint32_t index = decode_uint32(&p_buffer[12]);
			Ref<InputEventScreenTouch> iej;
			iej.instantiate();
			iej->set_index(index);
			ie = iej;
			if (r_len) {
				(*r_len) += 4;
			}

		} break;
		case V2Type::JOYSTICK_MOTION: { // JOYSTICK_MOTION
			ERR_FAIL_COND_V(p_len < 20, ERR_INVALID_DATA);
			uint32_t axis = decode_uint32(&p_buffer[12]);
			float axis_value = decode_float(&p_buffer[16]);
			Ref<InputEventJoypadMotion> iej;
			iej.instantiate();
			iej->set_axis(JoyAxis(axis));
			iej->set_axis_value(axis_value);
			ie = iej;
			if (r_len) {
				(*r_len) += 8;
			}
		} break;
		default: { // NONE or invalid
		} break;
	}
	if (ie.is_valid()) {
		ie->set_device(ie_device);
	}
	r_variant = ie;
	return OK;
}

Key convert_v2_key_string_to_v4_keycode(const String &p_code) {
	const _KeyCodeText *kct = &_keycodes[0];
	while (kct->text) {
		if (p_code.nocasecmp_to(kct->text) == 0) {
			return kct->code;
		}
		kct++;
	}

	return Key::NONE;
}

#define ERR_PARSE_V2INPUTEVENT_FAIL(c_type, error_string) \
	if (token.type != c_type) {                           \
		r_err_str = error_string;                         \
		return ERR_PARSE_ERROR;                           \
	}

#define EXPECT_TOKEN(token_type, error_string)                  \
	VariantParser::get_token(p_stream, token, line, r_err_str); \
	ERR_PARSE_V2INPUTEVENT_FAIL(token_type, error_string)

#define EXPECT_COMMA_MSG(error_string) EXPECT_TOKEN(VariantParser::TK_COMMA, error_string)
#define EXPECT_COMMA() EXPECT_COMMA_MSG("Expected comma in InputEvent variant")
#define EXPECT_PAREN_CLOSE() EXPECT_TOKEN(VariantParser::TK_PARENTHESIS_CLOSE, "Expected ')'")

Error InputEventParserV2::parse_input_event_construct_v2(VariantParser::Stream *p_stream, Variant &r_v, int &line, String &r_err_str, String id) {
	VariantParser::Token token;
	VariantParser::get_token(p_stream, token, line, r_err_str);
	if (token.type != VariantParser::TK_PARENTHESIS_OPEN) {
		r_err_str = "Expected '('";
		return ERR_PARSE_ERROR;
	}
	bool is_pcfg = !id.is_empty();
	// Old versions of engine.cfg did not have the "InputEvent" identifier before the input event type,
	if (!is_pcfg) {
		EXPECT_TOKEN(VariantParser::TK_IDENTIFIER, "Expected identifier");
		id = token.value;
		if (id != "NONE") {
			EXPECT_COMMA();
		}
	} else {
		id = id.to_upper();
	}

	Ref<InputEvent> ie;
	int device_id = -1;

	if (id == "NONE") {
		EXPECT_PAREN_CLOSE();
	} else if (id == "KEY") {
		Ref<InputEventKey> iek;
		iek.instantiate();

		VariantParser::get_token(p_stream, token, line, r_err_str);
		if (token.type == VariantParser::TK_IDENTIFIER || (token.type == VariantParser::TK_NUMBER && (int)token.value < 10)) {
			String name = token.type == VariantParser::TK_IDENTIFIER ? token.value.operator String() : String::num_int64((int64_t)token.value);
			// V2 identifier with spaces, e.g. "Kp Enter"
			if (v2_keys_with_spaces_prefixes.has(name)) {
				VariantParser::get_token(p_stream, token, line, r_err_str);
				if (token.type != VariantParser::TK_IDENTIFIER && token.type != VariantParser::TK_NUMBER) {
					r_err_str = "Expected identifier";
					return ERR_PARSE_ERROR;
				}
				name += " " + (token.type == VariantParser::TK_IDENTIFIER ? token.value.operator String() : String::num_int64((int64_t)token.value));
			}
			iek->set_keycode(convert_v2_key_string_to_v4_keycode(name));
		} else if (token.type == VariantParser::TK_NUMBER) {
			iek->set_keycode(convert_v2_key_to_v4_key(token.value));
		} else {
			r_err_str = "Expected string or integer for keycode";
			return ERR_PARSE_ERROR;
		}

		VariantParser::get_token(p_stream, token, line, r_err_str);

		if (token.type == VariantParser::TK_COMMA) {
			VariantParser::get_token(p_stream, token, line, r_err_str);

			if (token.type != VariantParser::TK_IDENTIFIER) {
				r_err_str = "Expected identifier with modifier flas";
				return ERR_PARSE_ERROR;
			}

			String mods = token.value;

			if (mods.findn("C") != -1) {
				iek->set_ctrl_pressed(true);
			}
			if (mods.findn("A") != -1) {
				iek->set_alt_pressed(true);
			}
			if (mods.findn("S") != -1) {
				iek->set_shift_pressed(true);
			}
			if (mods.findn("M") != -1) {
				iek->set_meta_pressed(true);
			}
			EXPECT_PAREN_CLOSE();
		} else if (token.type != VariantParser::TK_PARENTHESIS_CLOSE) {
			r_err_str = "Expected ')' or modifier flags.";
			return ERR_PARSE_ERROR;
		}
		ie = iek;
	} else if (id == "MBUTTON") {
		if (is_pcfg) {
			EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected device id");
			device_id = (int)token.value;
			EXPECT_COMMA();
		}

		Ref<InputEventMouseButton> iek;
		iek.instantiate();

		EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected button index");
		iek->set_button_index(MouseButton((int)token.value));
		EXPECT_PAREN_CLOSE();
		ie = iek;
	} else if (id == "JBUTTON") {
		if (is_pcfg) {
			EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected device id");
			device_id = (int)token.value;
			EXPECT_COMMA();
		}

		EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected button index");
		uint32_t btn_index = token.value;
		ie = convert_v2_joy_button_event_to_v4(btn_index);
		EXPECT_PAREN_CLOSE();
	} else if (id == "JAXIS") {
		Ref<InputEventJoypadMotion> iek;
		iek.instantiate();
		int axis;
		int axis_value;
		if (!is_pcfg) {
			EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected axis index");
			axis = (int)token.value;
			EXPECT_COMMA_MSG("Expected comma after axis index");
			EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected axis sign");
			axis_value = (int)token.value;
		} else {
			EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected device id");
			device_id = (int)token.value;
			EXPECT_COMMA();
			EXPECT_TOKEN(VariantParser::TK_NUMBER, "Expected axis value");
			int val = (int)token.value;
			axis = val >> 1;
			axis_value = val & 1 ? 1 : -1;
		}

		iek->set_axis(JoyAxis(axis));
		iek->set_axis_value(axis_value);

		EXPECT_PAREN_CLOSE();
		ie = iek;
	} else {
		r_err_str = "Invalid input event type.";
		return ERR_PARSE_ERROR;
	}

	if (device_id != -1) {
		ie->set_device(device_id);
	}

	r_v = ie;

	return OK;
}

#undef EXPECT_TOKEN
#undef EXPECT_COMMA_MSG
#undef EXPECT_COMMA
#undef EXPECT_PAREN_CLOSE
#undef ERR_PARSE_V2INPUTEVENT_FAIL

const HashMap<String, String> InputEventConverterCompat::old_prop_to_new_prop = {
	// InputEventWithModifiers properties
	{ "alt", "alt_pressed" },
	{ "shift", "shift_pressed" },
	{ "control", "ctrl_pressed" },
	{ "meta", "meta_pressed" },
	{ "command", "command_or_control_autoremap" },

	// InputEventKey properties
	{ "scancode", "keycode" },
	{ "physical_scancode", "physical_keycode" },
	{ "unicode", "unicode" },
	{ "echo", "echo" },

	// InputEventMouse properties
	{ "button_mask", "button_mask" },
	{ "position", "position" },
	{ "global_position", "global_position" },

	// InputEventMouseButton properties
	{ "factor", "factor" },
	{ "button_index", "button_index" },
	{ "pressed", "pressed" },
	{ "canceled", "canceled" },
	{ "doubleclick", "double_click" },

	// InputEventMouseMotion properties
	{ "tilt", "tilt" },
	{ "pressure", "pressure" },
	{ "pen_inverted", "pen_inverted" },
	{ "relative", "relative" },
	{ "speed", "velocity" },

	// InputEventJoypadMotion properties
	{ "axis", "axis" },
	{ "axis_value", "axis_value" },

	// InputEventJoypadButton properties
	{ "button_index", "button_index" },
	{ "pressed", "pressed" },
	{ "pressure", "pressure" },

	// InputEventScreenTouch properties
	{ "index", "index" },
	{ "position", "position" },
	{ "pressed", "pressed" },
	{ "canceled", "canceled" },
	{ "double_tap", "double_tap" },

	// InputEventScreenDrag properties
	{ "index", "index" },
	{ "position", "position" },
	{ "relative", "relative" },
	{ "speed", "velocity" },

	// InputEventAction properties
	{ "action", "action" },
	{ "pressed", "pressed" },
	{ "strength", "strength" },

	// InputEventMagnifyGesture properties
	{ "factor", "factor" },

	// InputEventPanGesture properties
	{ "delta", "delta" },

	// // InputEventMIDI properties
	// { "channel", "channel" },
	// { "message", "message" },
	// { "pitch", "pitch" },
	// { "velocity", "velocity" },
	// { "instrument", "instrument" },
	// { "pressure", "pressure" },
	// { "controller_number", "controller_number" },
	// { "controller_value", "controller_value" },
};

namespace {
// because of "velocity"
static const HashMap<String, String> midi_prop_map = {
	{ "channel", "channel" },
	{ "message", "message" },
	{ "pitch", "pitch" },
	{ "velocity", "velocity" },
	{ "instrument", "instrument" },
	{ "pressure", "pressure" },
	{ "controller_number", "controller_number" },
	{ "controller_value", "controller_value" },
};
} // namespace

Ref<Resource> InputEventConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	Ref<InputEvent> ie;
	String class_name = res->get_original_class();
	if (class_name == "InputEventMIDI") {
		return get_real_from_missing_resource(res, p_type, midi_prop_map);
	}
	auto real = get_real_from_missing_resource(res, p_type, InputEventConverterCompat::old_prop_to_new_prop);
	ERR_FAIL_COND_V_MSG(real.is_null(), Ref<InputEvent>(), "Failed to create input event");
	return real;
}

const static HashSet<String> valid_types = {
	"InputEvent",
	"InputEventWithModifiers",
	"InputEventKey",
	"InputEventMouse",
	"InputEventMouseButton",
	"InputEventMouseMotion",
	"InputEventJoypadMotion",
	"InputEventJoypadButton",
	"InputEventScreenTouch",
	"InputEventScreenDrag",
	"InputEventAction",
	"InputEventGesture",
	"InputEventMagnifyGesture",
	"InputEventPanGesture",
	"InputEventMIDI",
};

bool InputEventConverterCompat::handles_type_static(const String &p_type, int ver_major) {
	if (ver_major != 3) {
		return false;
	}
	return valid_types.has(p_type);
}

bool InputEventConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return handles_type_static(p_type, ver_major);
}

Ref<MissingResource> InputEventConverterCompat::convert_back(const Ref<Resource> &res, int ver_major, Error *r_error) {
	if (res->get_class() == "InputEventMIDI") {
		return get_missing_resource_from_real(res, ver_major, midi_prop_map);
	}
	// otherwise instantiate the new properties
	HashMap<String, String> required_prop_map;
	for (auto &[old_prop, new_prop] : InputEventConverterCompat::old_prop_to_new_prop) {
		required_prop_map[new_prop] = old_prop;
	}
	auto mr = get_missing_resource_from_real(res, ver_major, required_prop_map);
	ERR_FAIL_COND_V_MSG(mr.is_null(), Ref<MissingResource>(), "Failed to create missing resource");
	return mr;
}

V2InputEvent::V2Type InputEventParserV2::get_v2_type(const InputEventType &p_event) {
	switch (p_event) {
		case InputEventType::KEY:
			return V2InputEvent::V2Type::KEY;
		case InputEventType::MOUSE_BUTTON:
			return V2InputEvent::V2Type::MOUSE_BUTTON;
		case InputEventType::MOUSE_MOTION:
			return V2InputEvent::V2Type::MOUSE_MOTION;
		case InputEventType::JOY_BUTTON:
			return V2InputEvent::V2Type::JOYSTICK_BUTTON;
		case InputEventType::SCREEN_TOUCH:
			return V2InputEvent::V2Type::SCREEN_TOUCH;
		case InputEventType::SCREEN_DRAG:
			return V2InputEvent::V2Type::SCREEN_DRAG;
		case InputEventType::ACTION:
			return V2InputEvent::V2Type::ACTION;
		case InputEventType::JOY_MOTION:
			return V2InputEvent::V2Type::JOYSTICK_MOTION;
		default:
			return V2InputEvent::V2Type::NONE;
	}
}

int InputEventParserV2::encode_input_event(const Ref<InputEvent> &ie, uint8_t *p_buffer) {
	uint8_t *buf = p_buffer;
	// r_len already incremented
	if (buf) {
		encode_uint32(InputEventParserV2::get_v2_type(ie->get_type()), &buf[0]);
		encode_uint32(ie->get_device(), &buf[4]);
		encode_uint32(0, &buf[8]);
	}
	int llen = 12;
	switch (ie->get_type()) {
		case InputEventType::KEY: {
			if (buf) {
				Ref<InputEventKey> iek = ie;
				uint32_t mods = 0;
				if (iek->is_shift_pressed())
					mods |= V2InputEvent::KEY_MASK_SHIFT;
				if (iek->is_ctrl_pressed())
					mods |= V2InputEvent::KEY_MASK_CTRL;
				if (iek->is_alt_pressed())
					mods |= V2InputEvent::KEY_MASK_ALT;
				if (iek->is_meta_pressed())
					mods |= V2InputEvent::KEY_MASK_META;
				encode_uint32(mods, &buf[llen]);
				auto v2_keycode = get_v2_key_from_iek(iek);

				encode_uint32(v2_keycode, &buf[llen + 4]);
			}
			llen += 8;
		} break;
		case InputEventType::MOUSE_BUTTON: {
			if (buf) {
				Ref<InputEventMouseButton> iem = ie;
				encode_uint32((uint32_t)iem->get_button_index(), &buf[llen]);
			}
			llen += 4;
		} break;

		case InputEventType::JOY_BUTTON: {
			if (buf) {
				Ref<InputEventJoypadButton> iej = ie;
				encode_uint32((uint32_t)iej->get_button_index(), &buf[llen]);
			}
			llen += 4;
		} break;
		case InputEventType::SCREEN_TOUCH: {
			if (buf) {
				Ref<InputEventScreenTouch> ies = ie;
				encode_uint32((uint32_t)ies->get_index(), &buf[llen]);
			}
			llen += 4;
		} break;
		case InputEventType::JOY_MOTION: {
			if (buf) {
				Ref<InputEventJoypadMotion> iejm = ie;
				encode_uint32((uint32_t)iejm->get_axis(), &buf[llen]);
				encode_float(iejm->get_axis_value(), &buf[llen + 4]);
			}
			llen += 8;
		} break;
		default:
			break;
	}
	if (buf) {
		encode_uint32(llen, &buf[8]);
	}
	return llen;
}

HashMap<Key, String> InputEventParserV2::get_key_code_to_v2_string_map() {
	HashMap<Key, String> map;
	for (auto &[key, value] : _keycodes) {
		if (value == nullptr) {
			continue;
		}
		map[key] = value;
	}
	return map;
}
