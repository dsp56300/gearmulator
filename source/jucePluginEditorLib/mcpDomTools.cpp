#ifdef GEARMULATOR_BUILD_MCP_SERVER

#include "mcpDomTools.h"

#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "pluginProcessor.h"

#include "mcpServerLib/mcpServer.h"
#include "mcpServerLib/mcpTool.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <juce_events/juce_events.h>
#include <future>

namespace jucePluginEditorLib
{
	namespace
	{
		// Helper to serialize an element to JSON
		mcpServer::JsonValue serializeElement(Rml::Element* _elem, const int _maxDepth, const int _currentDepth = 0)
		{
			if (!_elem)
				return mcpServer::JsonValue::null();

			auto obj = mcpServer::JsonValue::object();
			obj.set("tag", mcpServer::JsonValue::fromString(_elem->GetTagName()));

			const auto& id = _elem->GetId();
			if (!id.empty())
				obj.set("id", mcpServer::JsonValue::fromString(id));

			// Collect attributes
			auto attrs = mcpServer::JsonValue::object();
			bool hasAttrs = false;

			for (const auto& [name, variant] : _elem->GetAttributes())
			{
				attrs.set(name, mcpServer::JsonValue::fromString(
					variant.Get<Rml::String>(_elem->GetCoreInstance())));
				hasAttrs = true;
			}

			if (hasAttrs)
				obj.set("attributes", attrs);

			// Class names
			const auto classNames = _elem->GetClassNames();
			if (!classNames.empty())
				obj.set("class", mcpServer::JsonValue::fromString(classNames));

			// Children (if within depth limit)
			const int numChildren = _elem->GetNumChildren();
			if (numChildren > 0 && _currentDepth < _maxDepth)
			{
				auto children = mcpServer::JsonValue::array();
				for (int i = 0; i < numChildren; ++i)
				{
					auto* child = _elem->GetChild(i);
					if (child && child->GetTagName() != "#text")
						children.append(serializeElement(child, _maxDepth, _currentDepth + 1));
				}
				obj.set("children", children);
			}
			else if (numChildren > 0)
			{
				obj.set("childCount", mcpServer::JsonValue::fromInt(numChildren));
			}

			return obj;
		}

		// Helper to run a function on the message thread and get the result
		template<typename F>
		auto runOnMessageThread(F&& _func) -> decltype(_func())
		{
			using ReturnType = decltype(_func());

			if (juce::MessageManager::getInstance()->isThisTheMessageThread())
				return _func();

			std::promise<ReturnType> promise;
			auto future = promise.get_future();

			juce::MessageManager::callAsync([&promise, func = std::forward<F>(_func)]
			{
				try
				{
					if constexpr (std::is_void_v<ReturnType>)
					{
						func();
						promise.set_value();
					}
					else
					{
						promise.set_value(func());
					}
				}
				catch (...)
				{
					promise.set_exception(std::current_exception());
				}
			});

			return future.get();
		}

		Editor* getEditor(Processor& _processor)
		{
			auto* editorState = _processor.getEditorState();
			if (!editorState)
				return nullptr;
			return editorState->getEditor();
		}
	}

	void registerDomTools(mcpServer::McpServer& _server, Processor& _processor)
	{
		// get_dom_tree
		{
			mcpServer::ToolDef tool;
			tool.name = "get_dom_tree";
			tool.description = "Get the RmlUI document DOM tree as JSON. Requires the plugin editor window to be open.";
			tool.inputSchema.addIntProperty("maxDepth", "Maximum tree depth to traverse (default: 5)", false, 1, 50);
			tool.inputSchema.addProperty("rootId", "string", "Optional element ID to use as root (default: document root)", false);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int maxDepth = _params.isObject() && _params.hasProperty("maxDepth")
					? _params.get("maxDepth").getInt() : 5;
				const auto rootId = _params.isObject() && _params.hasProperty("rootId")
					? _params.get("rootId").getString().toStdString() : std::string();

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto* editor = getEditor(_processor);
					if (!editor)
						throw std::runtime_error("Plugin editor is not open. Open the plugin window first.");

					auto* rmlComp = editor->getRmlComponent();
					if (!rmlComp)
						throw std::runtime_error("RmlUI component not available");

					juceRmlUi::RmlInterfaces::ScopedAccess access(*rmlComp);

					Rml::Element* root = editor->getDocument();
					if (!root)
						throw std::runtime_error("No RmlUI document loaded");

					if (!rootId.empty())
					{
						root = root->GetElementById(rootId);
						if (!root)
							throw std::runtime_error("Element not found: " + rootId);
					}

					return serializeElement(root, maxDepth);
				});
			};
			_server.registerTool(std::move(tool));
		}

		// get_element
		{
			mcpServer::ToolDef tool;
			tool.name = "get_element";
			tool.description = "Get detailed information about a specific element by ID";
			tool.inputSchema.addProperty("id", "string", "Element ID to look up", true);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto id = _params.get("id").getString().toStdString();

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto* editor = getEditor(_processor);
					if (!editor)
						throw std::runtime_error("Plugin editor is not open");

					auto* rmlComp = editor->getRmlComponent();
					if (!rmlComp)
						throw std::runtime_error("RmlUI component not available");

					juceRmlUi::RmlInterfaces::ScopedAccess access(*rmlComp);

					auto* doc = editor->getDocument();
					if (!doc)
						throw std::runtime_error("No RmlUI document loaded");

					auto* elem = doc->GetElementById(id);
					if (!elem)
						throw std::runtime_error("Element not found: " + id);

					auto result = mcpServer::JsonValue::object();
					result.set("tag", mcpServer::JsonValue::fromString(elem->GetTagName()));
					result.set("id", mcpServer::JsonValue::fromString(elem->GetId()));
					result.set("class", mcpServer::JsonValue::fromString(elem->GetClassNames()));
					result.set("childCount", mcpServer::JsonValue::fromInt(elem->GetNumChildren()));

					// Attributes
					auto attrs = mcpServer::JsonValue::object();
					for (const auto& [name, variant] : elem->GetAttributes())
						attrs.set(name, mcpServer::JsonValue::fromString(
							variant.Get<Rml::String>(elem->GetCoreInstance())));
					result.set("attributes", attrs);

					// Box model
					const auto box = elem->GetBox();
					auto boxObj = mcpServer::JsonValue::object();
					const auto& offset = elem->GetAbsoluteOffset(Rml::BoxArea::Border);
					boxObj.set("x", mcpServer::JsonValue::fromDouble(offset.x));
					boxObj.set("y", mcpServer::JsonValue::fromDouble(offset.y));
					boxObj.set("width", mcpServer::JsonValue::fromDouble(box.GetSize(Rml::BoxArea::Border).x));
					boxObj.set("height", mcpServer::JsonValue::fromDouble(box.GetSize(Rml::BoxArea::Border).y));
					result.set("box", boxObj);

					// Visibility
					result.set("visible", mcpServer::JsonValue::fromBool(elem->IsVisible()));

					// Inner text (truncated)
					const auto innerRml = elem->GetInnerRML();
					if (innerRml.size() <= 200)
						result.set("innerRml", mcpServer::JsonValue::fromString(innerRml));
					else
						result.set("innerRml", mcpServer::JsonValue::fromString(innerRml.substr(0, 200) + "..."));

					// Children summary
					auto childTags = mcpServer::JsonValue::array();
					for (int i = 0; i < elem->GetNumChildren() && i < 50; ++i)
					{
						auto* child = elem->GetChild(i);
						if (child && child->GetTagName() != "#text")
						{
							auto c = mcpServer::JsonValue::object();
							c.set("tag", mcpServer::JsonValue::fromString(child->GetTagName()));
							const auto& childId = child->GetId();
							if (!childId.empty())
								c.set("id", mcpServer::JsonValue::fromString(childId));
							childTags.append(c);
						}
					}
					result.set("children", childTags);

					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// find_elements
		{
			mcpServer::ToolDef tool;
			tool.name = "find_elements";
			tool.description = "Find elements by tag name, returning their IDs and basic info";
			tool.inputSchema.addProperty("tag", "string", "Tag name to search for (e.g. 'button', 'input', 'select')", true);
			tool.inputSchema.addIntProperty("limit", "Maximum number of results (default: 50)", false, 1, 500);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto tag = _params.get("tag").getString().toStdString();
				const int limit = _params.hasProperty("limit") ? _params.get("limit").getInt() : 50;

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto* editor = getEditor(_processor);
					if (!editor)
						throw std::runtime_error("Plugin editor is not open");

					auto* rmlComp = editor->getRmlComponent();
					if (!rmlComp)
						throw std::runtime_error("RmlUI component not available");

					juceRmlUi::RmlInterfaces::ScopedAccess access(*rmlComp);

					auto* doc = editor->getDocument();
					if (!doc)
						throw std::runtime_error("No RmlUI document loaded");

					// BFS to find matching elements
					auto results = mcpServer::JsonValue::array();
					int count = 0;

					std::vector<Rml::Element*> queue;
					queue.push_back(doc);

					while (!queue.empty() && count < limit)
					{
						auto* current = queue.front();
						queue.erase(queue.begin());

						if (current->GetTagName() == tag)
						{
							auto obj = mcpServer::JsonValue::object();
							obj.set("tag", mcpServer::JsonValue::fromString(current->GetTagName()));
							const auto& id = current->GetId();
							if (!id.empty())
								obj.set("id", mcpServer::JsonValue::fromString(id));
							obj.set("class", mcpServer::JsonValue::fromString(current->GetClassNames()));
							obj.set("visible", mcpServer::JsonValue::fromBool(current->IsVisible()));
							results.append(obj);
							++count;
						}

						for (int i = 0; i < current->GetNumChildren(); ++i)
						{
							auto* child = current->GetChild(i);
							if (child)
								queue.push_back(child);
						}
					}

					auto result = mcpServer::JsonValue::object();
					result.set("count", mcpServer::JsonValue::fromInt(count));
					result.set("elements", results);
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// click_element
		{
			mcpServer::ToolDef tool;
			tool.name = "click_element";
			tool.description = "Simulate a click on an element by ID";
			tool.inputSchema.addProperty("id", "string", "Element ID to click", true);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto id = _params.get("id").getString().toStdString();

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto* editor = getEditor(_processor);
					if (!editor)
						throw std::runtime_error("Plugin editor is not open");

					auto* rmlComp = editor->getRmlComponent();
					if (!rmlComp)
						throw std::runtime_error("RmlUI component not available");

					juceRmlUi::RmlInterfaces::ScopedAccess access(*rmlComp);

					auto* doc = editor->getDocument();
					if (!doc)
						throw std::runtime_error("No RmlUI document loaded");

					auto* elem = doc->GetElementById(id);
					if (!elem)
						throw std::runtime_error("Element not found: " + id);

					elem->Click();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("element", mcpServer::JsonValue::fromString(id));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// set_element_attribute
		{
			mcpServer::ToolDef tool;
			tool.name = "set_element_attribute";
			tool.description = "Set an attribute on an element by ID";
			tool.inputSchema.addProperty("id", "string", "Element ID", true);
			tool.inputSchema.addProperty("attribute", "string", "Attribute name", true);
			tool.inputSchema.addProperty("value", "string", "Attribute value", true);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto id = _params.get("id").getString().toStdString();
				const auto attribute = _params.get("attribute").getString().toStdString();
				const auto value = _params.get("value").getString().toStdString();

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto* editor = getEditor(_processor);
					if (!editor)
						throw std::runtime_error("Plugin editor is not open");

					auto* rmlComp = editor->getRmlComponent();
					if (!rmlComp)
						throw std::runtime_error("RmlUI component not available");

					juceRmlUi::RmlInterfaces::ScopedAccess access(*rmlComp);

					auto* doc = editor->getDocument();
					if (!doc)
						throw std::runtime_error("No RmlUI document loaded");

					auto* elem = doc->GetElementById(id);
					if (!elem)
						throw std::runtime_error("Element not found: " + id);

					elem->SetAttribute(attribute, value);

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}
	}
}

#endif // GEARMULATOR_BUILD_MCP_SERVER
