#include "hybridcontainer.h"
#include "os.h"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

// baseLib::HybridContainer in both of its storages, the fixed array and the vector it switches to beyond its fixed
// size: contents, order, the switch, copies and moves, and that it lets go of the elements it no longer holds

namespace
{
	int g_checks = 0;
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		++g_checks;
		if(_ok)
			return;
		++g_failures;
		printf("FAILED %s\n", _what);
	}

	using Ints = baseLib::HybridContainer<int, 4>;

	template<typename C>
	std::vector<int> contents(const C& _c)
	{
		return std::vector<int>(_c.begin(), _c.end());
	}

	template<typename C>
	bool is(const C& _c, const std::vector<int>& _expected)
	{
		if(_c.size() != _expected.size() || _c.empty() != _expected.empty())
			return false;
		for(size_t i=0; i<_expected.size(); ++i)
		{
			if(_c[i] != _expected[i])
				return false;
		}
		return contents(_c) == _expected;
	}

	Ints make(const int _count)
	{
		Ints c;
		for(int i=0; i<_count; ++i)
			c.push_back(i + 1);
		return c;
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	// push_back into the array, then past it into the vector
	{
		Ints c;
		check(is(c, {}), "empty");
		check(c.begin() == c.end(), "empty: begin is end");
		for(int i=1; i<=4; ++i)
			c.push_back(i);
		check(is(c, {1, 2, 3, 4}), "push_back within the fixed size");
		c.push_back(5);
		check(is(c, {1, 2, 3, 4, 5}), "push_back past the fixed size");
		for(int i=6; i<=20; ++i)
			c.emplace_back(i);
		check(c.size() == 20 && c.front() == 1 && c.back() == 20 && c[19] == 20, "push_back to 20");
	}

	// front, back, pop_back in both storages
	{
		auto c = make(3);
		check(c.front() == 1 && c.back() == 3, "front and back, array");
		c.pop_back();
		check(is(c, {1, 2}), "pop_back, array");
		c.pop_back();
		c.pop_back();
		check(is(c, {}), "pop_back to empty, array");

		auto v = make(6);
		check(v.front() == 1 && v.back() == 6, "front and back, vector");
		v.pop_back();
		v.pop_back();
		v.pop_back();
		check(is(v, {1, 2, 3}), "pop_back below the fixed size, vector");
		v.push_back(9);
		check(is(v, {1, 2, 3, 9}), "push_back after pop_back, vector");

		bool threw = false;
		Ints e;
		try { e.front(); } catch(const std::out_of_range&) { threw = true; }
		check(threw, "front of an empty container throws");

		threw = false;
		auto emptied = make(5);
		for(int i=0; i<5; ++i)
			emptied.pop_back();
		try { emptied.back(); } catch(const std::out_of_range&) { threw = true; }
		check(threw, "back of a container emptied in its vector throws");
	}

	// clear, then use again in both storages
	{
		auto c = make(7);
		c.clear();
		check(is(c, {}), "clear");
		c.push_back(5);
		c.push_back(6);
		check(is(c, {5, 6}), "push_back after clear");
		for(int i=7; i<=12; ++i)
			c.push_back(i);
		check(is(c, {5, 6, 7, 8, 9, 10, 11, 12}), "past the fixed size after clear");
	}

	// copies and moves, the copies independent
	for(const int count : {3, 7})
	{
		const auto source = make(count);
		const auto expected = contents(source);

		Ints copy(source);
		check(is(copy, expected), "copy construction");
		copy.push_back(99);
		check(is(source, expected), "a copy is independent");

		Ints assigned = make(count == 3 ? 7 : 2);
		assigned = source;
		check(is(assigned, expected), "copy assignment");

		Ints movedFrom(source);
		Ints moved(std::move(movedFrom));
		check(is(moved, expected), "move construction");
		check(movedFrom.empty(), "move construction empties the source");

		Ints movedFrom2(source);
		Ints moveAssigned = make(count == 3 ? 6 : 1);
		moveAssigned = std::move(movedFrom2);
		check(is(moveAssigned, expected), "move assignment");
		check(movedFrom2.empty(), "move assignment empties the source");

		baseLib::HybridContainer<int, 8> other;
		other = source;
		check(is(other, expected), "copy assignment from another fixed size");

		Ints movedFrom3(source);
		baseLib::HybridContainer<int, 2> small;
		small = std::move(movedFrom3);
		check(is(small, expected), "move assignment into a smaller fixed size");
		check(movedFrom3.empty(), "move assignment from another fixed size empties the source");
	}

	// resize in both storages and across them
	{
		Ints c = make(2);
		c.resize(4);
		check(is(c, {1, 2, 0, 0}), "resize up within the array");
		c.resize(6);
		check(is(c, {1, 2, 0, 0, 0, 0}), "resize up past the fixed size");
		c.resize(3);
		check(is(c, {1, 2, 0}), "resize down below the fixed size");
		c[2] = 3;
		c.resize(4);
		check(is(c, {1, 2, 3, 0}), "resize up again below the fixed size");
		c.resize(0);
		check(is(c, {}), "resize to zero");
	}

	// insert and erase in both storages
	{
		Ints c = make(3);
		c.insert(c.begin(), 0);
		check(is(c, {0, 1, 2, 3}), "insert at the front, array");
		c.insert(c.begin() + 2, 9);
		check(is(c, {0, 1, 9, 2, 3}), "insert into a full array");
		c.insert(c.end(), 4);
		check(is(c, {0, 1, 9, 2, 3, 4}), "insert at the end, vector");
		c.erase(c.begin() + 2);
		check(is(c, {0, 1, 2, 3, 4}), "erase one, vector");
		c.erase(c.begin(), c.begin() + 2);
		check(is(c, {2, 3, 4}), "erase a range, vector");

		Ints a = make(4);
		a.erase(a.begin() + 1, a.begin() + 3);
		check(is(a, {1, 4}), "erase a range, array");
		const int more[] = {7, 8};
		a.insert(a.begin() + 1, std::begin(more), std::end(more));
		check(is(a, {1, 7, 8, 4}), "insert a range, array");
		const int more2[] = {5, 6};
		a.insert(a.begin(), std::begin(more2), std::end(more2));
		check(is(a, {5, 6, 1, 7, 8, 4}), "insert a range past the fixed size");
	}

	// assign, append, swap, reserve, data
	{
		Ints c;
		const int five[] = {1, 2, 3, 4, 5};
		c.assign(five, 3);
		check(is(c, {1, 2, 3}), "assign from a pointer, array");
		c.assign(five, 5);
		check(is(c, {1, 2, 3, 4, 5}), "assign from a pointer past the fixed size");
		c.assign(std::vector<int>{7, 8});
		check(is(c, {7, 8}), "assign a vector");
		c.assign(std::array<int, 3>{4, 5, 6});
		check(is(c, {4, 5, 6}), "assign an array");
		c.assign({9});
		check(is(c, {9}), "assign an initializer list");

		Ints d = make(2);
		d.append(std::vector<int>{3});
		check(is(d, {1, 2, 3}), "append within the fixed size");
		d.append(std::vector<int>{4, 5});
		check(is(d, {1, 2, 3, 4, 5}), "append past the fixed size");

		Ints x = make(2);
		Ints y = make(6);
		x.swap(y);
		check(is(x, {1, 2, 3, 4, 5, 6}) && is(y, {1, 2}), "swap");

		Ints z = make(3);
		std::vector<int> w{7, 8, 9, 10, 11};
		z.swap(w);
		check(is(z, {7, 8, 9, 10, 11}) && w == std::vector<int>{1, 2, 3}, "swap with a vector");

		Ints r = make(4);
		r.reserve(4, true);
		check(is(r, {1, 2, 3, 4}), "reserve the fixed size");
		r.reserve(16, true);
		check(is(r, {1, 2, 3, 4}), "reserve past the fixed size");
		check(r.data() == &r[0], "data");

		const Ints init{1, 2, 3, 4, 5};
		check(is(init, {1, 2, 3, 4, 5}), "initializer list past the fixed size");
		const Ints fromVector(std::vector<int>{1, 2});
		check(is(fromVector, {1, 2}), "from a vector");
	}

	// the container lets go of what it no longer holds, the way the event holds its listeners
	{
		auto p = std::make_shared<int>(1);
		{
			baseLib::HybridContainer<std::shared_ptr<int>, 4> c;
			for(int i=0; i<10; ++i)
				c.push_back(p);
			check(p.use_count() == 11, "ten references held");
		}
		check(p.use_count() == 1, "all released with the container");

		baseLib::HybridContainer<std::shared_ptr<int>, 4> c;
		c.push_back(p);
		c.push_back(p);
		c.pop_back();
		check(p.use_count() == 2, "pop_back releases, array");
		c.clear();
		check(p.use_count() == 1, "clear releases, array");

		for(int i=0; i<6; ++i)
			c.push_back(p);
		c.clear();
		check(p.use_count() == 1, "clear releases, vector");

		for(int i=0; i<4; ++i)
			c.push_back(p);
		c.erase(c.begin(), c.begin() + 2);
		check(p.use_count() == 3, "erase releases, array");
		c.resize(1);
		check(p.use_count() == 2, "resize down releases");
		const auto q = std::make_shared<int>(2);
		c.assign(&q, 1);
		check(p.use_count() == 1 && q.use_count() == 2, "assign releases");
	}

	printf("%d of %d checks passed\n", g_checks - g_failures, g_checks);
	return g_failures ? 1 : 0;
}
