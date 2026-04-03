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
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Input.h"

#include <juce_events/juce_events.h>
#include <future>
#include <thread>

namespace jucePluginEditorLib
{
	namespace
	{
		// Helper to get plain text content from an element (strips HTML tags)
		std::string getTextContent(Rml::Element* _elem)
		{
			std::string result;
			for (int i = 0; i < _elem->GetNumChildren(true); ++i)
			{
				auto* child = _elem->GetChild(i);
				if (!child) continue;
				if (child->GetTagName() == "#text")
				{
					// Text node — get its content via the parent's inner RML at this position
					// Use the simpler approach: just get InnerRML and strip tags
				}
				else
				{
					result += getTextContent(child);
				}
			}
			if (result.empty())
			{
				// Fallback: use GetInnerRML and strip any HTML tags
				auto rml = _elem->GetInnerRML();
				std::string text;
				bool inTag = false;
				for (const char c : rml)
				{
					if (c == '<') inTag = true;
					else if (c == '>') inTag = false;
					else if (!inTag) text += c;
				}
				// Trim whitespace
				const auto start = text.find_first_not_of(" \t\n\r");
				if (start == std::string::npos) return {};
				const auto end = text.find_last_not_of(" \t\n\r");
				return text.substr(start, end - start + 1);
			}
			return result;
		}

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

			// Text content (leaf-level or inner text)
			const auto text = getTextContent(_elem);
			if (!text.empty())
				obj.set("text", mcpServer::JsonValue::fromString(text));

			// Box position and size
			if (_elem->IsVisible())
			{
				const auto offset = _elem->GetAbsoluteOffset(Rml::BoxArea::Border);
				const auto size = _elem->GetBox().GetSize(Rml::BoxArea::Border);
				if (size.x > 0 || size.y > 0)
				{
					auto box = mcpServer::JsonValue::object();
					box.set("x", mcpServer::JsonValue::fromInt(static_cast<int>(offset.x)));
					box.set("y", mcpServer::JsonValue::fromInt(static_cast<int>(offset.y)));
					box.set("w", mcpServer::JsonValue::fromInt(static_cast<int>(size.x)));
					box.set("h", mcpServer::JsonValue::fromInt(static_cast<int>(size.y)));
					obj.set("box", box);
				}
			}

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

		int parseMouseButton(const mcpServer::JsonValue& _params)
		{
			if (!_params.isObject() || !_params.hasProperty("button"))
				return 0; // left

			const auto btn = _params.get("button").getString().toStdString();
			if (btn == "right")  return 1;
			if (btn == "middle") return 2;
			return 0; // left
		}

		int parseModifiers(const mcpServer::JsonValue& _params)
		{
			int mods = 0;
			if (!_params.isObject() || !_params.hasProperty("modifiers"))
				return mods;

			const auto m = _params.get("modifiers");
			if (m.isObject())
			{
				if (m.hasProperty("ctrl") && m.get("ctrl").getBool())   mods |= Rml::Input::KM_CTRL;
				if (m.hasProperty("shift") && m.get("shift").getBool()) mods |= Rml::Input::KM_SHIFT;
				if (m.hasProperty("alt") && m.get("alt").getBool())     mods |= Rml::Input::KM_ALT;
				if (m.hasProperty("meta") && m.get("meta").getBool())   mods |= Rml::Input::KM_META;
			}
			return mods;
		}

		std::pair<int, int> getElementCenter(Rml::Element* _elem)
		{
			const auto offset = _elem->GetAbsoluteOffset(Rml::BoxArea::Border);
			const auto size = _elem->GetBox().GetSize(Rml::BoxArea::Border);
			return {
				static_cast<int>(offset.x + size.x * 0.5f),
				static_cast<int>(offset.y + size.y * 0.5f)
			};
		}

		Rml::Input::KeyIdentifier parseKeyName(const std::string& _name)
		{
			if (_name == "space")    return Rml::Input::KI_SPACE;
			if (_name == "return" || _name == "enter") return Rml::Input::KI_RETURN;
			if (_name == "escape" || _name == "esc")   return Rml::Input::KI_ESCAPE;
			if (_name == "backspace") return Rml::Input::KI_BACK;
			if (_name == "delete")   return Rml::Input::KI_DELETE;
			if (_name == "insert")   return Rml::Input::KI_INSERT;
			if (_name == "tab")      return Rml::Input::KI_TAB;
			if (_name == "left")     return Rml::Input::KI_LEFT;
			if (_name == "right")    return Rml::Input::KI_RIGHT;
			if (_name == "up")       return Rml::Input::KI_UP;
			if (_name == "down")     return Rml::Input::KI_DOWN;
			if (_name == "home")     return Rml::Input::KI_HOME;
			if (_name == "end")      return Rml::Input::KI_END;
			if (_name == "pageup")   return Rml::Input::KI_PRIOR;
			if (_name == "pagedown") return Rml::Input::KI_NEXT;

			// F-keys
			if (_name == "f1")  return Rml::Input::KI_F1;
			if (_name == "f2")  return Rml::Input::KI_F2;
			if (_name == "f3")  return Rml::Input::KI_F3;
			if (_name == "f4")  return Rml::Input::KI_F4;
			if (_name == "f5")  return Rml::Input::KI_F5;
			if (_name == "f6")  return Rml::Input::KI_F6;
			if (_name == "f7")  return Rml::Input::KI_F7;
			if (_name == "f8")  return Rml::Input::KI_F8;
			if (_name == "f9")  return Rml::Input::KI_F9;
			if (_name == "f10") return Rml::Input::KI_F10;
			if (_name == "f11") return Rml::Input::KI_F11;
			if (_name == "f12") return Rml::Input::KI_F12;

			// Single letters a-z
			if (_name.size() == 1)
			{
				const char c = _name[0];
				if (c >= 'a' && c <= 'z') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (c - 'a'));
				if (c >= 'A' && c <= 'Z') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (c - 'A'));
				if (c >= '0' && c <= '9') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (c - '0'));
			}

			return Rml::Input::KI_UNKNOWN;
		}

		struct RmlUiAccess
		{
			Editor* editor = nullptr;
			juceRmlUi::RmlComponent* rmlComp = nullptr;
			Rml::Context* context = nullptr;
			Rml::ElementDocument* document = nullptr;

			static RmlUiAccess acquire(Processor& _processor)
			{
				RmlUiAccess a;
				a.editor = getEditor(_processor);
				if (!a.editor)
					throw std::runtime_error("Plugin editor is not open. Open the plugin window first.");
				a.rmlComp = a.editor->getRmlComponent();
				if (!a.rmlComp)
					throw std::runtime_error("RmlUI component not available");
				a.context = a.rmlComp->getContext();
				if (!a.context)
					throw std::runtime_error("RmlUI context not available");
				a.document = a.editor->getDocument();
				if (!a.document)
					throw std::runtime_error("No RmlUI document loaded");
				return a;
			}
		};
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
			tool.description = "Find elements by tag name or CSS selector. Returns text content and box position for each match.";
			tool.inputSchema.addProperty("tag", "string", "Tag name to search for (e.g. 'div', 'button', 'select')", false);
			tool.inputSchema.addProperty("selector", "string", "CSS selector (e.g. '.menuitem', 'div.active', '#panel > div')", false);
			tool.inputSchema.addIntProperty("limit", "Maximum number of results (default: 50)", false, 1, 500);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto tag = _params.hasProperty("tag") ? _params.get("tag").getString().toStdString() : std::string();
				const auto selector = _params.hasProperty("selector") ? _params.get("selector").getString().toStdString() : std::string();
				const int limit = _params.hasProperty("limit") ? _params.get("limit").getInt() : 50;

				if (tag.empty() && selector.empty())
					throw std::runtime_error("Either 'tag' or 'selector' must be provided");

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

					std::vector<Rml::Element*> matches;

					if (!selector.empty())
					{
						Rml::ElementList elements;
						doc->QuerySelectorAll(elements, selector);
						for (auto* el : elements)
						{
							if (static_cast<int>(matches.size()) >= limit) break;
							matches.push_back(el);
						}
					}
					else
					{
						// BFS to find matching elements by tag
						std::vector<Rml::Element*> queue;
						queue.push_back(doc);

						while (!queue.empty() && static_cast<int>(matches.size()) < limit)
						{
							auto* current = queue.front();
							queue.erase(queue.begin());

							if (current->GetTagName() == tag)
								matches.push_back(current);

							for (int i = 0; i < current->GetNumChildren(); ++i)
							{
								auto* child = current->GetChild(i);
								if (child)
									queue.push_back(child);
							}
						}
					}

					auto results = mcpServer::JsonValue::array();
					int idx = 0;
					for (auto* elem : matches)
					{
						auto obj = mcpServer::JsonValue::object();
						obj.set("index", mcpServer::JsonValue::fromInt(idx++));
						obj.set("tag", mcpServer::JsonValue::fromString(elem->GetTagName()));

						const auto& id = elem->GetId();
						if (!id.empty())
							obj.set("id", mcpServer::JsonValue::fromString(id));

						const auto classNames = elem->GetClassNames();
						if (!classNames.empty())
							obj.set("class", mcpServer::JsonValue::fromString(classNames));

						const auto text = getTextContent(elem);
						if (!text.empty())
							obj.set("text", mcpServer::JsonValue::fromString(text));

						obj.set("visible", mcpServer::JsonValue::fromBool(elem->IsVisible()));

						if (elem->IsVisible())
						{
							const auto offset = elem->GetAbsoluteOffset(Rml::BoxArea::Border);
							const auto size = elem->GetBox().GetSize(Rml::BoxArea::Border);
							if (size.x > 0 || size.y > 0)
							{
								auto box = mcpServer::JsonValue::object();
								box.set("x", mcpServer::JsonValue::fromInt(static_cast<int>(offset.x)));
								box.set("y", mcpServer::JsonValue::fromInt(static_cast<int>(offset.y)));
								box.set("w", mcpServer::JsonValue::fromInt(static_cast<int>(size.x)));
								box.set("h", mcpServer::JsonValue::fromInt(static_cast<int>(size.y)));
								obj.set("box", box);
							}
						}

						results.append(obj);
					}

					auto result = mcpServer::JsonValue::object();
					result.set("count", mcpServer::JsonValue::fromInt(static_cast<int>(matches.size())));
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
			tool.description = "Simulate a mouse click on an element by ID or CSS selector. Injects mouse move, button down, and button up through the RmlUI context. Use clickCount=2 for double-click.";
			tool.inputSchema.addProperty("id", "string", "Element ID to click", false);
			tool.inputSchema.addProperty("selector", "string", "CSS selector to find the element (e.g. '.menuitem', 'div.active'). Uses first match.", false);
			tool.inputSchema.addProperty("button", "string", "Mouse button: 'left' (default), 'right', or 'middle'", false);
			tool.inputSchema.addIntProperty("clickCount", "Number of clicks (default: 1, use 2 for double-click)", false, 1, 3);
			tool.inputSchema.addProperty("modifiers", "object", "Modifier keys: {ctrl, shift, alt, meta} as booleans", false);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto id = _params.hasProperty("id") ? _params.get("id").getString().toStdString() : std::string();
				const auto selector = _params.hasProperty("selector") ? _params.get("selector").getString().toStdString() : std::string();
				const int button = parseMouseButton(_params);
				const int mods = parseModifiers(_params);
				const int clickCount = _params.hasProperty("clickCount") ? _params.get("clickCount").getInt() : 1;

				if (id.empty() && selector.empty())
					throw std::runtime_error("Either 'id' or 'selector' must be provided");

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					Rml::Element* elem = nullptr;
					std::string elemDesc;

					if (!id.empty())
					{
						elem = ui.document->GetElementById(id);
						elemDesc = id;
					}
					else
					{
						elem = ui.document->QuerySelector(selector);
						elemDesc = selector;
					}

					if (!elem)
						throw std::runtime_error("Element not found: " + elemDesc);

					const auto [cx, cy] = getElementCenter(elem);

					ui.context->ProcessMouseMove(cx, cy, mods);
					for (int i = 0; i < clickCount; ++i)
					{
						ui.context->ProcessMouseButtonDown(button, mods);
						ui.context->ProcessMouseButtonUp(button, mods);
					}
					ui.rmlComp->enqueueUpdate();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("element", mcpServer::JsonValue::fromString(elemDesc));
					const auto& elemId = elem->GetId();
					if (!elemId.empty() && elemId != elemDesc)
						result.set("id", mcpServer::JsonValue::fromString(elemId));
					const auto text = getTextContent(elem);
					if (!text.empty())
						result.set("text", mcpServer::JsonValue::fromString(text));
					result.set("x", mcpServer::JsonValue::fromInt(cx));
					result.set("y", mcpServer::JsonValue::fromInt(cy));
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

		// mouse_move - move the mouse cursor to coordinates or an element's center
		{
			mcpServer::ToolDef tool;
			tool.name = "mouse_move";
			tool.description = "Move the mouse cursor to specific coordinates or to the center of an element. Coordinates are in RmlUI document space.";
			tool.inputSchema.addProperty("id", "string", "Element ID to move to (uses element center). Either 'id' or 'x'+'y' must be provided.", false);
			tool.inputSchema.addIntProperty("x", "X coordinate in document space", false);
			tool.inputSchema.addIntProperty("y", "Y coordinate in document space", false);
			tool.inputSchema.addProperty("modifiers", "object", "Modifier keys: {ctrl, shift, alt, meta} as booleans", false);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int mods = parseModifiers(_params);

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					int x, y;
					if (_params.hasProperty("id"))
					{
						const auto id = _params.get("id").getString().toStdString();
						auto* elem = ui.document->GetElementById(id);
						if (!elem)
							throw std::runtime_error("Element not found: " + id);
						std::tie(x, y) = getElementCenter(elem);
					}
					else if (_params.hasProperty("x") && _params.hasProperty("y"))
					{
						x = _params.get("x").getInt();
						y = _params.get("y").getInt();
					}
					else
					{
						throw std::runtime_error("Either 'id' or both 'x' and 'y' must be provided");
					}

					ui.context->ProcessMouseMove(x, y, mods);
					ui.rmlComp->enqueueUpdate();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("x", mcpServer::JsonValue::fromInt(x));
					result.set("y", mcpServer::JsonValue::fromInt(y));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// element_at_point - hit-test: find the element at a given coordinate
		{
			mcpServer::ToolDef tool;
			tool.name = "element_at_point";
			tool.description = "Hit-test: find the topmost element at a given point in document space. Returns the element's tag, id, classes, attributes, and its ancestor chain up to the document root.";
			tool.inputSchema.addIntProperty("x", "X coordinate in document space", true);
			tool.inputSchema.addIntProperty("y", "Y coordinate in document space", true);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int x = _params.get("x").getInt();
				const int y = _params.get("y").getInt();

				return runOnMessageThread([&, x, y]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					auto* elem = ui.context->GetElementAtPoint(Rml::Vector2f(static_cast<float>(x), static_cast<float>(y)));
					if(!elem)
						throw std::runtime_error("No element at point");

					auto result = mcpServer::JsonValue::object();
					result.set("x", mcpServer::JsonValue::fromInt(x));
					result.set("y", mcpServer::JsonValue::fromInt(y));
					result.set("tag", mcpServer::JsonValue::fromString(elem->GetTagName()));
					result.set("id", mcpServer::JsonValue::fromString(elem->GetId()));
					result.set("class", mcpServer::JsonValue::fromString(elem->GetAttribute("class", std::string())));

					// Collect attributes
					auto attrs = mcpServer::JsonValue::object();
					for(const auto& [name, variant] : elem->GetAttributes())
						attrs.set(name, mcpServer::JsonValue::fromString(variant.Get<Rml::String>(elem->GetCoreInstance())));
					result.set("attributes", std::move(attrs));

					// Box
					const auto box = elem->GetAbsoluteOffset(Rml::BoxArea::Border);
					const auto size = elem->GetBox().GetSize(Rml::BoxArea::Border);
					auto boxObj = mcpServer::JsonValue::object();
					boxObj.set("x", mcpServer::JsonValue::fromInt(static_cast<int>(box.x)));
					boxObj.set("y", mcpServer::JsonValue::fromInt(static_cast<int>(box.y)));
					boxObj.set("w", mcpServer::JsonValue::fromInt(static_cast<int>(size.x)));
					boxObj.set("h", mcpServer::JsonValue::fromInt(static_cast<int>(size.y)));
					result.set("box", std::move(boxObj));

					// Ancestor chain
					auto ancestors = mcpServer::JsonValue::array();
					auto* parent = elem->GetParentNode();
					while(parent)
					{
						auto anc = mcpServer::JsonValue::object();
						anc.set("tag", mcpServer::JsonValue::fromString(parent->GetTagName()));
						anc.set("id", mcpServer::JsonValue::fromString(parent->GetId()));
						anc.set("class", mcpServer::JsonValue::fromString(parent->GetAttribute("class", std::string())));
						ancestors.append(std::move(anc));
						parent = parent->GetParentNode();
					}
					result.set("ancestors", std::move(ancestors));

					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// screenshot - capture the plugin UI as a PNG image
		{
			mcpServer::ToolDef tool;
			tool.name = "screenshot";
			tool.description = "Capture a screenshot of the plugin editor UI. Saves as PNG to a temp file and returns the path. Use the Read tool to view the image.";
			tool.handler = [&_processor](const mcpServer::JsonValue&) -> mcpServer::JsonValue
			{
				const auto tempDir = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory);
				const auto file = tempDir.getChildFile("gearmulator_screenshot.png");
				auto path = std::make_shared<std::string>(file.getFullPathName().toStdString());
				auto done = std::make_shared<std::atomic<bool>>(false);
				auto success = std::make_shared<std::atomic<bool>>(false);

				juce::MessageManager::callAsync([&_processor, path, done, success]()
				{
					auto ui = RmlUiAccess::acquire(_processor);
					if(!ui.rmlComp)
					{
						*done = true;
						return;
					}
					ui.rmlComp->takeScreenshot([path, done, success](const juce::Image& _image)
					{
						juce::File f(*path);
						juce::PNGImageFormat png;
						if(auto stream = f.createOutputStream())
						{
							*success = png.writeImageToStream(_image, *stream);
							stream->flush();
						}
						*done = true;
					});
				});

				for(int i = 0; i < 500 && !*done; ++i)
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

				if(!*success)
					throw std::runtime_error("Failed to capture screenshot");

				auto result = mcpServer::JsonValue::object();
				result.set("path", mcpServer::JsonValue::fromString(*path));
				return result;
			};
			_server.registerTool(std::move(tool));
		}

		// mouse_click_at - click at specific coordinates
		{
			mcpServer::ToolDef tool;
			tool.name = "mouse_click_at";
			tool.description = "Simulate a mouse click at specific coordinates. Injects mouse move, button down, and button up. Use clickCount=2 for double-click.";
			tool.inputSchema.addIntProperty("x", "X coordinate in document space", true);
			tool.inputSchema.addIntProperty("y", "Y coordinate in document space", true);
			tool.inputSchema.addProperty("button", "string", "Mouse button: 'left' (default), 'right', or 'middle'", false);
			tool.inputSchema.addIntProperty("clickCount", "Number of clicks (default: 1, use 2 for double-click)", false, 1, 3);
			tool.inputSchema.addProperty("modifiers", "object", "Modifier keys: {ctrl, shift, alt, meta} as booleans", false);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int x = _params.get("x").getInt();
				const int y = _params.get("y").getInt();
				const int button = parseMouseButton(_params);
				const int mods = parseModifiers(_params);
				const int clickCount = _params.hasProperty("clickCount") ? _params.get("clickCount").getInt() : 1;

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					ui.context->ProcessMouseMove(x, y, mods);
					for (int i = 0; i < clickCount; ++i)
					{
						ui.context->ProcessMouseButtonDown(button, mods);
						ui.context->ProcessMouseButtonUp(button, mods);
					}
					ui.rmlComp->enqueueUpdate();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("x", mcpServer::JsonValue::fromInt(x));
					result.set("y", mcpServer::JsonValue::fromInt(y));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// mouse_drag - drag from one position to another
		{
			mcpServer::ToolDef tool;
			tool.name = "mouse_drag";
			tool.description = "Simulate a mouse drag from one position to another. Supports element IDs or coordinates for start/end. Generates intermediate mouse move events for smooth dragging.";
			tool.inputSchema.addProperty("fromId", "string", "Element ID to start drag from (uses center)", false);
			tool.inputSchema.addIntProperty("fromX", "Start X coordinate", false);
			tool.inputSchema.addIntProperty("fromY", "Start Y coordinate", false);
			tool.inputSchema.addProperty("toId", "string", "Element ID to drag to (uses center)", false);
			tool.inputSchema.addIntProperty("toX", "End X coordinate", false);
			tool.inputSchema.addIntProperty("toY", "End Y coordinate", false);
			tool.inputSchema.addProperty("button", "string", "Mouse button: 'left' (default), 'right', or 'middle'", false);
			tool.inputSchema.addIntProperty("steps", "Number of intermediate move steps (default: 10)", false, 1, 100);
			tool.inputSchema.addProperty("modifiers", "object", "Modifier keys: {ctrl, shift, alt, meta} as booleans", false);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int button = parseMouseButton(_params);
				const int mods = parseModifiers(_params);
				const int steps = _params.hasProperty("steps") ? _params.get("steps").getInt() : 10;

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					int fromX, fromY, toX, toY;

					if (_params.hasProperty("fromId"))
					{
						auto* elem = ui.document->GetElementById(_params.get("fromId").getString().toStdString());
						if (!elem) throw std::runtime_error("Start element not found: " + _params.get("fromId").getString().toStdString());
						std::tie(fromX, fromY) = getElementCenter(elem);
					}
					else if (_params.hasProperty("fromX") && _params.hasProperty("fromY"))
					{
						fromX = _params.get("fromX").getInt();
						fromY = _params.get("fromY").getInt();
					}
					else
					{
						throw std::runtime_error("Either 'fromId' or both 'fromX' and 'fromY' must be provided");
					}

					if (_params.hasProperty("toId"))
					{
						auto* elem = ui.document->GetElementById(_params.get("toId").getString().toStdString());
						if (!elem) throw std::runtime_error("End element not found: " + _params.get("toId").getString().toStdString());
						std::tie(toX, toY) = getElementCenter(elem);
					}
					else if (_params.hasProperty("toX") && _params.hasProperty("toY"))
					{
						toX = _params.get("toX").getInt();
						toY = _params.get("toY").getInt();
					}
					else
					{
						throw std::runtime_error("Either 'toId' or both 'toX' and 'toY' must be provided");
					}

					// Move to start, press button
					ui.context->ProcessMouseMove(fromX, fromY, mods);
					ui.context->ProcessMouseButtonDown(button, mods);

					// Generate intermediate drag moves
					for (int i = 1; i <= steps; ++i)
					{
						const float t = static_cast<float>(i) / static_cast<float>(steps);
						const int mx = fromX + static_cast<int>(static_cast<float>(toX - fromX) * t);
						const int my = fromY + static_cast<int>(static_cast<float>(toY - fromY) * t);
						ui.context->ProcessMouseMove(mx, my, mods);
					}

					// Release button
					ui.context->ProcessMouseButtonUp(button, mods);
					ui.rmlComp->enqueueUpdate();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("fromX", mcpServer::JsonValue::fromInt(fromX));
					result.set("fromY", mcpServer::JsonValue::fromInt(fromY));
					result.set("toX", mcpServer::JsonValue::fromInt(toX));
					result.set("toY", mcpServer::JsonValue::fromInt(toY));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// mouse_wheel - inject scroll wheel events
		{
			mcpServer::ToolDef tool;
			tool.name = "mouse_wheel";
			tool.description = "Simulate mouse wheel scrolling at coordinates or on an element. Positive deltaY scrolls down, negative scrolls up.";
			tool.inputSchema.addProperty("id", "string", "Element ID to scroll on (moves mouse to center first). Either 'id' or 'x'+'y' must be provided.", false);
			tool.inputSchema.addIntProperty("x", "X coordinate to scroll at", false);
			tool.inputSchema.addIntProperty("y", "Y coordinate to scroll at", false);
			tool.inputSchema.addProperty("deltaX", "number", "Horizontal scroll delta (default: 0)", false);
			tool.inputSchema.addProperty("deltaY", "number", "Vertical scroll delta (default: 0)", false);
			tool.inputSchema.addProperty("modifiers", "object", "Modifier keys: {ctrl, shift, alt, meta} as booleans", false);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int mods = parseModifiers(_params);
				const auto deltaX = _params.hasProperty("deltaX") ? static_cast<float>(_params.get("deltaX").getDouble()) : 0.0f;
				const auto deltaY = _params.hasProperty("deltaY") ? static_cast<float>(_params.get("deltaY").getDouble()) : 0.0f;

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					int x, y;
					if (_params.hasProperty("id"))
					{
						const auto id = _params.get("id").getString().toStdString();
						auto* elem = ui.document->GetElementById(id);
						if (!elem)
							throw std::runtime_error("Element not found: " + id);
						std::tie(x, y) = getElementCenter(elem);
					}
					else if (_params.hasProperty("x") && _params.hasProperty("y"))
					{
						x = _params.get("x").getInt();
						y = _params.get("y").getInt();
					}
					else
					{
						throw std::runtime_error("Either 'id' or both 'x' and 'y' must be provided");
					}

					ui.context->ProcessMouseMove(x, y, mods);
					ui.context->ProcessMouseWheel(Rml::Vector2f(deltaX, deltaY), mods);
					ui.rmlComp->enqueueUpdate();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("x", mcpServer::JsonValue::fromInt(x));
					result.set("y", mcpServer::JsonValue::fromInt(y));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// send_key - inject keyboard events
		{
			mcpServer::ToolDef tool;
			tool.name = "send_key";
			tool.description = "Simulate a key press, release, or press+release. Supported key names: a-z, 0-9, space, return/enter, escape/esc, backspace, delete, insert, tab, left, right, up, down, home, end, pageup, pagedown, f1-f12.";
			tool.inputSchema.addProperty("key", "string", "Key name (e.g. 'space', 'return', 'a', 'f1', 'up')", true);
			tool.inputSchema.addEnumProperty("action", "Key action (default: 'press' = down+up)", {"press", "down", "up"}, false);
			tool.inputSchema.addProperty("modifiers", "object", "Modifier keys: {ctrl, shift, alt, meta} as booleans", false);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto keyName = _params.get("key").getString().toStdString();
				const auto action = _params.hasProperty("action") ? _params.get("action").getString().toStdString() : std::string("press");
				const int mods = parseModifiers(_params);

				const auto keyId = parseKeyName(keyName);
				if (keyId == Rml::Input::KI_UNKNOWN)
					throw std::runtime_error("Unknown key name: " + keyName);

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					if (action == "down" || action == "press")
						ui.context->ProcessKeyDown(keyId, mods);
					if (action == "up" || action == "press")
						ui.context->ProcessKeyUp(keyId, mods);
					ui.rmlComp->enqueueUpdate();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("key", mcpServer::JsonValue::fromString(keyName));
					result.set("action", mcpServer::JsonValue::fromString(action));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// send_text - inject text input
		{
			mcpServer::ToolDef tool;
			tool.name = "send_text";
			tool.description = "Inject text input into the focused element, character by character. Use this for typing into text fields and inputs.";
			tool.inputSchema.addProperty("text", "string", "Text to inject", true);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto text = _params.get("text").getString().toStdString();

				return runOnMessageThread([&]() -> mcpServer::JsonValue
				{
					auto ui = RmlUiAccess::acquire(_processor);
					juceRmlUi::RmlInterfaces::ScopedAccess access(*ui.rmlComp);

					ui.context->ProcessTextInput(text);
					ui.rmlComp->enqueueUpdate();

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("length", mcpServer::JsonValue::fromInt(static_cast<int>(text.size())));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}
	}
}
