#include "ChannelPackage.h"

namespace vl::inter_process
{
	NetworkPackage NetworkPackage::Create(Nullable<vint> _clientId, const WString& _channelName, const WString& _messageBody)
	{
		NetworkPackage package;
		package.clientId = std::move(_clientId);
		package.channelName = _channelName;
		package.messageBody = _messageBody;
		return package;
	}

	NetworkPackage NetworkPackage::Create(Nullable<vint> _clientId, const ClientIdList& _extraClientIds, const WString& _channelName, const WString& _messageBody)
	{
		auto package = Create(std::move(_clientId), _channelName, _messageBody);
		if (_extraClientIds.Count() > 0)
		{
			ClientIdList extraClientIds;
			for (auto clientId : _extraClientIds)
			{
				extraClientIds.Add(clientId);
			}
			package.extraClientIds = std::move(extraClientIds);
		}
		return package;
	}

	WString NetworkPackage::ToString(const NetworkPackage& package)
	{
		auto clientIds = package.clientId ? itow(package.clientId.Value()) : WString::Empty;
		if (package.extraClientIds)
		{
			for (auto clientId : package.extraClientIds.Value())
			{
				clientIds += L",";
				clientIds += itow(clientId);
			}
		}

		return clientIds
			+ L";" + package.channelName
			+ L";" + package.messageBody
			;
	}

	void NetworkPackage::Parse(const WString& str, NetworkPackage& package)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::NetworkPackage::Parse(const WString&, NetworkPackage&)#"
		const wchar_t* reading = str.Buffer();

		const wchar_t* afterClientId = wcschr(reading, L';');
		CHECK_ERROR(afterClientId != nullptr, ERROR_MESSAGE_PREFIX L"Invalid package format.");
		package.clientId.Reset();
		package.extraClientIds.Reset();
		const wchar_t* firstExtraClientId = wcschr(reading, L',');
		if (firstExtraClientId && firstExtraClientId > afterClientId)
		{
			firstExtraClientId = nullptr;
		}
		auto clientIdLength = firstExtraClientId ? (vint)(firstExtraClientId - reading) : (vint)(afterClientId - reading);
		if (clientIdLength > 0)
		{
			package.clientId = wtoi(str.Left(clientIdLength));
		}

		if (firstExtraClientId)
		{
			ClientIdList extraClientIds;
			auto readingExtraClientId = firstExtraClientId + 1;
			while (readingExtraClientId < afterClientId)
			{
				auto delimiter = wcschr(readingExtraClientId, L',');
				if (delimiter && delimiter > afterClientId)
				{
					delimiter = nullptr;
				}
				auto endingExtraClientId = delimiter ? delimiter : afterClientId;
				CHECK_ERROR(endingExtraClientId > readingExtraClientId, ERROR_MESSAGE_PREFIX L"Invalid extra client id format.");
				extraClientIds.Add(wtoi(WString::CopyFrom(readingExtraClientId, (vint)(endingExtraClientId - readingExtraClientId))));
				readingExtraClientId = endingExtraClientId + (delimiter ? 1 : 0);
			}
			if (extraClientIds.Count() > 0)
			{
				package.extraClientIds = std::move(extraClientIds);
			}
		}

		const wchar_t* afterChannelName = wcschr(afterClientId + 1, L';');
		CHECK_ERROR(afterChannelName != nullptr, ERROR_MESSAGE_PREFIX L"Invalid package format.");
		package.channelName = str.Sub((vint)(afterClientId - reading + 1), (vint)(afterChannelName - afterClientId - 1));
		package.messageBody = str.Right(str.Length() - (vint)(afterChannelName - reading + 1));
#undef ERROR_MESSAGE_PREFIX
	}
}
