/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2019, OpenROAD
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include <array>
#include <stdio.h>
#include <tcl.h>
// We have had too many problems with this std::filesytem on various platforms
// so it is disabled but kept for future reference
#ifdef USE_STD_FILESYSTEM
#include <filesystem>
#endif
#ifdef ENABLE_READLINE
  // If you get an error on this include be sure you have
  //   the package tcl-tclreadline-devel installed
  #include <tclreadline.h>
#endif

#include "sta/StringUtil.hh"
#include "sta/StaMain.hh"
#include "openroad/Version.hh"
#include "openroad/Error.hh"
#include "openroad/InitOpenRoad.hh"
#include "openroad/OpenRoad.hh"
#include "openroad/Logger.h"
#include "gui/gui.h"

using sta::stringEq;
using sta::findCmdLineFlag;
using sta::stringPrintTmp;
using sta::sourceTclFile;

static int cmd_argc;
static char **cmd_argv;
bool gui_mode = false;
static const char *init_filename = ".openroad";

static void
showUsage(const char *prog,
	  const char *init_filename);

int
main(int argc,
     char *argv[])
{
  ordlog::init();
  ordlog::info(ordlog::OPENROAD,1,"Starting OpenROAD main");
  if (argc == 2 && stringEq(argv[1], "-help")) {
    showUsage(argv[0], init_filename);
    return 0;
  }
  if (argc == 2 && stringEq(argv[1], "-version")) {
    printf("%s %s\n", OPENROAD_VERSION, OPENROAD_GIT_SHA1);
    return 0;
  }
  cmd_argc = argc;
  cmd_argv = argv;
  if (findCmdLineFlag(cmd_argc, cmd_argv, "-gui")) {
    gui_mode = true;
    return gui::start_gui(cmd_argc, cmd_argv);
  }
  // Set argc to 1 so Tcl_Main doesn't source any files.
  // Tcl_Main never returns.
  Tcl_Main(1, argv, ord::tclAppInit);
  return 0;
}

#ifdef ENABLE_READLINE
static int
tclReadlineInit(Tcl_Interp *interp)
{
  std::array<const char *, 8> readline_cmds = {
    "history",
    "history event",
    "eval $auto_index(::tclreadline::ScriptCompleter)",
    "::tclreadline::readline builtincompleter true",
    "::tclreadline::readline customcompleter ::tclreadline::ScriptCompleter",
    "proc ::tclreadline::prompt1 {} { return \"openroad " OPENROAD_VERSION "> \" }",
    "proc ::tclreadline::prompt2 {} { return \"...> \" }",
    "::tclreadline::Loop"
  };

  for (auto cmd : readline_cmds) {
    if (TCL_ERROR == Tcl_Eval(interp, cmd)) {
      return TCL_ERROR;
    }
  }
  return TCL_OK;
}
#endif

// Tcl init executed inside Tcl_Main.
static int
tclAppInit(int argc,
           char *argv[],
           const char *init_filename,
           Tcl_Interp *interp)
{
  // source init.tcl
  if (Tcl_Init(interp) == TCL_ERROR) {
    return TCL_ERROR;
  }
#ifdef ENABLE_READLINE
  if (!gui_mode) {
    if (Tclreadline_Init(interp) == TCL_ERROR) {
      return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "tclreadline", Tclreadline_Init, Tclreadline_SafeInit);
    if (Tcl_EvalFile(interp, TCLRL_LIBRARY "/tclreadlineInit.tcl") != TCL_OK) {
      printf("Failed to load tclreadline\n");
    }
  }
#endif
  ord::initOpenRoad(interp);

  if (!findCmdLineFlag(argc, argv, "-no_splash"))
    Tcl_Eval(interp, "show_openroad_splash");

  bool exit_after_cmd_file = findCmdLineFlag(argc, argv, "-exit");

  if (!findCmdLineFlag(argc, argv, "-no_init")) {
#ifdef USE_STD_FILESYSTEM
    std::filesystem::path init(getenv("HOME"));
    init /= init_filename;
    if (std::filesystem::is_regular_file(init)) {
      sourceTclFile(init.c_str(), true, true, interp);
    }
#else
    char *init_path = stringPrintTmp("[file join $env(HOME) %s]",init_filename);
    sourceTclFile(init_path, true, true, interp);
#endif
  }

  if (argc > 2 ||
      (argc > 1 && argv[1][0] == '-'))
    showUsage(argv[0], init_filename);
  else {
    if (argc == 2) {
      char *cmd_file = argv[1];
      if (cmd_file) {
	int result = sourceTclFile(cmd_file, false, false, interp);
        if (exit_after_cmd_file) {
          int exit_code = (result == TCL_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
          exit(exit_code);
        }
      }
    }
  }
#ifdef ENABLE_READLINE
  if (!gui_mode) {
    return tclReadlineInit(interp);
  }
#endif
  return TCL_OK;
}

int
ord::tclAppInit(Tcl_Interp *interp)
{
  return tclAppInit(cmd_argc, cmd_argv, init_filename, interp);
}


static void
showUsage(const char *prog,
	  const char *init_filename)
{
  printf("Usage: %s [-help] [-version] [-no_init] [-exit] cmd_file\n", prog);
  printf("  -help              show help and exit\n");
  printf("  -version           show version and exit\n");
  printf("  -no_init           do not read %s init file\n", init_filename);
  printf("  -threads count|max use count threads\n");
  printf("  -no_splash         do not show the license splash at startup\n");
  printf("  -exit              exit after reading cmd_file\n");
  printf("  -gui               start in gui mode\n");
  printf("  cmd_file           source cmd_file\n");
}

