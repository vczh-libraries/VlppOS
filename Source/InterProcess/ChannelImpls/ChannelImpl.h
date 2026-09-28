/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELIMPL
#define VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELIMPL

#include "../Channel.h"
#include "../../Threading.h"

namespace vl::inter_process
{
/***********************************************************************
NetworkProtocolChannel
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannel : public Object, public virtual IChannel<TPackage>
	{
		static_assert(std::is_same_v<typename TSerialization::SourceType, collections::List<TPackage>>);
		static_assert(std::is_same_v<typename TSerialization::DestType, WString>);
	protected:
		using PackageList = typename TSerialization::SourceType;
		using ClientIdList = collections::List<vint>;
		using ChannelMap = collections::Dictionary<WString, IChannel<TPackage>*>;
		using ChannelNameList = typename ChannelMap::KeyContainer;

		struct UnreadPackage
		{
			vint								senderClientId = -1;
			TPackage							package;
		};

		struct QueuedPackage
		{
			Nullable<vint>						receiverClientId;
			ClientIdList						blockedReceivers;
			TPackage							package;
		};

		WString									channelName;

		// covers reader and unreadPackages
		SpinLock								lockUnreadPackages;
		IChannelReader<TPackage>*				reader = nullptr;
		collections::List<UnreadPackage>		unreadPackages;

		// covers queuedPackages
		SpinLock								lockQueuedPackages;
		collections::List<QueuedPackage>		queuedPackages;

		virtual void ValidatePackage(Nullable<vint> receiverClientId, const ClientIdList& blockedReceivers) = 0;
		virtual bool WriteBatch(Nullable<vint> receiverClientId, const ClientIdList& blockedReceivers, const PackageList& batch) = 0;

	public:
		NetworkProtocolChannel(const WString& _channelName)
			: channelName(_channelName)
		{
		}

		static void ValidateChannelName(const WString& channelName)
		{
			CHECK_ERROR(channelName.Length() > 0, L"Channel name should not be empty.");
			CHECK_ERROR(wcschr(channelName.Buffer(), L'!') == nullptr, L"Channel name should not contain !.");
		}

		static WString JoinChannelNames(const ChannelNameList& channelNames)
		{
			WString joinedNames;
			for (auto&& channelName : channelNames)
			{
				if (joinedNames.Length() > 0)
				{
					joinedNames += L"!";
				}
				joinedNames += channelName;
			}
			return joinedNames;
		}

		static void SplitChannelNames(const WString& joinedNames, ChannelMap& availableChannels)
		{
			const wchar_t* reading = joinedNames.Buffer();
			while (true)
			{
				auto delimiter = wcschr(reading, L'!');
				auto channelName =
					delimiter
					? WString::CopyFrom(reading, (vint)(delimiter - reading))
					: WString::CopyFrom(reading, (vint)wcslen(reading));
				if (channelName.Length() > 0)
				{
					ValidateChannelName(channelName);
					availableChannels.Add(channelName, nullptr);
				}

				if (!delimiter)
				{
					break;
				}
				reading = delimiter + 1;
			}
		}

		const WString& GetChannelName() override
		{
			return channelName;
		}

		IChannelReader<TPackage>* GetReader() override
		{
			return reader;
		}

		void Initialize(IChannelReader<TPackage>* _reader) override
		{
			CHECK_ERROR(_reader, L"NetworkProtocolChannel::Initialize needs a valid reader.");
			SPIN_LOCK(lockUnreadPackages)
			{
				CHECK_ERROR(!reader, L"NetworkProtocolChannel::Initialize cannot be called more than once.");
				reader = _reader;
				for (auto&& package : unreadPackages)
				{
					reader->OnRead(package.senderClientId, package.package);
				}
				unreadPackages.Clear();
			}
		}

		void SendToClient(vint receiverClientId, const TPackage& package) override
		{
			ClientIdList blockedReceivers;
			ValidatePackage(receiverClientId, blockedReceivers);
			QueuePackage(receiverClientId, blockedReceivers, package);
		}

		void BroadcastFromClient(const TPackage& package) override
		{
			ClientIdList blockedReceivers;
			BroadcastFromClient(package, blockedReceivers);
		}

		void BroadcastFromClient(const TPackage& package, const ClientIdList& blockedReceivers) override
		{
			ValidatePackage({}, blockedReceivers);
			QueuePackage({}, blockedReceivers, package);
		}

		void BatchWrite(bool& disconnected) override
		{
			collections::List<QueuedPackage> packages;
			SPIN_LOCK(lockQueuedPackages)
			{
				packages = std::move(queuedPackages);
			}

			while (packages.Count() > 0)
			{
				PackageList batch;
				auto receiverClientId = packages[0].receiverClientId;
				ClientIdList blockedReceivers = std::move(packages[0].blockedReceivers);
				batch.Add(packages[0].package);
				packages.RemoveAt(0);
				for (vint i = 0; i < packages.Count();)
				{
					auto&& package = packages[i];
					if (package.receiverClientId == receiverClientId && ClientIdsEqual(package.blockedReceivers, blockedReceivers))
					{
						batch.Add(package.package);
						packages.RemoveAt(i);
					}
					else
					{
						i++;
					}
				}

				if (WriteBatch(receiverClientId, blockedReceivers, batch))
				{
					disconnected = true;
					return;
				}
			}
		}

		void ReadBatch(vint senderClientId, const PackageList& batch)
		{
			SPIN_LOCK(lockUnreadPackages)
			{
				if (!reader)
				{
					for (auto&& package : batch)
					{
						UnreadPackage unreadPackage;
						unreadPackage.senderClientId = senderClientId;
						unreadPackage.package = package;
						unreadPackages.Add(unreadPackage);
					}
					return;
				}
			}
			for (auto&& package : batch)
			{
				reader->OnRead(senderClientId, package);
			}
		}

	protected:
		static bool ClientIdsEqual(const ClientIdList& a, const ClientIdList& b)
		{
			if (a.Count() != b.Count())
			{
				return false;
			}
			for (vint i = 0; i < a.Count(); i++)
			{
				if (a[i] != b[i])
				{
					return false;
				}
			}
			return true;
		}

		void QueuePackage(Nullable<vint> receiverClientId, const ClientIdList& blockedReceivers, const TPackage& package)
		{
			SPIN_LOCK(lockQueuedPackages)
			{
				QueuedPackage queuedPackage;
				queuedPackage.receiverClientId = receiverClientId;
				for (auto blockedReceiver : blockedReceivers)
				{
					queuedPackage.blockedReceivers.Add(blockedReceiver);
				}
				queuedPackage.package = package;
				queuedPackages.Add(std::move(queuedPackage));
			}
		}
	};

}

#endif
