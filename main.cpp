#include "stdafx.h"
#if __has_include("version_generated.h")
#  include "version_generated.h"
#endif
#ifndef COMPONENT_VERSION
#  define COMPONENT_VERSION "1.0.0"
#endif

DECLARE_COMPONENT_VERSION(
    "Navidrome Subsonic Client",
    COMPONENT_VERSION,
    "Streams music from Navidrome (or any Subsonic-compatible server) via the Subsonic API.\n"
    "\n"
    "Configuration: Preferences > Tools > Navidrome\n"
    "Browse: File > Open Navidrome Browser\n"
    "\n"
    "https://www.navidrome.org/"
);

VALIDATE_COMPONENT_FILENAME("foo_navidrome.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
