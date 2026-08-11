#include "stdafx.h"
#if __has_include("version_generated.h")
#  include "version_generated.h"
#endif
#ifndef COMPONENT_VERSION
#  define COMPONENT_VERSION "1.0.0"
#endif

#ifdef _WIN32
#  define NAVIDROME_COMPONENT_DESCRIPTION \
    u8"通过 Subsonic API 从 Navidrome（或任何兼容 Subsonic 的服务器）播放音乐。\n" \
    u8"\n" \
    u8"配置：偏好设置 > 工具 > Navidrome\n" \
    u8"浏览：文件 > 打开 Navidrome 浏览器\n" \
    u8"\n" \
    u8"https://www.navidrome.org/"
#else
#  define NAVIDROME_COMPONENT_DESCRIPTION \
    "Streams music from Navidrome (or any Subsonic-compatible server) via the Subsonic API.\n" \
    "\n" \
    "Configuration: Preferences > Tools > Navidrome\n" \
    "Browse: File > Open Navidrome Browser\n" \
    "\n" \
    "https://www.navidrome.org/"
#endif

DECLARE_COMPONENT_VERSION(
    "Navidrome Subsonic Client",
    COMPONENT_VERSION,
    NAVIDROME_COMPONENT_DESCRIPTION
);

#undef NAVIDROME_COMPONENT_DESCRIPTION

VALIDATE_COMPONENT_FILENAME("foo_navidrome.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
