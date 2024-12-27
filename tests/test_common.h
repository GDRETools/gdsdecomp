#pragma once
#include "core/io/marshalls.h"
#include <utility/common.h>
#include <utility/gdre_settings.h>

_ALWAYS_INLINE_ String get_gdsdecomp_path() {
	return GDRESettings::get_singleton()->get_cwd().path_join("modules/gdsdecomp");
}

_ALWAYS_INLINE_ String get_tmp_path() {
	return get_gdsdecomp_path().path_join(".tmp");
}
