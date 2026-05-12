/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  IChannel<T>

***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNEL
#define VCZH_INTERPROCESS_CHANNEL

#include <Vlpp.h>

namespace vl::inter_process
{

/***********************************************************************
IGuiRemoteProtocolChannel<T>
***********************************************************************/

	template<typename TPackage>
	class IChannelReader : public virtual Interface
	{
	public:
		virtual void							OnRead(const TPackage& package) = 0;
	};

	template<typename TPackage>
	class IChannel : public virtual Interface
	{
	public:
		virtual const WString&					GetChannelName() = 0;
		virtual IChannelReader<TPackage>*		GetReader() = 0;
		virtual void							Initialize(IChannelReader<TPackage>* receiver) = 0;
		virtual void							SendToClient(vint clientId, const TPackage& package) = 0;
		virtual void							BroadcastFromClient(const TPackage& package) = 0;
		virtual void							BatchWrite(bool& disconnected) = 0;
	};

/***********************************************************************
Serialization
***********************************************************************/

	template<typename TFrom, typename TTo>
	class ChannelTransformerBase
		: public Object
		, public virtual IChannel<TTo>
		, protected virtual IChannelReader<TFrom>
	{
	protected:
		IChannel<TTo>*						channel = nullptr;
		IChannelReader<TFrom>*				reader = nullptr;
	public:
		ChannelTransformerBase(IChannel<TTo>* _channel)
			: channel(_channel)
		{
		}

		IChannelReader<TTo>* GetReader() override
		{
			return reader;
		}

		void Initialize(IChannelReader<TFrom>* _reader) override
		{
			reader = _reader;
			channel->Initialize(this);
		}

		void BatchWrite(bool& disconnected) override
		{
			channel->BatchWrite(disconnected);
		}
	};

	template<typename TSerialization>
	class ChannelSerializer
		: public ChannelTransformerBase<typename TSerialization::SourceType, typename TSerialization::DestType>
	{
	protected:
		typename TSerialization::ContextType					context;

		void OnReceive(const typename TSerialization::DestType& package) override
		{
			typename TSerialization::SourceType deserialized;
			TSerialization::Deserialize(context, package, deserialized);
			this->reader->OnRead(deserialized);
		}

	public:
		ChannelSerializer(IChannel<typename TSerialization::DestType>* _channel, const typename TSerialization::ContextType& _context = {})
			: ChannelTransformerBase<typename TSerialization::SourceType, typename TSerialization::DestType>(_channel)
			, context(_context)
		{
		}

		void SendToClient(vint clientId, const typename TSerialization::SourceType& package) override
		{
			typename TSerialization::DestType serialized;
			TSerialization::Serialize(context, package, serialized);
			this->channel->SendToClient(clientId, serialized);
		}

		void BroadcastFromClient(const typename TSerialization::SourceType& package) override
		{
			typename TSerialization::DestType serialized;
			TSerialization::Serialize(context, package, serialized);
			this->channel->BroadcastFromClient(serialized);
		}
	};

/***********************************************************************
String Transformation
***********************************************************************/

	template<typename TFrom, typename TTo>
	struct UtfStringSerializer
	{
		using SourceType = ObjectString<TFrom>;
		using DestType = ObjectString<TTo>;
		using ContextType = std::nullptr_t;

		static void Serialize(const ContextType&, const SourceType& source, DestType& dest)
		{
			ConvertUtfString(source, dest);
		}

		static void Deserialize(const ContextType&, const DestType& source, SourceType& dest)
		{
			ConvertUtfString(source, dest);
		}
	};

	template<typename TFrom, typename TTo>
	using UtfStringChannelSerializer = ChannelSerializer<UtfStringSerializer<TFrom, TTo>>;
}

#endif