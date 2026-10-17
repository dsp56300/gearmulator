#pragma once

#include <string>
#include <vector>

#include "asset.h"
#include "elevatedTask.h"
#include "zipExtractor.h"

#include "juce_core/juce_core.h"

namespace pluginManager
{
	// Installs that are unpacked and waiting for one set of administrator rights.
	//
	// A run that puts six plugins into C:\Program Files\Common Files\VST3 used to ask six times,
	// because every install asked for itself the moment it needed to write. Each one now unpacks as
	// this user, says where its files want to go, and the run asks once for all of them.
	//
	// What is waiting is only ever a copy from a staging folder this user already unpacked, which is
	// what it was before: nothing from the network is fetched or unpacked with more rights than the
	// program was started with.
	class ElevatedBatch
	{
	public:
		// What to write down once a copy has actually been made. Kept beside the copy rather than by
		// the caller, because an install that is only promised must not be recorded as done - that
		// record is what later tells which version is on disk.
		struct Record
		{
			Asset asset;
			std::vector<std::string> files;
		};

		bool empty() const { return m_jobs.empty(); }
		size_t size() const { return m_jobs.size(); }

		void add(ZipExtractor::PendingCopy _copy, Asset _asset, std::vector<std::string> _files)
		{
			m_jobs.push_back(_copy.job);
			m_staging.push_back(std::move(_copy.stagingDir));
			m_records.push_back({std::move(_asset), std::move(_files)});
		}

		const std::vector<ElevatedTask::Job>& getJobs() const { return m_jobs; }
		const std::vector<Record>& getRecords() const { return m_records; }

		// Also takes the staging folders away. They are kept alive while a copy is only promised -
		// they are what it would be made from - so whoever finishes the batch owns them, and a run
		// that is given up on must not leave a plugin's worth of files in the temp folder.
		void clear()
		{
			for (const auto& dir : m_staging)
				juce::File(juce::String(dir)).deleteRecursively();

			m_jobs.clear();
			m_staging.clear();
			m_records.clear();
		}

	private:
		// The two run in step: the nth record belongs to the nth copy, which is how a run that was
		// allowed but half failed still writes down the half that landed.
		std::vector<ElevatedTask::Job> m_jobs;
		std::vector<std::string> m_staging;		// one per job, the folder its files were unpacked into
		std::vector<Record> m_records;
	};
}
