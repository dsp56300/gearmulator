#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace juceRmlUi { class RmlComponent; }

namespace pluginManager
{
	// Asks, and calls back only on yes.
	//
	// _onConfirmed runs on the message thread with RmlUi access taken, and not at all when the answer
	// is no or the page it belongs to is gone by then. The box answers outside of an RmlUi handler,
	// which is the whole reason taking that access is not left to each call site.
	void confirm(juceRmlUi::RmlComponent& _rmlComponent, const std::string& _title,
	             const std::string& _question, const std::shared_ptr<bool>& _alive,
	             std::function<void()> _onConfirmed);

	// Removing several formats at once is the one thing on either page that throws away more than the
	// user pointed at, so both ask first - in the same words, from here.
	void confirmUninstall(juceRmlUi::RmlComponent& _rmlComponent, const std::string& _name,
	                      const std::vector<std::string>& _formats, const std::shared_ptr<bool>& _alive,
	                      std::function<void()> _onConfirmed);
}
