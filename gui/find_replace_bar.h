#include "core/object/object.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/text_edit.h"

class GDREFindReplaceBar : public PanelContainer {
	GDCLASS(GDREFindReplaceBar, PanelContainer);

	enum SearchMode {
		SEARCH_CURRENT,
		SEARCH_NEXT,
		SEARCH_PREV,
	};

	HBoxContainer *main = nullptr;

	Button *toggle_replace_button = nullptr;
	LineEdit *search_text = nullptr;
	Label *matches_label = nullptr;
	Button *find_prev = nullptr;
	Button *find_next = nullptr;
	CheckBox *case_sensitive = nullptr;
	CheckBox *whole_words = nullptr;
	Button *hide_button = nullptr;

	LineEdit *replace_text = nullptr;
	Button *replace = nullptr;
	Button *replace_all = nullptr;
	CheckBox *selection_only = nullptr;

	HBoxContainer *hbc_button_replace = nullptr;
	HBoxContainer *hbc_option_replace = nullptr;

	CodeEdit *text_editor = nullptr;

	uint32_t flags = 0;

	int result_line = 0;
	int result_col = 0;
	int results_count = -1;
	int results_count_to_current = -1;

	bool replace_all_mode = false;
	bool preserve_cursor = false;

	bool replace_enabled = true;
	bool should_show_panel_background = true;

	virtual void input(const Ref<InputEvent> &p_event) override;

	void _get_search_from(int &r_line, int &r_col, SearchMode p_search_mode);
	void _update_results_count();
	void _update_matches_display();

	void _show_search(bool p_with_replace, bool p_show_only);
	void _hide_bar();
	void _update_toggle_replace_button(bool p_replace_visible);

	void _editor_text_changed();
	void _search_options_changed(bool p_pressed);
	void _search_text_changed(const String &p_text);
	void _search_text_submitted(const String &p_text);
	void _replace_text_submitted(const String &p_text);
	void _toggle_replace_pressed();

	void _update_replace_bar_enabled();

	void _update_panel_background();

	String get_action_description(const String &p_action_name) const;

protected:
	void _notification(int p_what);

	void _update_flags(bool p_direction_backwards);

	bool _search(uint32_t p_flags, int p_from_line, int p_from_col);

	void _replace();
	void _replace_all();

	static void _bind_methods();

public:
	String get_search_text() const;
	String get_replace_text() const;

	bool is_case_sensitive() const;
	bool is_whole_words() const;
	bool is_selection_only() const;

	void set_text_edit(CodeEdit *p_text_editor);

	void popup_search(bool p_show_only = false);
	void popup_replace();

	bool search_current();
	bool search_prev();
	bool search_next();

	bool needs_to_count_results = true;
	bool line_col_changed_for_result = false;

	Ref<Shortcut> get_find_shortcut() const;
	Ref<Shortcut> get_replace_shortcut() const;
	Ref<Shortcut> get_find_next_shortcut() const;
	Ref<Shortcut> get_find_prev_shortcut() const;

	void set_replace_enabled(bool p_enabled);
	bool is_replace_enabled() const;

	void set_show_panel_background(bool p_show);
	bool is_showing_panel_background() const;

	void refresh_search();

	GDREFindReplaceBar();
};
