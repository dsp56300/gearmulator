#include "mcpEditorTools.h"

#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "pluginProcessor.h"

#include "mcpServerLib/mcpServer.h"
#include "mcpServerLib/mcpTool.h"

#include "juceRmlUi/juceRmlComponent.h"

#include <juce_events/juce_events.h>

#include <future>

namespace jucePluginEditorLib
{
	namespace
	{
		// The tool handlers run on the HTTP server's own thread, so anything touching the editor is
		// handed to the message thread and waited for.
		template<typename T>
		T runOnMessageThread(const std::function<T()>& _func)
		{
			auto* mm = juce::MessageManager::getInstanceWithoutCreating();

			if (!mm || mm->isThisTheMessageThread())
				return _func();

			std::promise<T> promise;
			auto future = promise.get_future();

			juce::MessageManager::callAsync([&promise, &_func]
			{
				try
				{
					promise.set_value(_func());
				}
				catch (...)
				{
					promise.set_exception(std::current_exception());
				}
			});

			return future.get();
		}

		void addEditorSize(mcpServer::JsonValue& _result, const Editor* _editor)
		{
			if (!_editor)
				return;

			if (auto* rmlComp = _editor->getRmlComponent())
			{
				_result.set("width", mcpServer::JsonValue::fromInt(rmlComp->getWidth()));
				_result.set("height", mcpServer::JsonValue::fromInt(rmlComp->getHeight()));
			}
		}
	}

	void registerEditorTools(mcpServer::McpServer& _server, Processor& _processor)
	{
		// get_gui_scale
		{
			mcpServer::ToolDef tool;
			tool.name = "get_gui_scale";
			tool.description = "Get the editor GUI scale as a percentage (100 = the skin's native size) plus the current "
				"rendered editor size in pixels. Pair with set_gui_scale to check how the skin's automatic layout reflows.";
			tool.handler = [&_processor](const mcpServer::JsonValue&) -> mcpServer::JsonValue
			{
				return runOnMessageThread<mcpServer::JsonValue>([&]() -> mcpServer::JsonValue
				{
					const int scale = juce::roundToInt(_processor.getConfig().getDoubleValue("scale", 100));

					auto result = mcpServer::JsonValue::object();
					result.set("scale", mcpServer::JsonValue::fromInt(scale));

					if (auto* editorState = _processor.getEditorState())
						addEditorSize(result, editorState->getEditor());

					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// set_gui_scale
		{
			mcpServer::ToolDef tool;
			tool.name = "set_gui_scale";
			tool.description = "Set the editor GUI scale as a percentage (e.g. 100, 150, 200). Resizes the editor live and "
				"persists the choice to the plugin config. Handy for checking how the skin reflows/wraps at different sizes.";
			tool.inputSchema.addIntProperty("scale", "Scale percentage (25-400)", true, 25, 400);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int scale = _params.get("scale").getInt();

				return runOnMessageThread<mcpServer::JsonValue>([&]() -> mcpServer::JsonValue
				{
					auto* editorState = _processor.getEditorState();
					if (!editorState)
						throw std::runtime_error("Editor state not available");

					_processor.getConfig().setValue("scale", juce::var(scale));
					if (editorState->evSetGuiScale)
						editorState->evSetGuiScale(scale);

					auto result = mcpServer::JsonValue::object();
					result.set("success", mcpServer::JsonValue::fromBool(true));
					result.set("scale", mcpServer::JsonValue::fromInt(scale));

					addEditorSize(result, editorState->getEditor());

					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}

		// resize_editor
		{
			mcpServer::ToolDef tool;
			tool.name = "resize_editor";
			tool.description = "Resize the plugin editor window the way a host does when the user drags its border: the "
				"requested size goes through the editor's size constraints (aspect ratio, minimum, maximum) first. Returns "
				"the size the editor ended up with.";
			tool.inputSchema.addIntProperty("width", "Requested width in pixels", true, 1, 16384);
			tool.inputSchema.addIntProperty("height", "Requested height in pixels", true, 1, 16384);
			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int width = _params.get("width").getInt();
				const int height = _params.get("height").getInt();

				return runOnMessageThread<mcpServer::JsonValue>([&]() -> mcpServer::JsonValue
				{
					auto* window = _processor.getActiveEditor();
					if (!window)
						throw std::runtime_error("The editor is not open");

					juce::Rectangle<int> bounds(window->getX(), window->getY(), width, height);
					if (auto* constrainer = window->getConstrainer())
						constrainer->checkBounds(bounds, window->getBounds(), {}, false, false, true, true);
					window->setSize(bounds.getWidth(), bounds.getHeight());

					auto result = mcpServer::JsonValue::object();
					result.set("width", mcpServer::JsonValue::fromInt(window->getWidth()));
					result.set("height", mcpServer::JsonValue::fromInt(window->getHeight()));
					return result;
				});
			};
			_server.registerTool(std::move(tool));
		}
	}
}
