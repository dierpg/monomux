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
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <system_error>
#include <thread>

#include <getopt.h>
#include <unistd.h>

#include "monomux/CheckedErrno.hpp"
#include "monomux/FrontendExitCode.hpp"
#include "monomux/Version.hpp"
#include "monomux/client/Main.hpp"
#include "monomux/server/Main.hpp"
#include "monomux/system/Backtrace.hpp"
#include "monomux/system/Environment.hpp"
#include "monomux/system/Process.hpp"
#include "monomux/system/SignalHandling.hpp"
#include "monomux/system/UnixBacktrace.hpp"
#include "monomux/system/UnixProcess.hpp"

#include "Config.hpp"

#include "monomux/Log.hpp"
#define LOG(SEVERITY) monomux::log::SEVERITY("main")

namespace
{

const char ShortOptions[] = "hvqVs:e:u:n:lidDNk";

// clang-format off
struct ::option LongOptions[] = {
  {"help",        no_argument,       nullptr, 'h'},
  {"verbose",     no_argument,       nullptr, 'v'},
  {"quiet",       no_argument,       nullptr, 'q'},
  {"server",      no_argument,       nullptr, 0},
  {"socket",      required_argument, nullptr, 's'},
  {"env",         required_argument, nullptr, 'e'},
  {"unset",       required_argument, nullptr, 'u'},
  {"name",        required_argument, nullptr, 'n'},
  {"list",        no_argument,       nullptr, 'l'},
  {"interactive", no_argument,       nullptr, 'i'},
  {"detach",      no_argument,       nullptr, 'd'},
  {"detach-all",  no_argument,       nullptr, 'D'},
  {"statistics",  no_argument,       nullptr, 0},
  {"no-daemon",   no_argument,       nullptr, 'N'},
  {"keepalive",   no_argument,       nullptr, 'k'},
  {nullptr,       0,                 nullptr, 0}
};
// clang-format on

struct MainOptions
{
  /// \p -h
  bool ShowHelp : 1;

  /// \p -V
  bool ShowVersion : 1;

  /// \p -V a second time
  bool ShowElaborateBuildInformation : 1;

  /// \p -v
  bool AnyVerboseFlag : 1;
  /// \p -q
  bool AnyQuietFlag : 1;

  /// \p -v or \p -q sequences
  std::int8_t VerbosityQuietnessDifferential = 0;

  /// \p -v and \p -q translated to \p Severity choice.
  monomux::log::Severity Severity;
};

void printHelp();
void printVersion();
void printFeatures();
// FIXME: Ifdef UNIX...
void coreDumped(monomux::system::SignalHandling::Signal SigNum,
                ::siginfo_t* Info,
                const monomux::system::SignalHandling* Handling);

} // namespace

int main(int ArgC, char* ArgV[])
{
  using namespace monomux;
  using namespace monomux::system;

  server::Options ServerOpts{};
  client::Options ClientOpts{};

  // ------------------------ Parse command-line options -----------------------
  {
    MainOptions MainOpts{};
    bool HadErrors = false;
    auto ArgError = [&HadErrors, Prog = ArgV[0]]() -> std::ostream& {
      std::cerr << Prog << ": ";
      HadErrors = true;
      return std::cerr;
    };

    int Opt;
    int LongOptIndex;
    while ((Opt = ::getopt_long(
              ArgC, ArgV, ShortOptions, LongOptions, &LongOptIndex)) != -1)
    {
      switch (Opt)
      {
        case 0:
        {
          // Long-option (that has no short option version) was specified.
          std::string_view Opt = LongOptions[LongOptIndex].name;
          if (Opt == "server")
          {
            ServerOpts.ServerMode = true;
            ClientOpts.ClientMode = false;
          }
          else if (Opt == "statistics")
          {
            ClientOpts.StatisticsRequest = true;
          }
          else
          {
            ArgError() << "option '--" << Opt
                       << "' registered, but no handler associated with it\n";
            break;
          }
          break;
        }
        case '?':
          HadErrors = true;
          break;
        case 'h':
          MainOpts.ShowHelp = true;
          break;
        case 'v':
          if (MainOpts.AnyQuietFlag)
          {
            ArgError() << "option '-v/--verbose' meaningless if '-q/--quiet' "
                          "was also supplied\n";
            break;
          }
          MainOpts.AnyVerboseFlag = true;
          ++MainOpts.VerbosityQuietnessDifferential;
          break;
        case 'q':
          if (MainOpts.AnyVerboseFlag)
          {
            ArgError() << "option '-q/--quiet' meaningless if '-v/--verbose' "
                          "was also supplied\n";
            break;
          }
          MainOpts.AnyQuietFlag = true;
          --MainOpts.VerbosityQuietnessDifferential;
          break;
        case 'V':
          if (!MainOpts.ShowVersion)
          {
            MainOpts.ShowVersion = true;
            break;
          }
          if (!MainOpts.ShowElaborateBuildInformation)
          {
            MainOpts.ShowElaborateBuildInformation = true;
            break;
          }
          ArgError() << "option '-V' cannot be repeated this many times\n";
          break;
        case 's':
          ClientOpts.SocketPath.emplace(optarg);
          break;
        case 'n':
          ClientOpts.SessionName.emplace(optarg);
          break;
        case 'e':
        {
          std::string_view::size_type EqualLoc =
            std::string_view{optarg}.find('=');
          if (EqualLoc == std::string_view::npos)
          {
            ArgError() << "option '-e/--env' must be specified in the format "
                          "'VAR=VAL'\n";
            break;
          }
          if (!ClientOpts.Program)
            ClientOpts.Program.emplace(Process::SpawnOptions{});
          ClientOpts.Program->Environment.emplace(
            std::string{optarg, EqualLoc}, std::string{optarg + EqualLoc + 1});
          break;
        }
        case 'u':
          if (!ClientOpts.Program)
            ClientOpts.Program.emplace(Process::SpawnOptions{});
          ClientOpts.Program->Environment.emplace(optarg, std::nullopt);
          break;
        case 'l':
          ClientOpts.OnlyListSessions = true;
          break;
        case 'i':
          ClientOpts.InteractiveSessionMenu = true;
          break;
        case 'd':
          ClientOpts.DetachRequestLatest = true;
          break;
        case 'D':
          ClientOpts.DetachRequestAll = true;
          break;
        case 'N':
          ServerOpts.Background = false;
          ServerOpts.ExitOnLastSessionTerminate = false;
          break;
        case 'k':
          ServerOpts.ExitOnLastSessionTerminate = false;
          break;
        default:
          std::cerr << ArgV[0] << ": "
                    << "option '-" << static_cast<char>(Opt)
                    << "' is registered to be accepted, but the associated "
                       "handler is not found\n\tThe flag will be "
                       "ignored! Please report this as a bug!\n";
          break;
      }
    }

    if (MainOpts.ShowHelp)
    {
      printHelp();
      return static_cast<int>(FrontendExitCode::Success);
    }
    if (MainOpts.ShowVersion)
    {
      printVersion();
      if (MainOpts.ShowElaborateBuildInformation)
        printFeatures();
      return static_cast<int>(FrontendExitCode::Success);
    }

    {
      using namespace monomux::log;

#ifdef MONOMUX_NON_ESSENTIAL_LOGS
      std::size_t VerbosityPositiveSizeT =
        std::abs(MainOpts.VerbosityQuietnessDifferential);
#endif
      if (MainOpts.VerbosityQuietnessDifferential > MaximumVerbosity)
      {
        MONOMUX_TRACE_LOG(
          std::cerr << "Warning: Requested logging verbosity '-"
                    << (std::string(VerbosityPositiveSizeT, 'v'))
                    << "' larger than possible, clamping to available maximum."
                    << std::endl);
        MainOpts.VerbosityQuietnessDifferential = MaximumVerbosity;
      }
      else if (MainOpts.VerbosityQuietnessDifferential < -MinimumVerbosity)
      {
        MONOMUX_TRACE_LOG(
          std::cerr << "Warning: Requested logging verbosity '-"
                    << (std::string(VerbosityPositiveSizeT, 'q'))
                    << "' larger than possible, clamping to available maximum."
                    << std::endl);
        MainOpts.VerbosityQuietnessDifferential = -MinimumVerbosity;
      }
      MainOpts.Severity = static_cast<Severity>(
        Default + MainOpts.VerbosityQuietnessDifferential);
    }

    if (ClientOpts.DetachRequestLatest && ClientOpts.DetachRequestAll)
      ArgError() << "option '-D/--detach-all' and '-d/--detach' are mutually "
                    "exclusive!\n";

    if (!ServerOpts.ServerMode)
      ClientOpts.ClientMode = true;

    // Handle positional arguments not handled earlier.
    for (; ::optind < ArgC; ++::optind)
    {
      if (ServerOpts.ServerMode)
      {
        ArgError() << "option '--server' does not take positional argument \""
                   << ArgV[::optind] << "\"\n";
        break;
      }

      assert(ClientOpts.ClientMode);
      if (!ClientOpts.Program)
        ClientOpts.Program.emplace(Process::SpawnOptions{});
      if (ClientOpts.Program->Program.empty())
        // The first positional argument is the program name to spawn.
        ClientOpts.Program->Program = ArgV[::optind];
      else
        // Otherwise they are arguments to the program to start.
        ClientOpts.Program->Arguments.emplace_back(ArgV[::optind]);
    }

    if (HadErrors)
      return static_cast<int>(FrontendExitCode::InvocationError);

    log::Logger::get().setLimit(MainOpts.Severity);
  }

  // ------------------- Initialise the core helper libraries ------------------
  {
    SignalHandling& Sig = SignalHandling::get();
    Sig.registerCallback(SIGILL, &coreDumped);
    Sig.registerCallback(SIGABRT, &coreDumped);
    Sig.registerCallback(SIGSEGV, &coreDumped);
    Sig.registerCallback(SIGSYS, &coreDumped);
    Sig.registerCallback(SIGSTKFLT, &coreDumped);
    Sig.registerObject(SignalHandling::ModuleObjName, "main");
    Sig.enable();
  }

  // --------------------- Set up some internal environment --------------------
  {
    if (ClientOpts.isControlMode() && !ClientOpts.SocketPath)
    {
      // Load a session from the current process's environment, to have a socket
      // for the controller client ready, if needed.
      std::optional<MonomuxSession> Sess = MonomuxSession::loadFromEnv();
      if (Sess)
      {
        ClientOpts.SocketPath = Sess->Socket.to_string();
        ClientOpts.SessionData = std::move(Sess);
      }
    }

    Platform::SocketPath SocketPath =
      ClientOpts.SocketPath.has_value()
        ? Platform::SocketPath::absolutise(*ClientOpts.SocketPath)
        : Platform::SocketPath::defaultSocketPath();
    ClientOpts.SocketPath = SocketPath.to_string();
    ServerOpts.SocketPath = ClientOpts.SocketPath;

    LOG(debug) << "Using socket: \"" << *ClientOpts.SocketPath << '"';
  }

  // --------------------- Dispatch to appropriate handler ---------------------
  if (ServerOpts.ServerMode)
    return static_cast<int>(server::main(ServerOpts));

  // The default behaviour in the client is to always try establishing a
  // connection to a server. However, it is very likely that the current process
  // has been the first monomux instance created by the user, in which case
  // there will be no server running. For convenience, we can initialise a
  // server right here.
  {
    std::string FailureReason;
    std::optional<client::Client> ToServer;
    try
    {
      ToServer = client::connect(ClientOpts, false, &FailureReason);
    }
    catch (...)
    {}

    if (!ToServer && !ClientOpts.isControlMode())
    {
      LOG(info) << "No running server found, starting one automatically...";
      ServerOpts.ServerMode = true;
      // FIXME: Ifdef UNIX...
      unix::Process::fork([] { /* Parent: noop. */ },
                          [&ServerOpts, &ArgV] {
                            // Perform the server restart in the child,
                            // so it gets disowned when we eventually
                            // exit, and we can remain the client.
                            server::exec(ServerOpts, ArgV[0]);
                          });

      // Give some time for the server to spawn...
      std::this_thread::sleep_for(std::chrono::seconds(1));

      try
      {
        ToServer = client::connect(ClientOpts, true, &FailureReason);
      }
      catch (...)
      {}
    }

    if (!ToServer)
    {
      std::cerr << "FATAL: Connecting to the server failed:\n\t"
                << FailureReason << std::endl;
      return static_cast<int>(FrontendExitCode::SystemError);
    }

    ClientOpts.Connection = std::move(ToServer);
  }

  return client::main(ClientOpts);
}

namespace
{

void printHelp()
{
  std::cout << R"EOF(Usage:
    monomux --server [-vq...] [SERVER OPTIONS...]
    monomux [-vq...] [CLIENT OPTIONS...] [PROGRAM]
    monomux [-vq...] [CLIENT OPTIONS...] -- PROGRAM [ARGS...]
    monomux (-dD)
    monomux (-V[V])

                 MonoMux -- Monophone Terminal Multiplexer

MonoMux is a system tool that allows executing shell sessions and processes in
a separate session in the background, and allows multiple clients attach to the
sessions.

Shells and programs are executed by a server that is automatically created for
the user at the first interaction. The client program (started by default when
monomux is called) takes over the user's terminal and communicates data to and
from the shell or program running under the server. This way, if the client
exits (either because the user explicitly requested it doing so, or through a
SIGHUP signal, e.g. in the case of SSH), the remote process may still continue
execution in the background.

NOTE! Unlike other terminal session manager or multiplexer tools, such as screen
or tmux, MonoMux performs NO VT-SEQUENCE (the invisible control characters that
make an interactive terminal an enjoyable experience) PARSING or understanding!
To put it bluntly, MonoMux is **NOT A TERMINAL EMULATOR**! Data from the
underlying program is passed verbatim to the attached client(s).

Options:
    --server                    - Start the Monomux server explicitly, without
                                  creating a client, or any sessions. (This
                                  option should seldom be given by users.)
    -V[V]                       - Show version information about the executable.
                                  If repeated, elaborate build configuration,
                                  such as features, too.
    -v, --verbose               - Increase the verbosity of the built-in logging
                                  mechanism. Each '-v' supplied enables one more
                                  level. (Meaningless togethe with '-q'.)
    -q, --quiet                 - Decrease the verbosity of the built-in logging
                                  mechanism. Each '-q' supplied disables one
                                  more level. (Meaningless together with '-v'.)


Client options:
    PROGRAM [ARGS...]           - If the session specified by '-n' does not
                                  exist, MonoMux will create a new session, in
                                  which the PROGRAM binary (with ARGS... given
                                  as its command-line arguments) will be
                                  started.

                                  It is recommended to specify a shell as the
                                  program. Defaults to the user's default shell
                                  (SHELL environment variable), "/bin/bash", or
                                  "/bin/sh", in this order.

                                  If the arguments to be passed to the started
                                  program start with '-' or '--', the program
                                  invocation and MonoMux's arguments must be
                                  separated by an explicit '--':

                                      monomux -n session /bin/zsh

                                      monomux -n session -- /bin/bash --no-rc

    -e VAR=VAL, --env VAR=VAR   - Set the environment variable 'VAR' to have the
                                  value 'VAL' in the spawned session. If the
                                  client attaches to an existing session, this
                                  flag is ignored!
                                  This flag may be specified multiple times for
                                  multiple environment variables.
    -u VAR, --unset VAR         - Make the environment variable 'VAR' undefined
                                  in the spawned session. If the client attaches
                                  to an existing session, this flag is ignored!
                                  This flag may be specified multiple times for
                                  multiple environment variables.
    -s PATH, --socket PATH      - Path of the server socket to connect to.
    -n NAME, --name NAME        - Name of the remote session to attach to or
                                  create. (Defaults to an automatically
                                  generated value.)
    -l, --list                  - List the sessions that are running on the
                                  server listening on the socket given to
                                  '--socket', but do not attach or configure
                                  anything otherwise.
    -i, --interactive           - Always start the client with the session list,
                                  even if only at most one session exists on the
                                  server. (The default behaviour is to
                                  automatically create a session or attach in
                                  this case.)


In-session options:
    -d, --detach                - When executed from within a running session,
                                  detach the CURRENT client.
    -D, --detach-all            - When executed from within a running session,
                                  detach ALL clients attached to that session.


Server options:
    -s PATH, --socket PATH      - Path of the sever socket to create and await
                                  clients on.
    -k, --keepalive             - Do not automatically shut the server down if
                                  the only session running in it had exited.
    -N, --no-daemon             - Do not daemonise (put the running server into
                                  the background) automatically. Implies '-k'.
)EOF";
  std::cout << std::endl;
}

void printVersion()
{
  std::cout << "MonoMux version " << monomux::getFullVersion() << std::endl;
}

void printFeatures()
{
  std::cout << "Features:\n"
            << monomux::getHumanReadableConfiguration() << std::endl;
}

void coreDumped(monomux::system::SignalHandling::Signal SigNum,
                ::siginfo_t* /* Info */,
                const monomux::system::SignalHandling* Handling)
{
  using namespace monomux;
  using namespace monomux::system;

  // Reset the signal handler for the current signal, so all other processes
  // and logics properly receive the fact that we are ending, anyway...
  SignalHandling::get().defaultCallback(SigNum);

  const volatile auto* ModulePtr = std::any_cast<const char*>(
    Handling->getObject(SignalHandling::ModuleObjName));
  const char* Module = ModulePtr ? *ModulePtr : "<Unknown>";
  LOG(fatal) << "in '" << Module << "' - FATAL SIGNAL " << SigNum << " '"
             << SignalHandling::signalName(SigNum) << "' RECEIVED!";

  unix::Backtrace BT;
  BT.prettify();

  std::cerr << "- * - * - * - * - * - * - * - * - * - * - * - * - * - * - * - "
               "* - * - * - * - * - * - * - * - * - * - * - * -\n";
  std::cerr << "\t\tMonomux (v" << getFullVersion() << ") has crashed!\n";
  std::cerr << "---------------------------------------------------------------"
               "-----------------------------------------------\n";
  std::cerr << '\n';
  std::cerr << getHumanReadableConfiguration() << '\n';
  std::cerr << "---------------------------------------------------------------"
               "-----------------------------------------------\n";
  printBacktrace(std::cerr, BT);
  std::cerr << "- * - * - * - * - * - * - * - * - * - * - * - * - * - * - * - "
               "* - * - * - * - * - * - * - * - * - * - * - * -\n";
}

} // namespace

#undef LOG
