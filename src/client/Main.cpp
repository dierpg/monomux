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
#include "Main.hpp"

#include "Client.hpp"
#include "Terminal.hpp"
#include "server/Server.hpp" // FIXME: Do not depend on this.

#include <chrono>
#include <iostream>
#include <thread>

#include <termios.h>

namespace monomux
{
namespace client
{

std::optional<Client> connect(const Options& Opts, bool Block)
{
  auto C = Client::create(server::Server::getServerSocketPath());
  if (!Block)
    return C;

  unsigned short HandshakeCounter = 1;
  while (!C)
  {
    ++HandshakeCounter;
    C = Client::create(server::Server::getServerSocketPath());
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (HandshakeCounter == 5)
    {
      std::cerr << "Connection failed after enough retries." << std::endl;
      return std::nullopt;
    }
  }

  return C;
}

int main(Options& Opts)
{
  // For the convenience of auto-starting a server if none exists, the creation
  // of the Client itself is placed into the global entry point.
  if (!Opts.Connection.has_value())
  {
    std::cerr << "ERROR: Attempted to start client without active connection."
              << std::endl;
    return EXIT_FAILURE;
  }
  Client& Client = *Opts.Connection;

  while (!Client.handshake())
  {
    std::clog << "DEBUG: Trying to authenticate with server again..."
              << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  Process::SpawnOptions SO;
  SO.Program = "/bin/bash";
  SO.Environment["MONOMUX_UNSET"] = std::nullopt;
  SO.Environment["MONOMUX_SET"] = "TEST";
  Opts.Connection->requestMakeSession("???", SO);

  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);

  termios Mode;
  raw_fd TTY = ::open("/dev/tty", O_RDWR);
  if (tcgetattr(TTY, &Mode) < 0)
    return EXIT_FAILURE;
  termios NewMode = Mode;
  NewMode.c_lflag &= ~(ICANON | ECHO);

  // TODO: Do we need all these flags, really?
  NewMode.c_iflag &= ~IXON;
  NewMode.c_iflag &= ~IXOFF;
  NewMode.c_iflag &= ~ICRNL;
  NewMode.c_iflag &= ~INLCR;
  NewMode.c_iflag &= ~IGNCR;
  NewMode.c_iflag &= ~IMAXBEL;
  NewMode.c_iflag &= ~ISTRIP;

  NewMode.c_oflag &= ~OPOST;
  NewMode.c_oflag &= ~ONLCR;
  NewMode.c_oflag &= ~OCRNL;
  NewMode.c_oflag &= ~ONLRET;

  if (tcsetattr(TTY, TCSANOW, &NewMode) < 0)
    return EXIT_FAILURE;

  {
    Terminal Term{fd::fileno(stdin), fd::fileno(stdout)};
    Client.setTerminal(std::move(Term));
  }

  Client.loop();

  return EXIT_SUCCESS;
}

} // namespace client
} // namespace monomux
