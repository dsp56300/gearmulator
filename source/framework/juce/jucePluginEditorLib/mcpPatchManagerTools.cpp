#include "mcpPatchManagerTools.h"

#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "pluginProcessor.h"
#include "patchmanager/patchmanager.h"

#include "mcpServerLib/mcpServer.h"
#include "mcpServerLib/mcpTool.h"

#include <juce_events/juce_events.h>
#include <future>
#include <thread>

namespace jucePluginEditorLib
{
	namespace
	{
		Editor* getEditor(Processor& _processor)
		{
			auto* editorState = _processor.getEditorState();
			if (!editorState)
				return nullptr;
			return editorState->getEditor();
		}

		patchManager::PatchManager* getPatchManager(Processor& _processor)
		{
			auto* editor = getEditor(_processor);
			if (!editor)
				return nullptr;
			return editor->getPatchManager();
		}

		template<typename F>
		auto runOnMessageThread(F&& _func) -> decltype(_func())
		{
			if (juce::MessageManager::getInstance()->isThisTheMessageThread())
				return _func();

			using ReturnType = decltype(_func());
			std::promise<ReturnType> promise;
			auto future = promise.get_future();

			juce::MessageManager::callAsync([&promise, func = std::forward<F>(_func)]() mutable
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

		mcpServer::JsonValue serializePatch(const pluginLib::patchDB::PatchPtr& _patch, bool _includeDataSource = true)
		{
			if (!_patch)
				return mcpServer::JsonValue::null();

			auto obj = mcpServer::JsonValue::object();
			obj.set("name", mcpServer::JsonValue::fromString(_patch->getName()));

			if (_patch->program != pluginLib::patchDB::g_invalidProgram)
				obj.set("program", mcpServer::JsonValue::fromInt(static_cast<int>(_patch->program)));

			if (_patch->bank != pluginLib::patchDB::g_invalidBank)
				obj.set("bank", mcpServer::JsonValue::fromInt(static_cast<int>(_patch->bank)));

			if (_includeDataSource)
			{
				if (const auto ds = _patch->source.lock())
				{
					obj.set("dataSource", mcpServer::JsonValue::fromString(ds->name));
					obj.set("sourceType", mcpServer::JsonValue::fromString(pluginLib::patchDB::toString(ds->type)));
				}
			}

			// Include tags
			const auto& tags = _patch->getTags();
			auto tagsObj = mcpServer::JsonValue::object();
			bool hasTags = false;

			for (int t = static_cast<int>(pluginLib::patchDB::TagType::Category); t < static_cast<int>(pluginLib::patchDB::TagType::Count); ++t)
			{
				const auto tagType = static_cast<pluginLib::patchDB::TagType>(t);
				const auto& tagSet = _patch->getTags(tagType);
				if (!tagSet.empty())
				{
					auto arr = mcpServer::JsonValue::array();
					for (const auto& tag : tagSet.getAdded())
						arr.append(mcpServer::JsonValue::fromString(tag));
					tagsObj.set(pluginLib::patchDB::toString(tagType), arr);
					hasTags = true;
				}
			}

			if (hasTags)
				obj.set("tags", tagsObj);

			return obj;
		}

		mcpServer::JsonValue serializeDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
		{
			auto obj = mcpServer::JsonValue::object();
			obj.set("name", mcpServer::JsonValue::fromString(_ds->name));
			obj.set("type", mcpServer::JsonValue::fromString(pluginLib::patchDB::toString(_ds->type)));
			obj.set("patchCount", mcpServer::JsonValue::fromInt(static_cast<int>(_ds->patches.size())));

			if (_ds->bank != pluginLib::patchDB::g_invalidBank)
				obj.set("bank", mcpServer::JsonValue::fromInt(static_cast<int>(_ds->bank)));
			if (_ds->midiBankNumber != pluginLib::patchDB::g_invalidMidiBankNumber)
				obj.set("midiBankNumber", mcpServer::JsonValue::fromInt(static_cast<int>(_ds->midiBankNumber)));

			return obj;
		}
	}

	void registerPatchManagerTools(mcpServer::McpServer& _server, Processor& _processor)
	{
		// ---- get_current_preset ----
		{
			mcpServer::ToolDef tool;
			tool.name = "get_current_preset";
			tool.description = "Get the name and details of the currently loaded preset for a part";
			tool.inputSchema.addIntProperty("part", "Part number (0-15)", false, 0, 15);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int part = _params.hasProperty("part") ? _params.get("part").getInt() : 0;

				return runOnMessageThread([&_processor, part]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					const auto& state = pm->getState();
					const auto patchKey = state.getPatch(static_cast<uint32_t>(part));

					if (!patchKey.isValid())
					{
						auto result = mcpServer::JsonValue::object();
						result.set("name", mcpServer::JsonValue::fromString("(unknown)"));
						result.set("part", mcpServer::JsonValue::fromInt(part));
						result.set("hasSelection", mcpServer::JsonValue::fromBool(false));
						return result;
					}

					// Find the actual patch from the search
					const auto searchHandle = state.getSearchHandle(static_cast<uint32_t>(part));
					auto search = pm->getSearch(searchHandle);

					if (search)
					{
						std::shared_lock lock(search->resultsMutex);
						for (const auto& patch : search->results)
						{
							if (*patch == patchKey)
							{
								auto result = serializePatch(patch);
								result.set("part", mcpServer::JsonValue::fromInt(part));
								result.set("hasSelection", mcpServer::JsonValue::fromBool(true));
								return result;
							}
						}
					}

					// Couldn't find in search, return partial info
					auto result = mcpServer::JsonValue::object();
					result.set("part", mcpServer::JsonValue::fromInt(part));
					result.set("hasSelection", mcpServer::JsonValue::fromBool(true));

					if (patchKey.source)
						result.set("dataSource", mcpServer::JsonValue::fromString(patchKey.source->name));

					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- list_data_sources ----
		{
			mcpServer::ToolDef tool;
			tool.name = "list_data_sources";
			tool.description = "List all available preset banks/data sources (ROM banks, user banks, folders, files)";
			tool.inputSchema.addProperty("type", "string", "Filter by source type: rom, folder, file, localstorage", false);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto typeFilter = _params.hasProperty("type") ? _params.get("type").getString().toStdString() : std::string();

				return runOnMessageThread([&_processor, typeFilter]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					std::vector<pluginLib::patchDB::DataSourceNodePtr> dataSources;
					pm->getDataSources(dataSources);

					auto result = mcpServer::JsonValue::array();

					for (const auto& ds : dataSources)
					{
						if (!typeFilter.empty())
						{
							if (pluginLib::patchDB::toString(ds->type) != typeFilter)
								continue;
						}
						result.append(serializeDataSource(ds));
					}

					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- search_presets ----
		{
			mcpServer::ToolDef tool;
			tool.name = "search_presets";
			tool.description = "Search presets by name. Returns a search handle that can be used to retrieve results with get_search_results.";
			tool.inputSchema.addProperty("name", "string", "Search text to match against preset names (case-insensitive substring match)", false);
			tool.inputSchema.addProperty("dataSource", "string", "Filter by data source name (e.g., 'ROM Bank A')", false);
			tool.inputSchema.addProperty("sourceType", "string", "Filter by source type: rom, folder, file, localstorage", false);
			tool.inputSchema.addProperty("category", "string", "Filter by category tag", false);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto name = _params.hasProperty("name") ? _params.get("name").getString().toStdString() : std::string();
				const auto dsName = _params.hasProperty("dataSource") ? _params.get("dataSource").getString().toStdString() : std::string();
				const auto sourceType = _params.hasProperty("sourceType") ? _params.get("sourceType").getString().toStdString() : std::string();
				const auto category = _params.hasProperty("category") ? _params.get("category").getString().toStdString() : std::string();

				return runOnMessageThread([&_processor, name, dsName, sourceType, category]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					pluginLib::patchDB::SearchRequest req;

					if (!name.empty())
					{
						// SearchRequest::match uses matchStringsIgnoreCase which lowercases the
						// test string but not the search string, so we must lowercase it here
						auto lowered = name;
						for (auto& c : lowered)
							c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
						req.name = lowered;
					}

					if (!sourceType.empty())
						req.sourceType = pluginLib::patchDB::toSourceType(sourceType);

					if (!category.empty())
						req.tags.add(pluginLib::patchDB::TagType::Category, category);

					// Find data source by name if specified
					if (!dsName.empty())
					{
						std::vector<pluginLib::patchDB::DataSourceNodePtr> dataSources;
						pm->getDataSources(dataSources);

						for (const auto& ds : dataSources)
						{
							if (ds->name == dsName)
							{
								req.sourceNode = ds;
								break;
							}
						}

						if (!req.sourceNode)
							throw std::runtime_error("Data source '" + dsName + "' not found");
					}

					// Use a promise/future to wait for search completion
					auto completionPromise = std::make_shared<std::promise<void>>();
					auto completionFuture = completionPromise->get_future();

					const auto handle = pm->search(std::move(req), [completionPromise](const pluginLib::patchDB::Search&)
					{
						completionPromise->set_value();
					});

					if (handle == pluginLib::patchDB::g_invalidSearchHandle)
						throw std::runtime_error("Failed to create search");

					auto result = mcpServer::JsonValue::object();
					result.set("searchHandle", mcpServer::JsonValue::fromInt(static_cast<int>(handle)));

					// Wait briefly for results (searches often complete quickly)
					if (completionFuture.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready)
					{
						auto search = pm->getSearch(handle);
						if (search)
						{
							std::shared_lock lock(search->resultsMutex);
							result.set("resultCount", mcpServer::JsonValue::fromInt(static_cast<int>(search->results.size())));
							result.set("state", mcpServer::JsonValue::fromString("completed"));
						}
					}
					else
					{
						result.set("state", mcpServer::JsonValue::fromString("running"));
						result.set("message", mcpServer::JsonValue::fromString("Search is still running. Use get_search_results to check later."));
					}

					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- get_search_results ----
		{
			mcpServer::ToolDef tool;
			tool.name = "get_search_results";
			tool.description = "Get results from a previous search. Returns a paginated list of presets.";
			tool.inputSchema.addIntProperty("searchHandle", "Search handle returned by search_presets", true);
			tool.inputSchema.addIntProperty("offset", "Start index for pagination (default: 0)", false, 0, 100000);
			tool.inputSchema.addIntProperty("limit", "Maximum results to return (default: 50, max: 200)", false, 1, 200);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto handle = static_cast<pluginLib::patchDB::SearchHandle>(_params.get("searchHandle").getInt());
				const int offset = _params.hasProperty("offset") ? _params.get("offset").getInt() : 0;
				const int limit = _params.hasProperty("limit") ? _params.get("limit").getInt() : 50;

				return runOnMessageThread([&_processor, handle, offset, limit]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					auto search = pm->getSearch(handle);
					if (!search)
						throw std::runtime_error("Search handle not found or expired");

					auto result = mcpServer::JsonValue::object();

					const auto stateStr = [&]
					{
						switch (search->state)
						{
						case pluginLib::patchDB::SearchState::NotStarted: return "not_started";
						case pluginLib::patchDB::SearchState::Running: return "running";
						case pluginLib::patchDB::SearchState::Completed: return "completed";
						case pluginLib::patchDB::SearchState::Cancelled: return "cancelled";
						default: return "unknown";
						}
					}();

					result.set("state", mcpServer::JsonValue::fromString(stateStr));
					result.set("searchHandle", mcpServer::JsonValue::fromInt(static_cast<int>(handle)));

					std::vector<pluginLib::patchDB::PatchPtr> patches;
					{
						std::shared_lock lock(search->resultsMutex);
						patches.assign(search->results.begin(), search->results.end());
					}

					result.set("totalCount", mcpServer::JsonValue::fromInt(static_cast<int>(patches.size())));

					auto presets = mcpServer::JsonValue::array();
					const int end = std::min(offset + limit, static_cast<int>(patches.size()));

					for (int i = offset; i < end; ++i)
					{
						auto entry = serializePatch(patches[i]);
						entry.set("index", mcpServer::JsonValue::fromInt(i));
						presets.append(entry);
					}

					result.set("presets", presets);
					result.set("offset", mcpServer::JsonValue::fromInt(offset));
					result.set("returnedCount", mcpServer::JsonValue::fromInt(end - offset));

					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- load_preset ----
		{
			mcpServer::ToolDef tool;
			tool.name = "load_preset";
			tool.description = "Load a preset from search results into a part. First use search_presets to find the preset, then use the searchHandle and index to load it.";
			tool.inputSchema.addIntProperty("searchHandle", "Search handle from search_presets", true);
			tool.inputSchema.addIntProperty("index", "Preset index within search results", true, 0, 100000);
			tool.inputSchema.addIntProperty("part", "Target part number (0-15, default: 0)", false, 0, 15);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto handle = static_cast<pluginLib::patchDB::SearchHandle>(_params.get("searchHandle").getInt());
				const int index = _params.get("index").getInt();
				const int part = _params.hasProperty("part") ? _params.get("part").getInt() : 0;

				return runOnMessageThread([&_processor, handle, index, part]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					auto search = pm->getSearch(handle);
					if (!search)
						throw std::runtime_error("Search handle not found or expired");

					if (search->state != pluginLib::patchDB::SearchState::Completed)
						throw std::runtime_error("Search not yet completed");

					std::vector<pluginLib::patchDB::PatchPtr> patches;
					{
						std::shared_lock lock(search->resultsMutex);
						patches.assign(search->results.begin(), search->results.end());
					}

					if (index < 0 || index >= static_cast<int>(patches.size()))
						throw std::runtime_error("Index " + std::to_string(index) + " out of range (0-" + std::to_string(patches.size() - 1) + ")");

					const auto& patch = patches[index];

					if (!pm->setSelectedPatch(static_cast<uint32_t>(part), patch, handle))
						throw std::runtime_error("Failed to activate preset");

					auto result = serializePatch(patch);
					result.set("part", mcpServer::JsonValue::fromInt(part));
					result.set("loaded", mcpServer::JsonValue::fromBool(true));
					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- load_preset_by_name ----
		{
			mcpServer::ToolDef tool;
			tool.name = "load_preset_by_name";
			tool.description = "Search for a preset by name and load the first match. Convenience tool that combines search + load.";
			tool.inputSchema.addProperty("name", "string", "Preset name to search for (case-insensitive substring match)", true);
			tool.inputSchema.addIntProperty("part", "Target part number (0-15, default: 0)", false, 0, 15);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto name = _params.get("name").getString().toStdString();
				const int part = _params.hasProperty("part") ? _params.get("part").getInt() : 0;

				return runOnMessageThread([&_processor, name, part]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					pluginLib::patchDB::SearchRequest req;

					auto lowered = name;
					for (auto& c : lowered)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					req.name = lowered;

					auto completionPromise = std::make_shared<std::promise<void>>();
					auto completionFuture = completionPromise->get_future();

					const auto handle = pm->search(std::move(req), [completionPromise](const pluginLib::patchDB::Search&)
					{
						completionPromise->set_value();
					});

					if (handle == pluginLib::patchDB::g_invalidSearchHandle)
						throw std::runtime_error("Failed to create search");

					// Wait for search to complete (up to 5 seconds)
					if (completionFuture.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
						throw std::runtime_error("Search timed out");

					auto search = pm->getSearch(handle);
					if (!search)
						throw std::runtime_error("Search results not available");

					std::vector<pluginLib::patchDB::PatchPtr> patches;
					{
						std::shared_lock lock(search->resultsMutex);
						patches.assign(search->results.begin(), search->results.end());
					}

					if (patches.empty())
						throw std::runtime_error("No presets found matching '" + name + "'");

					// Find exact match first, then use first result
					pluginLib::patchDB::PatchPtr bestMatch;
					for (const auto& p : patches)
					{
						if (p->getName() == name)
						{
							bestMatch = p;
							break;
						}
					}

					if (!bestMatch)
						bestMatch = patches.front();

					if (!pm->setSelectedPatch(static_cast<uint32_t>(part), bestMatch, handle))
						throw std::runtime_error("Failed to activate preset '" + bestMatch->getName() + "'");

					auto result = serializePatch(bestMatch);
					result.set("part", mcpServer::JsonValue::fromInt(part));
					result.set("loaded", mcpServer::JsonValue::fromBool(true));
					result.set("totalMatches", mcpServer::JsonValue::fromInt(static_cast<int>(patches.size())));
					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- select_next_preset / select_prev_preset ----
		for (const auto& [toolName, offset] : std::initializer_list<std::pair<const char*, int>>{{"select_next_preset", 1}, {"select_prev_preset", -1}})
		{
			const auto dir = offset;
			mcpServer::ToolDef tool;
			tool.name = toolName;
			tool.description = std::string(dir > 0 ? "Select the next" : "Select the previous") + " preset in the current search results for a part";
			tool.inputSchema.addIntProperty("part", "Part number (0-15, default: 0)", false, 0, 15);

			tool.handler = [&_processor, dir](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const int part = _params.hasProperty("part") ? _params.get("part").getInt() : 0;

				return runOnMessageThread([&_processor, part, dir]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					const bool success = dir > 0
						? pm->selectNextPreset(static_cast<uint32_t>(part))
						: pm->selectPrevPreset(static_cast<uint32_t>(part));

					if (!success)
						throw std::runtime_error("No preset to navigate to (no active search or single result)");

					// Return the newly selected preset
					const auto& state = pm->getState();
					const auto patchKey = state.getPatch(static_cast<uint32_t>(part));
					const auto searchHandle = state.getSearchHandle(static_cast<uint32_t>(part));
					auto search = pm->getSearch(searchHandle);

					auto result = mcpServer::JsonValue::object();
					result.set("part", mcpServer::JsonValue::fromInt(part));
					result.set("success", mcpServer::JsonValue::fromBool(true));

					if (search)
					{
						std::shared_lock lock(search->resultsMutex);
						for (const auto& patch : search->results)
						{
							if (*patch == patchKey)
							{
								result.set("preset", serializePatch(patch));
								break;
							}
						}
					}

					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- save_preset ----
		{
			mcpServer::ToolDef tool;
			tool.name = "save_preset";
			tool.description = "Save the current patch from a part to a user bank. If no user bank exists, one will be created.";
			tool.inputSchema.addProperty("bankName", "string", "Name of the user bank to save to (default: first available or 'User Bank')", false);
			tool.inputSchema.addIntProperty("part", "Part number (0-15, default: 0)", false, 0, 15);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto bankName = _params.hasProperty("bankName") ? _params.get("bankName").getString().toStdString() : std::string();
				const int part = _params.hasProperty("part") ? _params.get("part").getInt() : 0;

				return runOnMessageThread([&_processor, bankName, part]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					const auto currentPatch = pm->requestPatchForPart(static_cast<uint32_t>(part));
					if (!currentPatch)
						throw std::runtime_error("Could not get current patch data for part " + std::to_string(part));

					// Find or create user bank
					pluginLib::patchDB::DataSourceNodePtr targetDs;
					const auto existingLocalDS = pm->getDataSourcesOfSourceType(pluginLib::patchDB::SourceType::LocalStorage);

					if (!bankName.empty())
					{
						for (const auto& ds : existingLocalDS)
						{
							if (ds->name == bankName)
							{
								targetDs = ds;
								break;
							}
						}
					}
					else if (!existingLocalDS.empty())
					{
						targetDs = *existingLocalDS.begin();
					}

					if (!targetDs)
					{
						// Create new user bank
						pluginLib::patchDB::DataSource ds;
						ds.name = bankName.empty() ? "User Bank" : bankName;
						ds.type = pluginLib::patchDB::SourceType::LocalStorage;
						ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
						ds.timestamp = std::chrono::system_clock::now();

						auto createPromise = std::make_shared<std::promise<pluginLib::patchDB::DataSourceNodePtr>>();
						auto createFuture = createPromise->get_future();

						pm->addDataSource(ds, [createPromise](const bool _success, const std::shared_ptr<pluginLib::patchDB::DataSourceNode>& _ds)
						{
							if (_success)
								createPromise->set_value(_ds);
							else
								createPromise->set_value(nullptr);
						});

						if (createFuture.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
							throw std::runtime_error("Timed out creating user bank");

						targetDs = createFuture.get();
						if (!targetDs)
							throw std::runtime_error("Failed to create user bank");
					}

					// Save patch
					pm->copyPatchesToLocalStorage(targetDs, {currentPatch}, part);

					auto result = mcpServer::JsonValue::object();
					result.set("saved", mcpServer::JsonValue::fromBool(true));
					result.set("presetName", mcpServer::JsonValue::fromString(currentPatch->getName()));
					result.set("bankName", mcpServer::JsonValue::fromString(targetDs->name));
					result.set("part", mcpServer::JsonValue::fromInt(part));
					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}

		// ---- rename_preset ----
		{
			mcpServer::ToolDef tool;
			tool.name = "rename_preset";
			tool.description = "Rename the currently loaded preset for a part";
			tool.inputSchema.addProperty("name", "string", "New name for the preset", true);
			tool.inputSchema.addIntProperty("part", "Part number (0-15, default: 0)", false, 0, 15);

			tool.handler = [&_processor](const mcpServer::JsonValue& _params) -> mcpServer::JsonValue
			{
				const auto newName = _params.get("name").getString().toStdString();
				const int part = _params.hasProperty("part") ? _params.get("part").getInt() : 0;

				return runOnMessageThread([&_processor, newName, part]() -> mcpServer::JsonValue
				{
					auto* pm = getPatchManager(_processor);
					if (!pm)
						throw std::runtime_error("Patch manager not available (editor may not be open)");

					const auto& state = pm->getState();
					const auto patchKey = state.getPatch(static_cast<uint32_t>(part));

					if (!patchKey.isValid())
						throw std::runtime_error("No preset selected for part " + std::to_string(part));

					const auto searchHandle = state.getSearchHandle(static_cast<uint32_t>(part));
					auto search = pm->getSearch(searchHandle);

					if (!search)
						throw std::runtime_error("Could not find preset in database");

					pluginLib::patchDB::PatchPtr patchToRename;
					{
						std::shared_lock lock(search->resultsMutex);
						for (const auto& patch : search->results)
						{
							if (*patch == patchKey)
							{
								patchToRename = patch;
								break;
							}
						}
					}

					if (!patchToRename)
						throw std::runtime_error("Preset not found in search results");

					if (!pm->renamePatch(patchToRename, newName))
						throw std::runtime_error("Failed to rename preset");

					auto result = mcpServer::JsonValue::object();
					result.set("renamed", mcpServer::JsonValue::fromBool(true));
					result.set("newName", mcpServer::JsonValue::fromString(newName));
					result.set("part", mcpServer::JsonValue::fromInt(part));
					return result;
				});
			};

			_server.registerTool(std::move(tool));
		}
	}
}
