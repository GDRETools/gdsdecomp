#pragma once

#include "core/io/resource.h"
#include "scene/gui/control.h"

#include "utility/resource_info.h"

class GDREPreviewer : public Control {
	GDCLASS(GDREPreviewer, Control);

protected:
	static void _bind_methods();

public:
	virtual String get_previewer_name() const;
	virtual Error edit(Ref<Resource> p_scene);
	virtual Error edit_from_path(const String &p_resource_path);
	virtual bool can_edit(const String &p_resource_path, const String &p_resource_type) const;
	virtual String get_edited_resource_path() const;
	virtual void reset();
	virtual ResourceInfo::LoadType get_load_type() const;
	virtual bool can_switch_to_text() const;

	GDVIRTUAL0RC(String, _get_previewer_name);
	GDVIRTUAL1R(Error, _edit, Ref<Resource>);
	GDVIRTUAL1R(Error, _edit_from_path, String);
	GDVIRTUAL2RC(bool, _can_edit, String, String);
	GDVIRTUAL0RC(String, _get_edited_resource_path);
	GDVIRTUAL0(_reset);
	GDVIRTUAL0RC(ResourceInfo::LoadType, _get_load_type);
	GDVIRTUAL0RC(bool, _can_switch_to_text);
};
