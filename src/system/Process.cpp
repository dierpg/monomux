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
#include "Process.hpp"

#include "CheckedPOSIX.hpp"
#include "Pty.hpp"
#include "Signal.hpp"
#include "unreachable.hpp"

#include <cstring>

#include <sys/wait.h>

namespace monomux
{

static void allocCopyString(const std::string& Source,
                            // NOLINTNEXTLINE(modernize-avoid-c-arrays)
                            char* DestinationStringArray[],
                            std::size_t Index)
{
  DestinationStringArray[Index] =
    reinterpret_cast<char*>(std::calloc(Source.size() + 1, 1));
  std::strncpy(
    DestinationStringArray[Index], Source.c_str(), Source.size() + 1);
}

[[noreturn]] void Process::exec(const SpawnOptions& Opts)
{
  std::clog << "----- Process::exec() was called -----\n\n";

  char** NewArgv = new char*[Opts.Arguments.size() + 2];
  allocCopyString(Opts.Program, NewArgv, 0);
  std::clog << "        Program:    " << Opts.Program << '\n';
  NewArgv[Opts.Arguments.size() + 1] = nullptr;
  for (std::size_t I = 0; I < Opts.Arguments.size(); ++I)
  {
    allocCopyString(Opts.Arguments[I], NewArgv, I + 1);
    std::clog << "        Arg " << I << ":    " << Opts.Arguments[I] << '\n';
  }

  for (const auto& E : Opts.Environment)
  {
    if (!E.second.has_value())
    {
      std::clog << "        Env var:    " << E.first << " unset!" << '\n';
      CheckedPOSIX([&K = E.first] { return ::unsetenv(K.c_str()); }, -1);
    }
    else
    {
      std::clog << "        Env var:    " << E.first << '=' << *E.second
                << '\n';
      CheckedPOSIX(
        [&K = E.first, &V = E.second] {
          return ::setenv(K.c_str(), V->c_str(), 1);
        },
        -1);
    }
  }

  std::clog << "----- Process::exec() firing -----" << std::endl;

  CheckedPOSIXThrow([NewArgv] { return ::execvp(NewArgv[0], NewArgv); },
                    "Executing process failed",
                    -1);
  unreachable("::exec() should've started a new process");
}

Process Process::spawn(const SpawnOptions& Opts)
{
  std::optional<Pty> PTY;
  if (Opts.CreatePTY)
    PTY.emplace(Pty{});

  raw_handle ForkResult =
    CheckedPOSIXThrow([] { return ::fork(); }, "fork() failed in spawn()", -1);
  if (ForkResult != 0)
  {
    // We are in the parent.
    Process P;
    P.Handle = ForkResult;

    if (PTY)
    {
      PTY->setupParentSide();
      P.PTY = std::move(PTY);
    }

    return P;
  }

  // We are in the child.
  SignalHandling::get().reset();
  CheckedPOSIXThrow([] { return ::setsid(); }, "setsid()", -1);
  if (PTY)
    PTY->setupChildrenSide();

  Process::exec(Opts);
  unreachable("Process::exec() should've replaced the process.");
}

bool Process::reapIfDead()
{
  if (Handle == Invalid)
    return true;

  auto ChangedPID =
    CheckedPOSIX([this] { return ::waitpid(Handle, nullptr, WNOHANG); }, -1);
  if (!ChangedPID)
  {
    std::error_code EC = ChangedPID.getError();
    if (EC == std::errc::no_child_process)
      return false;
    throw std::system_error{EC, "waitpid(" + std::to_string(Handle) + ")"};
  }

  if (ChangedPID.get() == Handle)
    return true;
  if (ChangedPID.get() == 0)
    return false;

  return false;
}

} // namespace monomux
