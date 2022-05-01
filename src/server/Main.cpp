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
#include <thread>

#include "monomux/adt/ScopeGuard.hpp"
#include "monomux/server/Server.hpp"
#include "monomux/system/Environment.hpp"
#include "monomux/system/Process.hpp"
#include "monomux/system/Signal.hpp"
#include "monomux/unreachable.hpp"

#include "ExitCode.hpp"
#include "monomux/server/Main.hpp"

#include "monomux/Log.hpp"
#define LOG(SEVERITY) monomux::log::SEVERITY("server/Main")

namespace monomux::server
{

Options::Options()
  : ServerMode(false), Background(true), ExitOnLastSessionTerminate(true)
{}

std::vector<std::string> Options::toArgv() const
{
  std::vector<std::string> Ret;

  if (ServerMode)
    Ret.emplace_back("--server");
  if (SocketPath.has_value())
  {
    Ret.emplace_back("--socket");
    Ret.emplace_back(*SocketPath);
  }
  if (!Background)
    Ret.emplace_back("--no-daemon");
  if (!ExitOnLastSessionTerminate)
    Ret.emplace_back("--keepalive");

  return Ret;
}

[[noreturn]] void exec(const Options& Opts, const char* ArgV0)
{
  MONOMUX_TRACE_LOG(LOG(trace) << "exec() a new server");

  Process::SpawnOptions SO;
  SO.Program = ArgV0;
  SO.Arguments = Opts.toArgv();

  Process::exec(SO);
  unreachable("[[noreturn]]");
}

static constexpr char ServerObjName[] = "Server";
static constexpr char MasterAborterName[] = "Master-Aborter";

/// Handler for request to terinate the server.
static void serverShutdown(SignalHandling::Signal /* SigNum */,
                           ::siginfo_t* /* Info */,
                           const SignalHandling* Handling)
{
  const volatile auto* Srv =
    std::any_cast<Server*>(Handling->getObject(ServerObjName));
  if (!Srv)
    return;
  (*Srv)->interrupt();
}

/// Handler for \p SIGCHLD when a process spawned by the server quits.
static void childExited(SignalHandling::Signal /* SigNum */,
                        ::siginfo_t* Info,
                        const SignalHandling* Handling)
{
  Process::raw_handle CPID = Info->si_pid;
  const volatile auto* Srv =
    std::any_cast<Server*>(Handling->getObject(ServerObjName));
  if (!Srv)
    return;
  (*Srv)->registerDeadChild(CPID);
}

/// Custom handler for \p SIGABRT. This is even more custom than the handler in
/// the global \p main() as it deals with killing the server first.
static void coreDumped(SignalHandling::Signal SigNum,
                       ::siginfo_t* Info,
                       const SignalHandling* Handling)
{
  serverShutdown(SigNum, Info, Handling);

  // Fallback to the master handler that main.cpp should've installed.
  const auto* MasterAborter =
    std::any_cast<std::function<SignalHandling::SignalCallback>>(
      Handling->getObject(MasterAborterName));
  if (MasterAborter)
    (*MasterAborter)(SigNum, Info, Handling);
  else
  {
    LOG(fatal) << "In Server, " << SignalHandling::signalName(SigNum)
               << " FATAL SIGNAL received, but local handler did not find the "
                  "appropriate master one.";
  }
}

int main(Options& Opts)
{
  if (Opts.Background)
    CheckedPOSIXThrow(
      [] { return ::daemon(0, 0); }, "Backgrounding ourselves failed", -1);

  Socket ServerSock = Socket::create(*Opts.SocketPath);
  Server S = Server(std::move(ServerSock));
  S.setExitIfNoMoreSessions(Opts.ExitOnLastSessionTerminate);
  ScopeGuard Signal{
    [&S] {
      SignalHandling& Sig = SignalHandling::get();
      Sig.registerObject(SignalHandling::ModuleObjName, "Server");
      Sig.registerObject(ServerObjName, &S);
      Sig.registerCallback(SIGINT, &serverShutdown);
      Sig.registerCallback(SIGTERM, &serverShutdown);
      Sig.registerCallback(SIGCHLD, &childExited);
      Sig.ignore(SIGPIPE);

      // Override the SIGABRT handler with a custom one that
      // resets the terminal during a crash.
      Sig.registerObject(MasterAborterName, Sig.getCallback(SIGABRT));
      Sig.registerCallback(SIGILL, &coreDumped);
      Sig.registerCallback(SIGABRT, &coreDumped);
      Sig.registerCallback(SIGSEGV, &coreDumped);
      Sig.registerCallback(SIGSYS, &coreDumped);
      Sig.registerCallback(SIGSTKFLT, &coreDumped);
      Sig.enable();
      Sig.enable();
    },
    [] {
      SignalHandling& Sig = SignalHandling::get();
      Sig.unignore(SIGPIPE);
      Sig.defaultCallback(SIGCHLD);
      Sig.defaultCallback(SIGTERM);
      Sig.defaultCallback(SIGINT);
      Sig.deleteObject(ServerObjName);

      auto MasterAborter =
        *std::any_cast<std::function<SignalHandling::SignalCallback>>(
          Sig.getObject(MasterAborterName));
      Sig.registerCallback(SIGILL, MasterAborter);
      Sig.registerCallback(SIGABRT, MasterAborter);
      Sig.registerCallback(SIGSEGV, MasterAborter);
      Sig.registerCallback(SIGSYS, MasterAborter);
      Sig.registerCallback(SIGSTKFLT, MasterAborter);
      Sig.deleteObject(MasterAborterName);
    }};

  LOG(info) << "Starting Monomux Server";
  ScopeGuard Server{[&S] { S.loop(); }, [&S] { S.shutdown(); }};
  LOG(info) << "Monomux Server stopped";
  return EXIT_Success;
}

} // namespace monomux::server
