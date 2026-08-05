#pragma once

#include "compat/resource_loader_compat.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "modules/visual_shader/visual_shader.h"

class VisualShaderCompat : public VisualShader {
	GDSOFTCLASS(VisualShaderCompat, VisualShader);

protected:
	virtual void _update_shader() const override;
	virtual void reset_state() override;

public:
	//get_mode()
	virtual Mode get_mode() const override;
	virtual bool is_text_shader() const override;
};

class VisualShaderConverterCompat : public ResourceCompatConverter {
	GDCLASS(VisualShaderConverterCompat, ResourceCompatConverter);

public:
	virtual Ref<Resource> convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error = nullptr) override;
	virtual bool handles_type(const String &p_type, int ver_major) const override;
};

// This is used for scene exports;
// For visual shaders that have VisualShaderNodeCustom nodes, we need to ensure the script is a real compiled script,
// or the shader will fail to compile.
// NOTE: This is not safe, since it executes arbitrary code.
class VisualShaderNodeCustomConverterCompat : public ResourceCompatConverter {
	GDCLASS(VisualShaderNodeCustomConverterCompat, ResourceCompatConverter);

public:
	virtual Ref<Resource> convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error = nullptr) override;
	virtual bool handles_type(const String &p_type, int ver_major) const override;
};
