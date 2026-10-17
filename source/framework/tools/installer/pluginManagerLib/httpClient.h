#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "credentials.h"

namespace pluginManager
{
	// HTTP for the plugin manager.
	//
	// The blocking calls are the real ones, so the library works without a message loop - which is what
	// lets the headless program and the tests use it. The async wrappers are for the GUI, which must not
	// block its message thread: they run the blocking call on a background thread and answer on the
	// message thread, so they need a running JUCE message loop.
	class HttpClient
	{
	public:
		using StringCallback = std::function<void(bool _success, const std::string& _result)>;
		using ProgressCallback = std::function<void(float _progress)>;

		// A request the server answered with an error fails with "HTTP <status>". These tell a rejected
		// login (401, 403) and a folder that is not there (404) apart from any other failure, such as a
		// server that could not be reached at all.
		static bool isAuthError(const std::string& _error);
		static bool isNotFound(const std::string& _error);

		static bool fetchString(const std::string& _url, std::string& _result, std::string& _error,
		                        const Credentials& _credentials = {});

		// When the file behind this URL last changed, in seconds since 1970 UTC - what Last-Modified
		// answers, which is the same clock everywhere and so comparable with a value stored earlier.
		// The body is never read: the headers are all this is for.
		//
		// Fails when the server does not send the header, which is not an error worth showing anybody -
		// the caller carries on with what the listing said.
		static bool fetchLastModified(const std::string& _url, int64_t& _seconds, std::string& _error,
		                              const Credentials& _credentials = {});

		// "Mon, 05 Oct 2026 18:37:24 GMT" as seconds since 1970. Public because it is the part of the
		// above worth testing without a server: a date read wrongly would hide every update there is.
		static bool parseHttpDate(const std::string& _text, int64_t& _seconds);

		// How long to wait before asking again after a 429 Too Many Requests: what Retry-After says, in
		// seconds or as a date, else 30, 60 and 120 seconds for the first three attempts. Never more than
		// five minutes. Public for the same reason as parseHttpDate: it is the part worth a test without
		// a server, and _now is passed in for it.
		static int retryDelaySeconds(const std::string& _retryAfter, int _attempt, int64_t _now);

		// A body as it arrived, which is gzip when the server took up the offer the request made and
		// plain when it did not. Public because it is worth a test of its own: the catalogs are JSON
		// and a fraction of their size compressed, and getting this wrong would be an empty window.
		static std::string decodeBody(const void* _data, size_t _size);

		// _onProgress is called on the calling thread and may be empty. Setting *_cancel stops the
		// transfer, which then fails with "Cancelled".
		static bool downloadFile(const std::string& _url, const std::string& _targetPath, std::string& _error,
		                         const ProgressCallback& _onProgress = {},
		                         const std::shared_ptr<bool>& _cancel = {},
		                         const Credentials& _credentials = {});

		static void fetchStringAsync(const std::string& _url, StringCallback _callback,
		                             const Credentials& _credentials = {});

		static void downloadFileAsync(const std::string& _url, const std::string& _targetPath,
		                              ProgressCallback _onProgress, StringCallback _onComplete,
		                              std::shared_ptr<bool> _cancelToken, const Credentials& _credentials = {});
	};
}
