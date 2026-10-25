#include "httpClient.h"

#include <cstdio>
#include <cstring>

#include "log.h"

#include "juce_core/juce_core.h"
#include "juce_events/juce_events.h"

namespace pluginManager
{
	namespace
	{
		// The protected channels sit behind plain HTTP basic auth, so the whole of "logging in" is one
		// header. An empty set of credentials sends none and the server answers 401.
		//
		// A User-Agent goes out either way: the GitHub API rejects requests without one.
		//
		// _acceptCompressed is for the catalogs, which are JSON and shrink to a fraction of
		// themselves on the way. Not for the downloads: an archive is compressed already, and a
		// second layer around it would cost time at both ends and save nothing.
		juce::String requestHeaders(const Credentials& _credentials, const bool _acceptCompressed = false)
		{
			juce::String headers = "User-Agent: TUS-Plugin-Manager";

			if (_acceptCompressed)
				headers += "\r\nAccept-Encoding: gzip";

			if (!_credentials.valid())
				return headers;

			const auto plain = juce::String(_credentials.user) + ":" + juce::String(_credentials.password);

			juce::MemoryOutputStream encoded;
			juce::Base64::convertToBase64(encoded, plain.toRawUTF8(), plain.getNumBytesAsUTF8());

			return headers + "\r\nAuthorization: Basic " + encoded.toString();
		}

		// Only a failed connection makes JUCE hand back no stream. An error answer such as 401 or 404
		// still arrives as a readable stream holding the error page from the server, which would pass
		// for a result: a wrong password would look like an empty folder, and a failed download would
		// be saved as the archive.
		bool isHttpError(const int _statusCode)
		{
			return _statusCode >= 400;
		}

		std::string httpErrorMessage(const int _statusCode)
		{
			return "HTTP " + std::to_string(_statusCode);
		}

		// Our host answers 429 Too Many Requests to one address for a few minutes after a long run of
		// downloads. Failing at once failed every install still waiting in that run, one a second, so a
		// request told to slow down waits and asks again - three times, see retryDelaySeconds.
		constexpr int g_tooManyRequests = 429;
		constexpr int g_maxRetries = 3;

		// In quarter seconds, so that cancelling a download that is waiting takes effect at once.
		bool waitUnlessCancelled(const int _seconds, const std::shared_ptr<bool>& _cancel)
		{
			for (int i = 0; i < _seconds * 4; ++i)
			{
				if (_cancel && *_cancel)
					return false;

				juce::Thread::sleep(250);
			}

			return true;
		}

		// Opens _url, waiting and asking again while the server says 429. Not on the message thread,
		// which must not stop for minutes: a request there gets the answer as it is. Null when the
		// connection failed, or when _cancel was set while waiting.
		std::unique_ptr<juce::InputStream> openStream(const std::string& _url, const juce::String& _requestHeaders,
		                                              const int _timeoutMs, int& _statusCode,
		                                              juce::StringPairArray& _responseHeaders,
		                                              const std::shared_ptr<bool>& _cancel = {})
		{
			for (int attempt = 0;; ++attempt)
			{
				_responseHeaders.clear();

				const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
					.withConnectionTimeoutMs(_timeoutMs)
					.withNumRedirectsToFollow(5)
					.withExtraHeaders(_requestHeaders)
					.withResponseHeaders(&_responseHeaders)
					.withStatusCode(&_statusCode);

				auto stream = juce::URL(juce::String(_url)).createInputStream(options);

				if (!stream || _statusCode != g_tooManyRequests || attempt >= g_maxRetries ||
					juce::MessageManager::existsAndIsCurrentThread())
				{
					return stream;
				}

				stream.reset();

				const auto retryAfter = _responseHeaders["Retry-After"].toStdString();
				const auto now = juce::Time::currentTimeMillis() / 1000;
				const auto seconds = HttpClient::retryDelaySeconds(retryAfter, attempt, now);

				Log::write(_url + ": too many requests, asking again in " + std::to_string(seconds) + " s");

				if (!waitUnlessCancelled(seconds, _cancel))
					return nullptr;
			}
		}

		// A local path. Reading a catalog from disk is how the tests drive the library.
		bool isLocalFile(const std::string& _url)
		{
			return juce::String(_url).startsWith("file:///");
		}

		juce::File localFile(const std::string& _url)
		{
			auto path = juce::String(_url).substring(7);		// file://

			// What follows is /home/x on a system whose paths start with a slash and /C:/x on one
			// whose paths do not, where that slash is part of the URL rather than of the path.
			if (!juce::File::isAbsolutePath(path))
				path = path.substring(1);

			const auto file = juce::File(path);

			// A build channel is a folder, and what the server answers for one is its index page. A
			// folder on disk answers with the index page in it, so that a catalog can be read from
			// files exactly as it is read from the server.
			if (file.isDirectory())
				return file.getChildFile("index.html");

			return file;
		}
	}

	bool HttpClient::isAuthError(const std::string& _error)
	{
		return _error == httpErrorMessage(401) || _error == httpErrorMessage(403);
	}

	bool HttpClient::isNotFound(const std::string& _error)
	{
		return _error == httpErrorMessage(404);
	}

	// Told apart by the first two bytes rather than by the Content-Encoding header. A header can
	// be wrong in both directions - something in between may decompress on the way through without
	// taking the header off, and a server may compress without saying so - while the bytes
	// themselves are always what they are. 1f 8b is the gzip magic number.
	std::string HttpClient::decodeBody(const void* _data, const size_t _size)
	{
		if (!_data || !_size)
			return {};

		const auto* bytes = static_cast<const uint8_t*>(_data);

		juce::MemoryInputStream body(_data, _size, false);

		if (_size < 2 || bytes[0] != 0x1f || bytes[1] != 0x8b)
			return body.readEntireStreamAsString().toStdString();

		juce::GZIPDecompressorInputStream gzip(&body, false, juce::GZIPDecompressorInputStream::gzipFormat);

		return gzip.readEntireStreamAsString().toStdString();
	}

	// RFC 1123, which is the one format a server must send: "Mon, 05 Oct 2026 18:37:24 GMT". Always
	// GMT, so no zone to interpret, and turned into seconds here rather than kept as text because
	// these compare as numbers - a string compare would put Tue before Wed in any week.
	bool HttpClient::parseHttpDate(const std::string& _text, int64_t& _seconds)
	{
		static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

		char month[4] = {};
		int day = 0, year = 0, hour = 0, minute = 0, second = 0;

		// The weekday is skipped rather than read: it says nothing the date does not.
		const auto comma = _text.find(',');
		const auto rest = comma == std::string::npos ? _text : _text.substr(comma + 1);

		if (std::sscanf(rest.c_str(), " %d %3s %d %d:%d:%d", &day, month, &year, &hour, &minute, &second) != 6)
			return false;

		int mon = -1;

		for (int i = 0; i < 12; ++i)
		{
			if (std::strcmp(month, months[i]) == 0)
			{
				mon = i + 1;
				break;
			}
		}

		if (mon < 0 || day < 1 || day > 31 || year < 1970)
			return false;

		// Days since 1970-01-01, by the civil-calendar formula - no time zone, no library, no leap
		// table. March is taken as the start of the year, which is what makes the leap day last.
		const int64_t y = year - (mon <= 2 ? 1 : 0);
		const int64_t era = (y >= 0 ? y : y - 399) / 400;
		const int64_t yoe = y - era * 400;
		const int64_t doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
		const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		const int64_t days = era * 146097 + doe - 719468;

		_seconds = days * 86400 + hour * 3600 + minute * 60 + second;

		return true;
	}

	int HttpClient::retryDelaySeconds(const std::string& _retryAfter, const int _attempt, const int64_t _now)
	{
		const auto value = juce::String(_retryAfter).trim();

		int seconds = 0;

		if (value.isNotEmpty() && value.containsOnly("0123456789"))
		{
			seconds = value.getIntValue();
		}
		else
		{
			int64_t at = 0;

			if (parseHttpDate(value.toStdString(), at))
				seconds = static_cast<int>(at - _now);
		}

		if (seconds <= 0)
			seconds = 30 << juce::jlimit(0, 2, _attempt);

		return juce::jlimit(1, 300, seconds);
	}

	bool HttpClient::fetchLastModified(const std::string& _url, int64_t& _seconds, std::string& _error,
	                                   const Credentials& _credentials)
	{
		_seconds = 0;

		// A file on disk is what the tests read, and it answers for itself.
		if (isLocalFile(_url))
		{
			const auto file = localFile(_url);

			if (!file.existsAsFile())
			{
				_error = "File not found: " + file.getFullPathName().toStdString();
				return false;
			}

			_seconds = file.getLastModificationTime().toMilliseconds() / 1000;
			return true;
		}

		int statusCode = 0;
		juce::StringPairArray headers;

		const auto stream = openStream(_url, requestHeaders(_credentials, false), 10000, statusCode, headers);

		if (!stream)
		{
			_error = "Failed to connect";
			Log::write("HEAD " + _url + ": " + _error);
			return false;
		}

		if (isHttpError(statusCode))
		{
			_error = httpErrorMessage(statusCode);
			Log::write("HEAD " + _url + ": " + _error);
			return false;
		}

		const auto value = headers["Last-Modified"].toStdString();

		if (value.empty() || !parseHttpDate(value, _seconds))
		{
			_error = "No Last-Modified";
			return false;
		}

		return true;
	}

	bool HttpClient::fetchString(const std::string& _url, std::string& _result, std::string& _error,
	                             const Credentials& _credentials)
	{
		if (isLocalFile(_url))
		{
			const auto file = localFile(_url);

			if (!file.existsAsFile())
			{
				_error = "File not found: " + file.getFullPathName().toStdString();
				return false;
			}

			_result = file.loadFileAsString().toStdString();
			return true;
		}

		int statusCode = 0;
		juce::StringPairArray headers;

		const auto stream = openStream(_url, requestHeaders(_credentials, true), 10000, statusCode, headers);

		if (!stream)
		{
			_error = "Failed to connect";
			Log::write("GET " + _url + ": " + _error);
			return false;
		}

		if (isHttpError(statusCode))
		{
			_error = httpErrorMessage(statusCode);
			Log::write("GET " + _url + ": " + _error);
			return false;
		}

		// Read as bytes rather than as text, because what arrives may still be compressed.
		juce::MemoryOutputStream body;
		body.writeFromInputStream(*stream, -1);

		_result = decodeBody(body.getData(), body.getDataSize());

		return true;
	}

	namespace
	{
		// One go at a download. _cutOff says the server stopped sending before the length it announced,
		// which is worth one more go - see downloadFile.
		bool downloadOnce(const std::string& _url, const juce::File& _targetFile, std::string& _error,
		                  const HttpClient::ProgressCallback& _onProgress, const std::shared_ptr<bool>& _cancel,
		                  const Credentials& _credentials, bool& _cutOff)
		{
			_cutOff = false;

			int statusCode = 0;
			juce::StringPairArray headers;

			const auto stream = openStream(_url, requestHeaders(_credentials), 15000, statusCode, headers, _cancel);

			if (_cancel && *_cancel)
			{
				_error = "Cancelled";
				return false;
			}

			if (!stream)
			{
				_error = "Failed to connect to server";
				Log::write("Download " + _url + ": " + _error);
				return false;
			}

			if (isHttpError(statusCode))
			{
				_error = httpErrorMessage(statusCode);
				Log::write("Download " + _url + ": " + _error);
				return false;
			}

			// Written beside the target and moved into place at the end, so a cancelled or failed download
			// never leaves something that looks like a finished archive.
			auto tempFile = _targetFile.getSiblingFile(_targetFile.getFileName() + ".downloading");
			tempFile.deleteFile();

			auto outStream = tempFile.createOutputStream();

			if (!outStream)
			{
				_error = "Failed to create output file";
				return false;
			}

			const auto totalLength = stream->getTotalLength();

			constexpr int bufferSize = 65536;
			juce::HeapBlock<char> buffer(bufferSize);

			juce::int64 bytesRead = 0;
			int lastReportedPercent = -1;

			// Said as the system says it. Not checked, a full disk - /tmp is in memory on Fedora - went on
			// to report the archive as damaged, which sends everybody looking in the wrong place (BUG-10507).
			const auto writeFailed = [&]
			{
				_error = "Could not write " + tempFile.getFullPathName().toStdString() + ": " +
					outStream->getStatus().getErrorMessage().toStdString();
				outStream.reset();
				tempFile.deleteFile();
				Log::write("Download " + _url + ": " + _error);
				return false;
			};

			while (!stream->isExhausted())
			{
				if (_cancel && *_cancel)
				{
					outStream.reset();
					tempFile.deleteFile();
					_error = "Cancelled";
					return false;
				}

				const auto numRead = stream->read(buffer, bufferSize);

				if (numRead <= 0)
					break;

				if (!outStream->write(buffer, static_cast<size_t>(numRead)))
					return writeFailed();

				bytesRead += numRead;

				if (!_onProgress)
					continue;

				// A negative value means the size is unknown, which the UI shows as an indeterminate bar.
				const auto progress = totalLength > 0
					? static_cast<float>(bytesRead) / static_cast<float>(totalLength)
					: -1.0f;

				const auto percent = static_cast<int>(progress * 100.0f);

				if (percent == lastReportedPercent)
					continue;

				lastReportedPercent = percent;
				_onProgress(progress);
			}

			// What is still in the stream's buffer fails here, not in write().
			outStream->flush();

			if (outStream->getStatus().failed())
				return writeFailed();

			outStream.reset();

			// Less than the server said it would send: the connection went before the end. The archive would
			// only fail later as damaged, so it is caught here, where trying again is still possible.
			if (totalLength > 0 && bytesRead < totalLength)
			{
				tempFile.deleteFile();
				_error = "The download was cut off";
				_cutOff = true;
				Log::write("Download " + _url + ": " + _error + " after " + std::to_string(bytesRead) + " of "
					+ std::to_string(totalLength) + " bytes");
				return false;
			}

			if (!tempFile.moveFileTo(_targetFile))
			{
				tempFile.deleteFile();
				_error = "Failed to write " + _targetFile.getFullPathName().toStdString();
				return false;
			}

			return true;
		}
	}

	bool HttpClient::downloadFile(const std::string& _url, const std::string& _targetPath, std::string& _error,
	                              const ProgressCallback& _onProgress, const std::shared_ptr<bool>& _cancel,
	                              const Credentials& _credentials)
	{
		const auto targetFile = juce::File(juce::String(_targetPath));

		if (isLocalFile(_url))
		{
			const auto source = localFile(_url);

			if (!source.existsAsFile() || !source.copyFileTo(targetFile))
			{
				_error = "File not found: " + source.getFullPathName().toStdString();
				return false;
			}

			return true;
		}

		// A transfer cut off half way gets one more go: it is what a busy server does now and then, and
		// the second one usually arrives whole.
		for (int attempt = 0;; ++attempt)
		{
			bool cutOff = false;

			if (downloadOnce(_url, targetFile, _error, _onProgress, _cancel, _credentials, cutOff))
			{
				_error.clear();
				return true;
			}

			if (!cutOff || attempt > 0)
				return false;
		}
	}

	void HttpClient::fetchStringAsync(const std::string& _url, StringCallback _callback, const Credentials& _credentials)
	{
		auto callback = std::make_shared<StringCallback>(std::move(_callback));

		juce::Thread::launch([url = _url, callback, credentials = _credentials]
		{
			std::string result;
			std::string error;

			const auto ok = fetchString(url, result, error, credentials);

			juce::MessageManager::callAsync([callback, ok, payload = ok ? result : error]
			{
				(*callback)(ok, payload);
			});
		});
	}

	void HttpClient::downloadFileAsync(const std::string& _url, const std::string& _targetPath,
	                                   ProgressCallback _onProgress, StringCallback _onComplete,
	                                   std::shared_ptr<bool> _cancelToken, const Credentials& _credentials)
	{
		auto onProgress = std::make_shared<ProgressCallback>(std::move(_onProgress));
		auto onComplete = std::make_shared<StringCallback>(std::move(_onComplete));

		juce::Thread::launch([url = _url, targetPath = _targetPath, onProgress, onComplete,
		                      cancel = std::move(_cancelToken), credentials = _credentials]
		{
			// Progress arrives on this thread, so it is handed to the message thread as well.
			ProgressCallback progress;

			if (*onProgress)
			{
				progress = [onProgress](const float _progressValue)
				{
					juce::MessageManager::callAsync([onProgress, _progressValue] { (*onProgress)(_progressValue); });
				};
			}

			std::string error;

			const auto ok = downloadFile(url, targetPath, error, progress, cancel, credentials);

			juce::MessageManager::callAsync([onComplete, ok, error] { (*onComplete)(ok, error); });
		});
	}
}
