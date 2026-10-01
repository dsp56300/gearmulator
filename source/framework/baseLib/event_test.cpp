#include "event.h"
#include "os.h"

#include <cstdio>
#include <memory>
#include <string>

// baseLib::Event: the listeners a listener changes while it is called. One it removes before its turn is not called,
// one it adds waits for the next invoke, one that removes itself finishes its call, and an event destroyed by its own
// listener lets the invoke finish as before

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

	using IntEvent = baseLib::Event<int>;
}

int main()
{
	baseLib::disableErrorDialogs();

	// in the order they were added, with the arguments
	{
		IntEvent e;
		std::string order;
		e.addListener([&](const int _v) { order += "a" + std::to_string(_v); });
		e.addListener([&](const int _v) { order += "b" + std::to_string(_v); });
		e(7);
		check(order == "a7b7", "listeners called in order with the arguments");
	}

	// a listener that removes itself is not called again, the others are
	{
		IntEvent e;
		int a = 0, b = 0;
		IntEvent::ListenerId idA = IntEvent::InvalidListenerId;
		idA = e.addListener([&](int) { ++a; e.removeListener(idA); });
		e.addListener([&](int) { ++b; });
		e(0);
		e(0);
		check(a == 1 && b == 2, "a listener removes itself");
	}

	// a listener removes one that comes later: that one is not called any more, it may be gone with its object
	{
		IntEvent e;
		auto alive = std::make_shared<bool>(true);
		int calledAfterItsEnd = 0;
		IntEvent::ListenerId idB = IntEvent::InvalidListenerId;
		e.addListener([&](int) { e.removeListener(idB); *alive = false; });
		idB = e.addListener([&, alive](int) { if(!*alive) ++calledAfterItsEnd; });
		e(0);
		check(calledAfterItsEnd == 0, "a listener removed by an earlier one is not called");
	}

	// the same through an EventListener whose owner an earlier listener deletes
	{
		IntEvent e;
		struct Owner
		{
			Owner(IntEvent& _e, int& _calls) : listener(_e, [&_calls](int) { ++_calls; }) {}
			baseLib::EventListener<int> listener;
		};
		int calls = 0;
		Owner* owner = nullptr;
		e.addListener([&](int) { delete owner; owner = nullptr; });
		owner = new Owner(e, calls);
		e(0);
		check(owner == nullptr && calls == 0, "a listener whose owner an earlier one deletes is not called");
	}

	// a listener added while the event is invoked waits for the next invoke
	{
		IntEvent e;
		int added = 0;
		bool once = true;
		e.addListener([&](int)
		{
			if(!once)
				return;
			once = false;
			e.addListener([&](int) { ++added; });
		});
		e(0);
		check(added == 0, "a listener added during an invoke is not called by it");
		e(0);
		check(added == 1, "a listener added during an invoke is called by the next");
	}

	// clear while invoked: no later listener is called
	{
		IntEvent e;
		int later = 0;
		e.addListener([&](int) { e.clear(); });
		e.addListener([&](int) { ++later; });
		e(0);
		e(0);
		check(later == 0, "clear while invoked");
	}

	// a listener destroys the event: the invoke finishes with the listeners it had, as before
	{
		auto* e = new IntEvent();
		int after = 0;
		e->addListener([&](int) { delete e; e = nullptr; });
		e->addListener([&](int) { ++after; });
		e->invoke(0);
		check(e == nullptr && after == 1, "an event destroyed by its listener");
	}

	// invoked again by its own listener
	{
		IntEvent e;
		int depth = 0, inner = 0, last = 0;
		IntEvent::ListenerId idLast = IntEvent::InvalidListenerId;
		e.addListener([&](const int _v)
		{
			if(_v == 0)
			{
				++depth;
				e(1);
				--depth;
			}
		});
		e.addListener([&](const int _v)
		{
			if(_v == 1)
			{
				++inner;
				e.removeListener(idLast);
			}
		});
		idLast = e.addListener([&](int) { ++last; });
		e(0);
		check(inner == 1 && last == 0 && depth == 0, "removed in a nested invoke, not called by the outer one");
	}

	// more listeners than the invoke holds without allocating
	{
		IntEvent e;
		int calls = 0;
		IntEvent::ListenerId ids[20];
		for(auto& id : ids)
			id = e.addListener([&](int) { ++calls; });
		e.addListener([&](int) { e.removeListener(ids[19]); });
		e(0);
		check(calls == 20, "twenty listeners called");
		calls = 0;
		IntEvent::ListenerId idLast = IntEvent::InvalidListenerId;
		e.addListener([&](int) { e.removeListener(idLast); });
		idLast = e.addListener([&](int) { calls += 100; });
		e(0);
		check(calls == 19, "the twenty-first and later: one removed by an earlier one is not called");
	}

	// a retained value reaches a listener added later at once
	{
		IntEvent e;
		e.retain(5);
		int got = 0;
		e.addListener([&](const int _v) { got = _v; });
		check(got == 5, "retained value");
		e.clearRetained();
		got = 0;
		e.addListener([&](const int _v) { got = _v; });
		check(got == 0, "cleared retained value");
	}

	// an explicit id that is in use keeps its listener, as before
	{
		IntEvent e;
		int first = 0, second = 0;
		e.addListener(3, [&](int) { ++first; });
		e.addListener(3, [&](int) { ++second; });
		e(0);
		check(first == 1 && second == 0, "an explicit id in use");
		check(e.getListener(3).has_value() && !e.getListener(4).has_value(), "getListener");
	}

	// a copy has listeners of its own
	{
		IntEvent e;
		int calls = 0;
		const auto id = e.addListener([&](int) { ++calls; });
		IntEvent copy(e);
		copy.removeListener(id);
		e(0);
		copy(0);
		check(calls == 1, "removed from a copy, still in the original");
		IntEvent assigned;
		assigned = e;
		assigned(0);
		check(calls == 2, "copy assignment");
		IntEvent moved(std::move(assigned));
		moved(0);
		check(calls == 3, "move construction");
	}

	// EventListener: removed with it, moved, moved to another event
	{
		IntEvent e, other;
		int calls = 0;
		{
			baseLib::EventListener<int> l(e, [&](int) { ++calls; });
			e(0);
			baseLib::EventListener<int> moved(std::move(l));
			e(0);
			moved.set(other);
			e(0);
			other(0);
		}
		e(0);
		other(0);
		check(calls == 3, "EventListener");
	}

	printf("%d of %d checks passed\n", g_checks - g_failures, g_checks);
	return g_failures ? 1 : 0;
}
