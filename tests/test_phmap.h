#pragma once
#include "tests/test_macros.h"

#include "utility/gd_parallel_hashmap.h"

static constexpr int SIMPLE_TEST_MAX_ITERS = 50000;
static constexpr int SIMPLE_TEST_DIVISOR = 4;
static constexpr int COMPLEX_TEST_MAX_ITERS = 5000;
static constexpr int COMPLEX_TEST_DIVISOR = 4;

struct simple_phmap_test {
	template <bool multiple_writers_for_same_value = false>
	void do_test(int i, ParallelFlatHashMap<String, int> *thingy) {
		int value = multiple_writers_for_same_value ? (i / SIMPLE_TEST_DIVISOR) : i;
		if (value >= SIMPLE_TEST_MAX_ITERS) {
			return;
		}
		ParallelFlatHashMap<String, int> &phmap = *thingy;
		auto key = String::num_int64(value);
		bool phmap_has_it = phmap.contains(key);
		if (value % 2 == 0) {
			CHECK(phmap_has_it);
			CHECK(phmap[key] == value);
		} else {
			if (!multiple_writers_for_same_value) {
				CHECK_FALSE(phmap_has_it);
			}
			phmap.if_contains(key, [&](auto &v) {
				CHECK(v.first == key);
				CHECK(v.second == value);
			});
			phmap.try_emplace_l(key, [&](auto &v) {
				CHECK(v.first == key);
				CHECK(v.second == value);
				v.second = value; }, value);
		}
	}
};

template <class K, class V, int max_iters = 5000, int divsor = 4>
struct test_thing {
	Vector<K> keys;
	Vector<V> values;
	bool multiple_writers_for_same_value = false;
	void do_test(int _i, ParallelFlatHashMap<K, V> *thingy) {
		int idx = multiple_writers_for_same_value ? (_i / SIMPLE_TEST_DIVISOR) : _i;
		if (idx >= SIMPLE_TEST_MAX_ITERS) {
			return;
		}
		auto &key = keys[idx];
		auto &value = values[idx];
		ParallelFlatHashMap<K, V> &phmap = *thingy;
		bool phmap_has_it = phmap.contains(key);
		if (!multiple_writers_for_same_value) {
			CHECK_FALSE(phmap_has_it);
		}
		phmap.if_contains(key, [&](auto &v) {
			CHECK(v.first == key);
			CHECK(v.second == value);
		});
		phmap.try_emplace_l(key, [&](auto &v) {
			CHECK(v.first == key);
			CHECK(v.second == value);
			v.second = value; }, value);
	}
};

void inline init_simple_phmap(ParallelFlatHashMap<String, int> &thingy) {
	for (int i = 0; i < SIMPLE_TEST_MAX_ITERS; i += 2) {
		auto str = String::num_int64(i);
		thingy[str] = i;
	}
	for (int i = 0; i < SIMPLE_TEST_MAX_ITERS; i++) {
		auto str = String::num_int64(i);
		if (i % 2 == 0) {
			CHECK(thingy.contains(str));
		} else {
			CHECK_FALSE(thingy.contains(str));
		}
	}
}

void inline check_phmap(ParallelFlatHashMap<String, int> &thingy) {
	for (int i = 0; i < SIMPLE_TEST_MAX_ITERS; i++) {
		auto str = String::num_int64(i);
		CHECK(thingy.contains(str));
		CHECK(thingy[str] == i);
	}
}

TEST_CASE("[GDSDecomp] Test phmap") {
	ParallelFlatHashMap<String, int> non_multi_writer;
	init_simple_phmap(non_multi_writer);
	simple_phmap_test thing;
	auto group_id = WorkerThreadPool::get_singleton()->add_template_group_task(&thing, &simple_phmap_test::do_test<false>, &non_multi_writer, SIMPLE_TEST_MAX_ITERS, 8, true);
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
	check_phmap(non_multi_writer);
}

TEST_CASE("[GDSDecomp] Test phmap with multiple writers for same value") {
	ParallelFlatHashMap<String, int> multi_writer;
	init_simple_phmap(multi_writer);
	simple_phmap_test thing;
	auto group_id = WorkerThreadPool::get_singleton()->add_template_group_task(&thing, &simple_phmap_test::do_test<true>, &multi_writer, SIMPLE_TEST_MAX_ITERS * SIMPLE_TEST_DIVISOR, 8, true);
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
	check_phmap(multi_writer);
}

String generate_random_string() {
	static constexpr int max_len = 20;
	static constexpr char alphanum[] = "0123456789"
									   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
									   "abcdefghijklmnopqrstuvwxyz";
	String string;
	for (int i = 0; i < max_len; i++) {
		string += alphanum[rand() % (sizeof(alphanum) - 1)];
	}
	return string;
}

template <class K, class V>
void run_complex_test(test_thing<K, V> tester) {
	tester.multiple_writers_for_same_value = false;
	ParallelFlatHashMap<K, V> map;
	auto group_id = WorkerThreadPool::get_singleton()->add_template_group_task(&tester, &test_thing<K, V>::do_test, &map, COMPLEX_TEST_MAX_ITERS, -1, true);
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
	tester.multiple_writers_for_same_value = true;
	group_id = WorkerThreadPool::get_singleton()->add_template_group_task(&tester, &test_thing<K, V>::do_test, &map, COMPLEX_TEST_MAX_ITERS * COMPLEX_TEST_DIVISOR, -1, true);
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_id);
}

TEST_CASE("[GDSDecomp] Test phmap with complex values") {
	test_thing<String, String> thing;
	thing.keys.resize(COMPLEX_TEST_MAX_ITERS);
	thing.values.resize(COMPLEX_TEST_MAX_ITERS);
	for (int i = 0; i < COMPLEX_TEST_MAX_ITERS; i++) {
		thing.keys.write[i] = generate_random_string();
		thing.values.write[i] = generate_random_string();
	}
	run_complex_test(thing);
}
