#include "fake_scene_state.h"
#include "core/io/resource.h"
#include "core/object/object.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"
#include "utility/common.h"

class fake_scene_state : public RefCounted {
	GDCLASS(fake_scene_state, RefCounted);

	Vector<StringName> names;
	Vector<Variant> variants;
	Vector<NodePath> node_paths;
	Vector<NodePath> editable_instances;
	mutable HashMap<NodePath, int> node_path_cache;
	mutable HashMap<int, int> base_scene_node_remap;

	int base_scene_idx = -1;

	enum {
		NO_PARENT_SAVED = 0x7FFFFFFF,
		NAME_INDEX_BITS = 18,
		NAME_MASK = (1 << NAME_INDEX_BITS) - 1,
	};

	struct NodeData {
		int parent = 0;
		int owner = 0;
		int type = 0;
		int name = 0;
		int instance = 0;
		int index = 0;

		struct Property {
			int name = 0;
			int value = 0;
		};

		Vector<Property> properties;
		Vector<int> groups;
	};

	struct DeferredNodePathProperties {
		Node *base = nullptr;
		StringName property;
		Variant value;
	};

	Vector<NodeData> nodes;

	struct ConnectionData {
		int from = 0;
		int to = 0;
		int signal = 0;
		int method = 0;
		int flags = 0;
		int unbinds = 0;
		Vector<int> binds;
	};
	Vector<ConnectionData> connections;
	String path;
	uint64_t last_modified_time = 0;

public:
	Ref<Resource> get_resource_if_packed_scene(int p_idx) {
		return variants[p_idx];
	}
	Ref<Resource> get_fake_instance(int p_idx) {
		ERR_FAIL_INDEX_V(p_idx, nodes.size(), Ref<PackedScene>());

		if (nodes[p_idx].instance >= 0) {
			if (nodes[p_idx].instance & SceneState::FLAG_INSTANCE_IS_PLACEHOLDER) {
				return Ref<PackedScene>();
			} else {
				return variants[nodes[p_idx].instance & SceneState::FLAG_MASK];
			}
		} else if (nodes[p_idx].parent < 0 || nodes[p_idx].parent == NO_PARENT_SAVED) {
			if (base_scene_idx >= 0) {
				return variants[base_scene_idx];
			}
		}

		return Ref<PackedScene>();
	}
};
CHECK_SIZE_MATCH_NO_PADDING(fake_scene_state, SceneState);

// Ref<Resource> get_fake_instance(SceneState *state, int p_idx);

Ref<Resource> SceneStateInstanceGetter::get_fake_instance(SceneState *state, int p_idx) {
	return reinterpret_cast<fake_scene_state *>(state)->get_fake_instance(p_idx);
}
