/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_NETWORKPROTOCOLCHANNEL
#define VCZH_INTERPROCESS_NETWORKPROTOCOLCHANNEL

#include "NetworkProtocol.h"
#include "ChannelImpls/ChannelPackage.h"
#include "ChannelImpls/ChannelImpl.h"
#include "ChannelImpls/ChannelClientBaseImpl.h"
#include "ChannelImpls/ChannelClientImpl.h"
#include "ChannelImpls/LocalChannelClientImpl.h"
#include "ChannelImpls/ChannelServerImpl.h"

/***********************************************************************
Hooking IChannelServer/IChannelClient to INetworkProtocolServer/INetworkProtocolClient

The serialization contract is the same to the one described in ChannelSerialization.h
SourceType will be List<TPackage>
DestType will be WString

NetworkPackage will be used as text message parsing and formatting for INetworkProtocolConnection.
BatchWrite belongs to IChannel, meaning each channel sends its own batch messages in one NetworkPackage.
channelName will be either a system channel or a user defined channel.
messageBody represents a list of TPackage.
The first section is "clientId,extraClientId1,extraClientId2,...".
  Empty clientId means no direct client id.
  Empty or missing extra client ids are equivalent.
When sending from client to server, clientId means the target client.
  Empty means broadcasting.
  When clientId is empty, extraClientIds means blocked receiver client ids.
  When clientId is not empty, extraClientIds are ignored.
When sending from server to client, clientId means the source client.
  Channel messages delivered by the server always carry a source client id.

When a client establishes a connection to the server, channel names will be sent to the server:
  clientId will be empty, it does not mean broadcasting.
  channelName will be empty.
  messageBody will be all available channel names joined by "!", as "!" cannot be part of the channel name anyway.
After the server receives the first message from a client, an client id will be sent to the client:
  clientId is the assigned client id, starting from 1.
  channelName will be empty.
  messageBody will be empty.
***********************************************************************/

#endif
