#pragma once

#include "core/io/resource.h"
class SceneState;
class SceneStateInstanceGetter {
public:
	static constexpr int CURRENT_PACKED_SCENE_VERSION = 3;
	static Ref<Resource> get_fake_instance(SceneState *state, int p_idx);
};