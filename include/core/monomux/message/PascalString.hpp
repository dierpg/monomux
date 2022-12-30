/**
 * Copyright (C) 2022 Whisperity
 *
 * SPDX-License-Identifier: GPL-3.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include "monomux/message/MessageBase.hpp"
#include "monomux/system/BufferedChannel.hpp"
#include "monomux/system/Channel.hpp"

namespace monomux::message
{

/// Sends a specific message, fully encoded for transportation, on the
/// \p Channel.
///
/// \note This operation \b MAY block.
template <typename T>
std::size_t sendMessage(system::Channel& Channel, const T& Msg)
{
  return Channel.write(encodeWithSize(Msg));
}

/// Sends a specific message, fully encoded for transportation, on the
/// \p Channel.
///
/// \note This operation \b MAY block.
template <typename T>
std::size_t sendMessage(system::BufferedChannel& Channel, const T& Msg)
{
  return Channel.write(encodeWithSize(Msg));
}

/// Reads a size-prefixed payload from the \p Channel.
///
/// \note This operation \b MAY block.
[[nodiscard]] std::string readPascalString(system::Channel& Channel);

/// Reads a size-prefixed payload from the \p Channel.
///
/// \note This operation \b MAY block.
[[nodiscard]] std::string readPascalString(system::BufferedChannel& Channel);

namespace detail
{

template <typename T>[[nodiscard]] std::optional<T> unpack(std::string&& Data)
{
  Message MsgBase = Message::unpack(Data);
  if (MsgBase.Kind != T::Kind)
    return std::nullopt;

  std::optional<T> Msg = T::decode(MsgBase.RawData);
  return Msg;
}

} // namespace detail

/// Reads a fully encoded message from the \p Channel and expects it to be of
/// message type \p T. If the message successfully read, returns it.
///
/// \note This operation \b MAY block. If the message fails to read, or the
/// message is not the \e expected type, the message \b MAY be dropped and lost.
template <typename T>
[[nodiscard]] std::optional<T> receiveMessage(system::Channel& Channel)
{
  std::string Data = readPascalString(Channel);
  return detail::unpack<T>(std::move(Data));
}

/// Reads a fully encoded message from the \p Channel and expects it to be of
/// message type \p T. If the message successfully read, returns it.
///
/// \note This operation \b MAY block. If the message fails to read, or the
/// message is not the \e expected type, the message \b MAY be dropped and lost.
template <typename T>
[[nodiscard]] std::optional<T> receiveMessage(system::BufferedChannel& Channel)
{
  std::string Data = readPascalString(Channel);
  return detail::unpack<T>(std::move(Data));
}

} // namespace monomux::message
