#pragma once
#include "gui/gdre_previewer.h"
#include "scene/gui/margin_container.h"

class AspectRatioContainer;
class ColorRect;
class TextureRect;
class ShaderMaterial;
class GDREColorChannelSelector;
class SpinBox;

class TexturePreviewer : public GDREPreviewer {
	GDCLASS(TexturePreviewer, GDREPreviewer);

private:
	struct ThemeCache {
		Color outline_color;
	} theme_cache;

	TextureRect *texture_display = nullptr;

	MarginContainer *main_margin_container = nullptr;
	MarginContainer *margin_container = nullptr;
	Control *outline_overlay = nullptr;
	AspectRatioContainer *centering_container = nullptr;
	ColorRect *bg_rect = nullptr;
	TextureRect *checkerboard = nullptr;
	Label *metadata_label = nullptr;
	float scale = 1.0;
	float position_x = 0.0;
	float position_y = 0.0;

	static inline Ref<ShaderMaterial> texture_material;

	GDREColorChannelSelector *channel_selector = nullptr;
	SpinBox *mipmap_spinbox = nullptr;

	void _draw_outline();
	void _update_metadata_label_text();
	void _update_position_and_scale();

protected:
	void _notification(int p_what);
	void _update_texture_display_ratio();
	void on_selected_channels_changed();
	void on_selected_mipmap_changed(double p_value);
	void gui_input(const Ref<InputEvent> &p_event) override;

public:
	static void init_shaders();
	static void finish_shaders();

	TextureRect *get_texture_display();
	virtual String get_previewer_name() const override;
	virtual Error edit(Ref<Resource> p_texture) override;
	virtual Error edit_from_path(const String &p_resource_path) override;
	virtual void reset() override;
	virtual String get_edited_resource_path() const override;
	virtual bool can_edit(const String &p_resource_path, const String &p_resource_type) const override;
	TexturePreviewer();
};
