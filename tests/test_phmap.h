#include "tests/test_macros.h"

#include "utility/gd_parallel_hashmap.h"

static constexpr int PHMAP_MAX_TEST = 50000;
struct test_thing {
	void do_test(int i, ParallelFlatHashMap<String, int> *thingy) {
		ParallelFlatHashMap<String, int> &phmap = *thingy;
		auto str = String::num_int64(i);
		bool phmap_has_it = thingy->contains(str);
		if (i % 2 == 0) {
			CHECK(phmap_has_it);
		} else {
			CHECK_FALSE(phmap_has_it);
			phmap[str] = i;
		}
	}
};

TEST_CASE("Test phmap") {
	ParallelFlatHashMap<String, int> thingy;
	CHECK(false);
	for (int i = 0; i < PHMAP_MAX_TEST; i += 2) {
		auto str = String::num_int64(i);
		thingy[str] = i;
	}
	for (int i = 0; i < PHMAP_MAX_TEST; i++) {
		auto str = String::num_int64(i);
		if (i % 2 == 0) {
			CHECK(thingy.contains(str));
		} else {
			CHECK_FALSE(thingy.contains(str));
		}
	}
	test_thing thing;
	auto group_id = WorkerThreadPool::get_singleton()->add_template_group_task(&thing, &test_thing::do_test, &thingy, PHMAP_MAX_TEST, 8, true);
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
	for (int i = 0; i < PHMAP_MAX_TEST; i++) {
		auto str = String::num_int64(i);
		CHECK(thingy.contains(str));
	}
}
