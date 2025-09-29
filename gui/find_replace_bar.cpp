#include "find_replace_bar.h"
#include "core/variant/array.h"
#include "editor/gdre_icons.gen.h"
#include "utility/gdre_settings.h"
#ifdef MODULE_SVG_ENABLED
#include "modules/svg/image_loader_svg.h"
#endif
#include "core/input/input_map.h"
#include "core/input/shortcut.h"
#include "core/os/keyboard.h"
#include "scene/resources/image_texture.h"

namespace {
// TODO: move this elsewhere
static Ref<ImageTexture> generate_icon(int p_index) {
	Ref<Image> img = memnew(Image);

#ifdef MODULE_SVG_ENABLED
	// Upsample icon generation only if the scale isn't an integer multiplier.
	// Generating upsampled icons is slower, and the benefit is hardly visible
	// with integer scales.
	ImageLoaderSVG img_loader;
	img_loader.create_image_from_string(img, gdre_icons_sources[p_index], 1.0, false, false);
#endif

	return ImageTexture::create_from_image(img);
}

Ref<ImageTexture> get_gdre_icon(const StringName &p_name) {
	for (int i = 0; i < gdre_icons_count; i++) {
		if (gdre_icons_names[i] == p_name) {
			return generate_icon(i);
		}
	}
	return nullptr;
}

} //namespace

void GDREFindReplaceBar::_notification(int p_what) {
	switch (p_what) {
		// case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
		// 	if (!EditorThemeManager::is_generated_theme_outdated()) {
		// 		break;
		// 	}
		// 	[[fallthrough]];
		// }
		case NOTIFICATION_READY: {
			find_prev->set_button_icon(get_gdre_icon(SNAME("MoveUp")));
			find_next->set_button_icon(get_gdre_icon(SNAME("MoveDown")));
			hide_button->set_button_icon(get_gdre_icon(SNAME("Close")));
			_update_toggle_replace_button(replace_text->is_visible_in_tree());
		} break;

		case NOTIFICATION_TRANSLATION_CHANGED: {
			if (matches_label->is_visible()) {
				_update_matches_display();
			}
			[[fallthrough]];
		}
		case NOTIFICATION_LAYOUT_DIRECTION_CHANGED: {
			_update_toggle_replace_button(replace_text->is_visible_in_tree());
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			set_process_input(is_visible_in_tree());
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			matches_label->add_theme_color_override(SceneStringName(font_color), results_count > 0 ? get_theme_color(SceneStringName(font_color), SNAME("Label")) : get_theme_color(SNAME("error_color"), SNAME("Editor")));
		} break;

		case NOTIFICATION_PREDELETE: {
			// if (base_text_editor) {
			// 	base_text_editor->remove_find_replace_bar();
			// 	base_text_editor = nullptr;
			// }
		} break;
	}
}

// Implemented in input(..) as the LineEdit consumes the Escape pressed key.
void GDREFindReplaceBar::input(const Ref<InputEvent> &p_event) {
	ERR_FAIL_COND(p_event.is_null());

	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_action_pressed(SNAME("ui_cancel"), false, true)) {
		Control *focus_owner = get_viewport()->gui_get_focus_owner();

		if (text_editor->has_focus() || (focus_owner && is_ancestor_of(focus_owner))) {
			_hide_bar();
			accept_event();
		}
	}
}

void GDREFindReplaceBar::_update_flags(bool p_direction_backwards) {
	flags = 0;

	if (is_whole_words()) {
		flags |= TextEdit::SEARCH_WHOLE_WORDS;
	}
	if (is_case_sensitive()) {
		flags |= TextEdit::SEARCH_MATCH_CASE;
	}
	if (p_direction_backwards) {
		flags |= TextEdit::SEARCH_BACKWARDS;
	}
}

bool GDREFindReplaceBar::_search(uint32_t p_flags, int p_from_line, int p_from_col) {
	if (!preserve_cursor) {
		text_editor->remove_secondary_carets();
	}
	String text = get_search_text();
	Point2i pos = text_editor->search(text, p_flags, p_from_line, p_from_col);

	if (pos.x != -1) {
		if (!preserve_cursor && !is_selection_only()) {
			text_editor->unfold_line(pos.y);
			text_editor->select(pos.y, pos.x, pos.y, pos.x + text.length());
			text_editor->center_viewport_to_caret(0);
			text_editor->set_code_hint("");
			text_editor->cancel_code_completion();

			line_col_changed_for_result = true;
		}

		text_editor->set_search_text(text);
		text_editor->set_search_flags(p_flags);

		result_line = pos.y;
		result_col = pos.x;

		_update_results_count();
	} else {
		results_count = 0;
		result_line = -1;
		result_col = -1;
		text_editor->set_search_text("");
		text_editor->set_search_flags(p_flags);
	}

	_update_matches_display();

	return pos.x != -1;
}

void GDREFindReplaceBar::_replace() {
	text_editor->begin_complex_operation();
	text_editor->remove_secondary_carets();
	bool selection_enabled = text_editor->has_selection(0);
	Point2i selection_begin, selection_end;
	if (selection_enabled) {
		selection_begin = Point2i(text_editor->get_selection_from_line(0), text_editor->get_selection_from_column(0));
		selection_end = Point2i(text_editor->get_selection_to_line(0), text_editor->get_selection_to_column(0));
	}

	String repl_text = get_replace_text();
	int search_text_len = get_search_text().length();

	if (selection_enabled && is_selection_only()) {
		// Restrict search_current() to selected region.
		text_editor->set_caret_line(selection_begin.width, false, true, -1, 0);
		text_editor->set_caret_column(selection_begin.height, true, 0);
	}

	if (search_current()) {
		text_editor->unfold_line(result_line);
		text_editor->select(result_line, result_col, result_line, result_col + search_text_len, 0);

		if (selection_enabled && is_selection_only()) {
			Point2i match_from(result_line, result_col);
			Point2i match_to(result_line, result_col + search_text_len);
			if (!(match_from < selection_begin || match_to > selection_end)) {
				text_editor->insert_text_at_caret(repl_text, 0);
				if (match_to.x == selection_end.x) {
					// Adjust selection bounds if necessary.
					selection_end.y += repl_text.length() - search_text_len;
				}
			}
		} else {
			text_editor->insert_text_at_caret(repl_text, 0);
		}
	}
	text_editor->end_complex_operation();
	results_count = -1;
	results_count_to_current = -1;
	needs_to_count_results = true;

	if (selection_enabled && is_selection_only()) {
		// Reselect in order to keep 'Replace' restricted to selection.
		text_editor->select(selection_begin.x, selection_begin.y, selection_end.x, selection_end.y, 0);
	} else {
		text_editor->deselect(0);
	}
}

void GDREFindReplaceBar::_replace_all() {
	text_editor->begin_complex_operation();
	text_editor->remove_secondary_carets();
	text_editor->disconnect(SceneStringName(text_changed), callable_mp(this, &GDREFindReplaceBar::_editor_text_changed));
	// Line as x so it gets priority in comparison, column as y.
	Point2i orig_cursor(text_editor->get_caret_line(0), text_editor->get_caret_column(0));
	Point2i prev_match = Point2(-1, -1);

	bool selection_enabled = text_editor->has_selection(0);
	if (!is_selection_only()) {
		text_editor->deselect();
		selection_enabled = false;
	} else {
		result_line = -1;
		result_col = -1;
	}

	Point2i selection_begin, selection_end;
	if (selection_enabled) {
		selection_begin = Point2i(text_editor->get_selection_from_line(0), text_editor->get_selection_from_column(0));
		selection_end = Point2i(text_editor->get_selection_to_line(0), text_editor->get_selection_to_column(0));
	}

	int vsval = text_editor->get_v_scroll();

	String repl_text = get_replace_text();
	int search_text_len = get_search_text().length();

	int rc = 0;

	replace_all_mode = true;

	if (selection_enabled && is_selection_only()) {
		text_editor->set_caret_line(selection_begin.width, false, true, -1, 0);
		text_editor->set_caret_column(selection_begin.height, true, 0);
	} else {
		text_editor->set_caret_line(0, false, true, -1, 0);
		text_editor->set_caret_column(0, true, 0);
	}

	if (search_current()) {
		do {
			// Replace area.
			Point2i match_from(result_line, result_col);
			Point2i match_to(result_line, result_col + search_text_len);

			if (match_from < prev_match) {
				break; // Done.
			}

			prev_match = Point2i(result_line, result_col + repl_text.length());

			text_editor->unfold_line(result_line);
			text_editor->select(result_line, result_col, result_line, match_to.y, 0);

			if (selection_enabled) {
				if (match_from < selection_begin || match_to > selection_end) {
					break; // Done.
				}

				// Replace but adjust selection bounds.
				text_editor->insert_text_at_caret(repl_text, 0);
				if (match_to.x == selection_end.x) {
					selection_end.y += repl_text.length() - search_text_len;
				}

			} else {
				// Just replace.
				text_editor->insert_text_at_caret(repl_text, 0);
			}

			rc++;
		} while (search_next());
	}

	text_editor->end_complex_operation();

	replace_all_mode = false;

	// Restore editor state (selection, cursor, scroll).
	text_editor->set_caret_line(orig_cursor.x, false, true, 0, 0);
	text_editor->set_caret_column(orig_cursor.y, true, 0);

	if (selection_enabled) {
		// Reselect.
		text_editor->select(selection_begin.x, selection_begin.y, selection_end.x, selection_end.y, 0);
	}

	text_editor->set_v_scroll(vsval);
	matches_label->add_theme_color_override(SceneStringName(font_color), rc > 0 ? get_theme_color(SceneStringName(font_color), SNAME("Label")) : get_theme_color(SNAME("error_color"), SNAME("Editor")));
	matches_label->set_text(vformat(RTR("%d replaced."), rc));

	callable_mp((Object *)text_editor, &Object::connect).call_deferred(SceneStringName(text_changed), callable_mp(this, &GDREFindReplaceBar::_editor_text_changed), 0U);
	results_count = -1;
	results_count_to_current = -1;
	needs_to_count_results = true;
}

void GDREFindReplaceBar::_get_search_from(int &r_line, int &r_col, SearchMode p_search_mode) {
	if (!text_editor->has_selection(0) || is_selection_only()) {
		r_line = text_editor->get_caret_line(0);
		r_col = text_editor->get_caret_column(0);

		if (p_search_mode == SEARCH_PREV && r_line == result_line && r_col >= result_col && r_col <= result_col + get_search_text().length()) {
			r_col = result_col;
		}
		return;
	}

	if (p_search_mode == SEARCH_NEXT) {
		r_line = text_editor->get_selection_to_line();
		r_col = text_editor->get_selection_to_column();
	} else {
		r_line = text_editor->get_selection_from_line();
		r_col = text_editor->get_selection_from_column();
	}
}

void GDREFindReplaceBar::_update_results_count() {
	int caret_line, caret_column;
	_get_search_from(caret_line, caret_column, SEARCH_CURRENT);
	bool match_selected = caret_line == result_line && caret_column == result_col && !is_selection_only() && text_editor->has_selection(0);

	if (match_selected && !needs_to_count_results && result_line != -1 && results_count_to_current > 0) {
		results_count_to_current += (flags & TextEdit::SEARCH_BACKWARDS) ? -1 : 1;

		if (results_count_to_current > results_count) {
			results_count_to_current = results_count_to_current - results_count;
		} else if (results_count_to_current <= 0) {
			results_count_to_current = results_count;
		}

		return;
	}

	String searched = get_search_text();
	if (searched.is_empty()) {
		return;
	}

	needs_to_count_results = !match_selected;

	results_count = 0;
	results_count_to_current = 0;

	for (int i = 0; i < text_editor->get_line_count(); i++) {
		String line_text = text_editor->get_line(i);

		int col_pos = 0;

		bool searched_start_is_symbol = is_symbol(searched[0]);
		bool searched_end_is_symbol = is_symbol(searched[searched.length() - 1]);

		while (true) {
			col_pos = is_case_sensitive() ? line_text.find(searched, col_pos) : line_text.findn(searched, col_pos);

			if (col_pos == -1) {
				break;
			}

			if (is_whole_words()) {
				if (!searched_start_is_symbol && col_pos > 0 && !is_symbol(line_text[col_pos - 1])) {
					col_pos += searched.length();
					continue;
				}
				if (!searched_end_is_symbol && col_pos + searched.length() < line_text.length() && !is_symbol(line_text[col_pos + searched.length()])) {
					col_pos += searched.length();
					continue;
				}
			}

			results_count++;

			if (i <= result_line && col_pos <= result_col) {
				results_count_to_current = results_count;
			}
			if (i == result_line && col_pos < result_col && col_pos + searched.length() > result_col) {
				// Searching forwards and backwards with repeating text can lead to different matches.
				col_pos = result_col;
			}
			col_pos += searched.length();
		}
	}
	if (!match_selected) {
		// Current result should refer to the match before the caret, if the caret is not on a match.
		if (caret_line != result_line || caret_column != result_col) {
			results_count_to_current -= 1;
		}
		if (results_count_to_current == 0 && (caret_line > result_line || (caret_line == result_line && caret_column > result_col))) {
			// Caret is after all matches.
			results_count_to_current = results_count;
		}
	}
}

void GDREFindReplaceBar::_update_matches_display() {
	if (search_text->get_text().is_empty() || results_count == -1) {
		matches_label->hide();
	} else {
		matches_label->show();

		matches_label->add_theme_color_override(SceneStringName(font_color), results_count > 0 ? get_theme_color(SceneStringName(font_color), SNAME("Label")) : get_theme_color(SNAME("error_color"), SNAME("Editor")));

		if (results_count == 0) {
			matches_label->set_text(RTR("No match"));
		} else if (results_count_to_current == -1) {
			matches_label->set_text(vformat(RTRN("%d match", "%d matches", results_count), results_count));
		} else {
			matches_label->set_text(vformat(RTRN("%d of %d match", "%d of %d matches", results_count), results_count_to_current, results_count));
		}
	}
	find_prev->set_disabled(results_count < 1);
	find_next->set_disabled(results_count < 1);
	replace->set_disabled(search_text->get_text().is_empty());
	replace_all->set_disabled(search_text->get_text().is_empty());
}

bool GDREFindReplaceBar::search_current() {
	_update_flags(false);

	int line, col;
	_get_search_from(line, col, SEARCH_CURRENT);

	return _search(flags, line, col);
}

bool GDREFindReplaceBar::search_prev() {
	if (is_selection_only() && !replace_all_mode) {
		return false;
	}

	if (!is_visible()) {
		popup_search(true);
	}

	String text = get_search_text();

	if ((flags & TextEdit::SEARCH_BACKWARDS) == 0) {
		needs_to_count_results = true;
	}

	_update_flags(true);

	int line, col;
	_get_search_from(line, col, SEARCH_PREV);

	col -= text.length();
	if (col < 0) {
		line -= 1;
		if (line < 0) {
			line = text_editor->get_line_count() - 1;
		}
		col = text_editor->get_line(line).length();
	}

	return _search(flags, line, col);
}

bool GDREFindReplaceBar::search_next() {
	if (is_selection_only() && !replace_all_mode) {
		return false;
	}

	if (!is_visible()) {
		popup_search(true);
	}

	if (flags & TextEdit::SEARCH_BACKWARDS) {
		needs_to_count_results = true;
	}

	_update_flags(false);

	int line, col;
	_get_search_from(line, col, SEARCH_NEXT);

	return _search(flags, line, col);
}

void GDREFindReplaceBar::_hide_bar() {
	if (replace_text->has_focus() || search_text->has_focus()) {
		text_editor->grab_focus();
	}

	text_editor->set_search_text("");
	result_line = -1;
	result_col = -1;
	hide();
}

void GDREFindReplaceBar::_update_toggle_replace_button(bool p_replace_visible) {
	String tooltip = p_replace_visible ? TTRC("Hide Replace") : TTRC("Show Replace");
	String shortcut = p_replace_visible ? get_action_description("ui_replace") : get_action_description("ui_find");
	toggle_replace_button->set_tooltip_text(vformat("%s (%s)", tooltip, shortcut));
	if (!replace_enabled) {
		toggle_replace_button->hide();
		return;
	}
	StringName rtl_compliant_arrow = is_layout_rtl() ? SNAME("GuiTreeArrowLeft") : SNAME("GuiTreeArrowRight");
	toggle_replace_button->set_button_icon(get_gdre_icon(p_replace_visible ? SNAME("GuiTreeArrowDown") : rtl_compliant_arrow));
}

void GDREFindReplaceBar::_show_search(bool p_with_replace, bool p_show_only) {
	show();
	if (p_show_only) {
		return;
	}

	const bool on_one_line = text_editor->has_selection(0) && text_editor->get_selection_from_line(0) == text_editor->get_selection_to_line(0);
	const bool focus_replace = p_with_replace && on_one_line;

	if (focus_replace) {
		search_text->deselect();
		callable_mp((Control *)replace_text, &Control::grab_focus).call_deferred();
	} else {
		replace_text->deselect();
		callable_mp((Control *)search_text, &Control::grab_focus).call_deferred();
	}

	if (on_one_line) {
		search_text->set_text(text_editor->get_selected_text(0));
		result_line = text_editor->get_selection_from_line();
		result_col = text_editor->get_selection_from_column();
	}

	if (!get_search_text().is_empty()) {
		if (focus_replace) {
			replace_text->select_all();
			replace_text->set_caret_column(replace_text->get_text().length());
		} else {
			search_text->select_all();
			search_text->set_caret_column(search_text->get_text().length());
		}

		preserve_cursor = true;
		_search_text_changed(get_search_text());
		preserve_cursor = false;
	}
}

void GDREFindReplaceBar::popup_search(bool p_show_only) {
	replace_text->hide();
	hbc_button_replace->hide();
	hbc_option_replace->hide();
	selection_only->set_pressed(false);
	_update_toggle_replace_button(false);

	_show_search(false, p_show_only);
}

void GDREFindReplaceBar::popup_replace() {
	if (!replace_enabled) {
		return;
	}

	if (!replace_text->is_visible_in_tree()) {
		replace_text->show();
		hbc_button_replace->show();
		hbc_option_replace->show();
		_update_toggle_replace_button(true);
	}

	selection_only->set_pressed(text_editor->has_selection(0) && text_editor->get_selection_from_line(0) < text_editor->get_selection_to_line(0));

	_show_search(true, false);
}

void GDREFindReplaceBar::_search_options_changed(bool p_pressed) {
	results_count = -1;
	results_count_to_current = -1;
	needs_to_count_results = true;
	search_current();
}

void GDREFindReplaceBar::_editor_text_changed() {
	results_count = -1;
	results_count_to_current = -1;
	needs_to_count_results = true;
	if (is_visible_in_tree()) {
		preserve_cursor = true;
		search_current();
		preserve_cursor = false;
	}
}

void GDREFindReplaceBar::_search_text_changed(const String &p_text) {
	results_count = -1;
	results_count_to_current = -1;
	needs_to_count_results = true;
	search_current();
}

void GDREFindReplaceBar::_search_text_submitted(const String &p_text) {
	if (Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
		search_prev();
	} else {
		search_next();
	}
}

void GDREFindReplaceBar::_replace_text_submitted(const String &p_text) {
	if (selection_only->is_pressed() && text_editor->has_selection(0)) {
		_replace_all();
		_hide_bar();
	} else if (Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
		_replace();
		search_prev();
	} else {
		_replace();
		search_next();
	}
}

void GDREFindReplaceBar::_toggle_replace_pressed() {
	bool replace_visible = replace_text->is_visible_in_tree();
	replace_visible ? popup_search(true) : popup_replace();
}

String GDREFindReplaceBar::get_search_text() const {
	return search_text->get_text();
}

String GDREFindReplaceBar::get_replace_text() const {
	return replace_text->get_text();
}

bool GDREFindReplaceBar::is_case_sensitive() const {
	return case_sensitive->is_pressed();
}

bool GDREFindReplaceBar::is_whole_words() const {
	return whole_words->is_pressed();
}

bool GDREFindReplaceBar::is_selection_only() const {
	return selection_only->is_pressed();
}

void GDREFindReplaceBar::set_text_edit(CodeEdit *p_text_editor) {
	if (p_text_editor == text_editor) {
		return;
	}

	if (text_editor) {
		text_editor->set_search_text(String());
		// base_text_editor->remove_find_replace_bar();
		// base_text_editor = nullptr;
		text_editor->disconnect(SceneStringName(text_changed), callable_mp(this, &GDREFindReplaceBar::_editor_text_changed));
		text_editor = nullptr;
	}

	if (!p_text_editor) {
		return;
	}

	results_count = -1;
	results_count_to_current = -1;
	needs_to_count_results = true;
	// base_text_editor = p_text_editor;
	// text_editor = base_text_editor->get_text_editor();
	text_editor = p_text_editor;
	text_editor->connect(SceneStringName(text_changed), callable_mp(this, &GDREFindReplaceBar::_editor_text_changed));

	_editor_text_changed();
}

Ref<Shortcut> get_or_create_shortcut(const String &p_action_name, const String &p_default_label, Key p_default_keycode, Key macos_override_keycode = Key::NONE) {
	Ref<Shortcut> shortcut = memnew(Shortcut);
	Array arr;

	if (InputMap::get_singleton()->has_action(p_action_name)) {
		auto events = InputMap::get_singleton()->action_get_events(p_action_name);
		if (events) {
			for (const auto &event : *events) {
				arr.push_back(event);
			}
		}
	}
	shortcut->set_name(p_default_label);
	if (arr.size() == 0) {
		Ref<InputEventKey> replace_event = InputEventKey::create_reference(p_default_keycode);
		arr.push_back(replace_event);
		if (macos_override_keycode != Key::NONE) {
			Ref<InputEventKey> macos_event = InputEventKey::create_reference(macos_override_keycode);
			arr.push_back(macos_event);
		}
		shortcut->set_events(arr);
	}
	shortcut->set_events(arr);

	return shortcut;
}

String GDREFindReplaceBar::get_action_description(const String &p_action_name) const {
	String desc = InputMap::get_singleton()->get_action_description(p_action_name);

	if (desc.is_empty()) {
		Ref<Shortcut> shortcut;
		if (p_action_name == "ui_find") {
			shortcut = get_find_shortcut();
		} else if (p_action_name == "ui_find_next") {
			shortcut = get_find_next_shortcut();
		} else if (p_action_name == "ui_find_previous") {
			shortcut = get_find_prev_shortcut();
		} else if (p_action_name == "ui_replace") {
			shortcut = get_replace_shortcut();
		}
		if (shortcut.is_valid()) {
			desc = shortcut->get_as_text();
		}
	}
	return desc;
}

// ED_SHORTCUT_AND_COMMAND("script_text_editor/find", TTRC("Find..."), KeyModifierMask::CMD_OR_CTRL | Key::F);

// ED_SHORTCUT("script_text_editor/find_next", TTRC("Find Next"), Key::F3);
// ED_SHORTCUT_OVERRIDE("script_text_editor/find_next", "macos", KeyModifierMask::META | Key::G);

// ED_SHORTCUT("script_text_editor/find_previous", TTRC("Find Previous"), KeyModifierMask::SHIFT | Key::F3);
// ED_SHORTCUT_OVERRIDE("script_text_editor/find_previous", "macos", KeyModifierMask::META | KeyModifierMask::SHIFT | Key::G);

// ED_SHORTCUT_AND_COMMAND("script_text_editor/replace", TTRC("Replace..."), KeyModifierMask::CTRL | Key::R);
// ED_SHORTCUT_OVERRIDE("script_text_editor/replace", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::F);

Ref<Shortcut> GDREFindReplaceBar::get_replace_shortcut() const {
	return get_or_create_shortcut("ui_replace", TTRC("Replace..."), KeyModifierMask::CMD_OR_CTRL | Key::R, KeyModifierMask::ALT | KeyModifierMask::META | Key::F);
}

Ref<Shortcut> GDREFindReplaceBar::get_find_shortcut() const {
	return get_or_create_shortcut("ui_find", TTRC("Find..."), KeyModifierMask::CMD_OR_CTRL | Key::F);
}

Ref<Shortcut> GDREFindReplaceBar::get_find_next_shortcut() const {
	return get_or_create_shortcut("ui_find_next", TTRC("Find Next"), Key::F3, KeyModifierMask::META | Key::G);
}

Ref<Shortcut> GDREFindReplaceBar::get_find_prev_shortcut() const {
	return get_or_create_shortcut("ui_find_previous", TTRC("Find Previous"), KeyModifierMask::SHIFT | Key::F3, KeyModifierMask::META | KeyModifierMask::SHIFT | Key::G);
}

void GDREFindReplaceBar::set_replace_enabled(bool p_enabled) {
	if (replace_enabled == p_enabled) {
		return;
	}
	replace_enabled = p_enabled;
}

void GDREFindReplaceBar::_update_replace_bar_enabled() {
	_update_toggle_replace_button(false);
	if (!replace_enabled) {
		replace_text->hide();
		hbc_button_replace->hide();
		hbc_option_replace->hide();
		if (search_text->is_visible_in_tree()) {
			_show_search(false, true);
		}
	}
}

bool GDREFindReplaceBar::is_replace_enabled() const {
	return replace_enabled;
}

void GDREFindReplaceBar::set_show_panel_background(bool p_show) {
	should_show_panel_background = p_show;
	_update_panel_background();
}

bool GDREFindReplaceBar::is_showing_panel_background() const {
	return should_show_panel_background;
}

void GDREFindReplaceBar::_update_panel_background() {
	if (should_show_panel_background) {
		remove_theme_style_override(SceneStringName(panel));
	} else {
		// empty stylebox
		add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("normal"), SNAME("Label")));
	}
}

void GDREFindReplaceBar::refresh_search() {
	preserve_cursor = true;
	_search_text_changed(get_search_text());
	preserve_cursor = false;
}

void GDREFindReplaceBar::_bind_methods() {
	ClassDB::bind_method(D_METHOD("search_next"), &GDREFindReplaceBar::search_next);
	ClassDB::bind_method(D_METHOD("search_prev"), &GDREFindReplaceBar::search_prev);

	ClassDB::bind_method(D_METHOD("search_current"), &GDREFindReplaceBar::search_current);

	ClassDB::bind_method(D_METHOD("get_search_text"), &GDREFindReplaceBar::get_search_text);
	ClassDB::bind_method(D_METHOD("get_replace_text"), &GDREFindReplaceBar::get_replace_text);

	ClassDB::bind_method(D_METHOD("is_case_sensitive"), &GDREFindReplaceBar::is_case_sensitive);
	ClassDB::bind_method(D_METHOD("is_whole_words"), &GDREFindReplaceBar::is_whole_words);
	ClassDB::bind_method(D_METHOD("is_selection_only"), &GDREFindReplaceBar::is_selection_only);

	ClassDB::bind_method(D_METHOD("set_text_edit", "p_text_editor"), &GDREFindReplaceBar::set_text_edit);

	ClassDB::bind_method(D_METHOD("get_find_shortcut"), &GDREFindReplaceBar::get_find_shortcut);
	ClassDB::bind_method(D_METHOD("get_replace_shortcut"), &GDREFindReplaceBar::get_replace_shortcut);
	ClassDB::bind_method(D_METHOD("get_find_next_shortcut"), &GDREFindReplaceBar::get_find_next_shortcut);
	ClassDB::bind_method(D_METHOD("get_find_prev_shortcut"), &GDREFindReplaceBar::get_find_prev_shortcut);

	ClassDB::bind_method(D_METHOD("popup_search", "p_show_only"), &GDREFindReplaceBar::popup_search, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("popup_replace"), &GDREFindReplaceBar::popup_replace);

	ClassDB::bind_method(D_METHOD("set_replace_enabled", "p_enabled"), &GDREFindReplaceBar::set_replace_enabled);
	ClassDB::bind_method(D_METHOD("is_replace_enabled"), &GDREFindReplaceBar::is_replace_enabled);

	ClassDB::bind_method(D_METHOD("set_show_panel_background", "p_show"), &GDREFindReplaceBar::set_show_panel_background);
	ClassDB::bind_method(D_METHOD("is_showing_panel_background"), &GDREFindReplaceBar::is_showing_panel_background);

	ClassDB::bind_method(D_METHOD("refresh_search"), &GDREFindReplaceBar::refresh_search);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_panel_background"), "set_show_panel_background", "is_showing_panel_background");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "replace_enabled"), "set_replace_enabled", "is_replace_enabled");
}

GDREFindReplaceBar::GDREFindReplaceBar() {
	main = memnew(HBoxContainer);
	main->set_h_size_flags(SIZE_EXPAND_FILL);
	main->set_v_size_flags(SIZE_EXPAND_FILL);
	// main->set_anchors_preset(Control::PRESET_FULL_RECT);
	add_child(main);

	toggle_replace_button = memnew(Button);
	main->add_child(toggle_replace_button);
	toggle_replace_button->set_accessibility_name(TTRC("Replace Mode"));
	toggle_replace_button->set_flat(true);
	toggle_replace_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	toggle_replace_button->connect(SceneStringName(pressed), callable_mp(this, &GDREFindReplaceBar::_toggle_replace_pressed));

	VBoxContainer *vbc_lineedit = memnew(VBoxContainer);
	main->add_child(vbc_lineedit);
	vbc_lineedit->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	vbc_lineedit->set_h_size_flags(SIZE_EXPAND_FILL);
	VBoxContainer *vbc_button = memnew(VBoxContainer);
	main->add_child(vbc_button);
	VBoxContainer *vbc_option = memnew(VBoxContainer);
	main->add_child(vbc_option);

	HBoxContainer *hbc_button_search = memnew(HBoxContainer);
	hbc_button_search->set_v_size_flags(SIZE_EXPAND_FILL);
	hbc_button_search->set_alignment(BoxContainer::ALIGNMENT_END);
	vbc_button->add_child(hbc_button_search);
	hbc_button_replace = memnew(HBoxContainer);
	hbc_button_replace->set_v_size_flags(SIZE_EXPAND_FILL);
	hbc_button_replace->set_alignment(BoxContainer::ALIGNMENT_END);
	vbc_button->add_child(hbc_button_replace);

	HBoxContainer *hbc_option_search = memnew(HBoxContainer);
	vbc_option->add_child(hbc_option_search);
	hbc_option_replace = memnew(HBoxContainer);
	vbc_option->add_child(hbc_option_replace);

	// Search toolbar.
	search_text = memnew(LineEdit);
	search_text->set_keep_editing_on_text_submit(true);
	vbc_lineedit->add_child(search_text);
	search_text->set_placeholder(TTRC("Find"));
	search_text->set_tooltip_text(TTRC("Find"));
	search_text->set_accessibility_name(TTRC("Find"));
	search_text->set_custom_minimum_size(Size2(100 * GDRESettings::get_singleton()->get_auto_display_scale(), 0));
	search_text->connect(SceneStringName(text_changed), callable_mp(this, &GDREFindReplaceBar::_search_text_changed));
	search_text->connect(SceneStringName(text_submitted), callable_mp(this, &GDREFindReplaceBar::_search_text_submitted));

	matches_label = memnew(Label);
	hbc_button_search->add_child(matches_label);
	matches_label->set_focus_mode(FOCUS_ACCESSIBILITY);
	matches_label->hide();

	find_prev = memnew(Button);
	find_prev->set_flat(true);
	find_prev->set_disabled(results_count < 1);
	find_prev->set_tooltip_text(vformat("%s (%s)", TTRC("Previous Match"), get_action_description("ui_find_previous")));
	hbc_button_search->add_child(find_prev);
	find_prev->set_focus_mode(FOCUS_ACCESSIBILITY);
	find_prev->connect(SceneStringName(pressed), callable_mp(this, &GDREFindReplaceBar::search_prev));

	find_next = memnew(Button);
	find_next->set_flat(true);
	find_next->set_disabled(results_count < 1);
	find_next->set_tooltip_text(vformat("%s (%s)", TTRC("Next Match"), get_action_description("ui_find_next")));
	hbc_button_search->add_child(find_next);
	find_next->set_focus_mode(FOCUS_ACCESSIBILITY);
	find_next->connect(SceneStringName(pressed), callable_mp(this, &GDREFindReplaceBar::search_next));

	case_sensitive = memnew(CheckBox);
	hbc_option_search->add_child(case_sensitive);
	case_sensitive->set_text(TTRC("Match Case"));
	case_sensitive->set_focus_mode(FOCUS_ACCESSIBILITY);
	case_sensitive->connect(SceneStringName(toggled), callable_mp(this, &GDREFindReplaceBar::_search_options_changed));

	whole_words = memnew(CheckBox);
	hbc_option_search->add_child(whole_words);
	whole_words->set_text(TTRC("Whole Words"));
	whole_words->set_focus_mode(FOCUS_ACCESSIBILITY);
	whole_words->connect(SceneStringName(toggled), callable_mp(this, &GDREFindReplaceBar::_search_options_changed));

	// Replace toolbar.
	replace_text = memnew(LineEdit);
	vbc_lineedit->add_child(replace_text);
	replace_text->set_placeholder(TTRC("Replace"));
	replace_text->set_tooltip_text(TTRC("Replace"));
	replace_text->set_accessibility_name(TTRC("Replace"));
	replace_text->set_custom_minimum_size(Size2(100 * GDRESettings::get_singleton()->get_auto_display_scale(), 0));
	replace_text->connect(SceneStringName(text_submitted), callable_mp(this, &GDREFindReplaceBar::_replace_text_submitted));

	replace = memnew(Button);
	hbc_button_replace->add_child(replace);
	replace->set_text(TTRC("Replace"));
	replace->connect(SceneStringName(pressed), callable_mp(this, &GDREFindReplaceBar::_replace));

	replace_all = memnew(Button);
	hbc_button_replace->add_child(replace_all);
	replace_all->set_text(TTRC("Replace All"));
	replace_all->connect(SceneStringName(pressed), callable_mp(this, &GDREFindReplaceBar::_replace_all));

	selection_only = memnew(CheckBox);
	hbc_option_replace->add_child(selection_only);
	selection_only->set_text(TTRC("Selection Only"));
	selection_only->set_focus_mode(FOCUS_ACCESSIBILITY);
	selection_only->connect(SceneStringName(toggled), callable_mp(this, &GDREFindReplaceBar::_search_options_changed));

	hide_button = memnew(Button);
	hide_button->set_flat(true);
	hide_button->set_tooltip_text(TTRC("Hide"));
	hide_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	hide_button->connect(SceneStringName(pressed), callable_mp(this, &GDREFindReplaceBar::_hide_bar));
	hide_button->set_v_size_flags(SIZE_SHRINK_CENTER);
	main->add_child(hide_button);

	_update_panel_background();
}
