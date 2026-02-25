// Embedded icon data - auto-generated PNG icons compiled into firmware
// SD card icons override these if present at /icons/
#pragma once

#include "icons/icon1_png.h"
#include "icons/icon2_png.h"
#include "icons/icon3_png.h"
#include "icons/icon4_png.h"
#include "icons/icon5_png.h"
#include "icons/icon6_png.h"
#include "icons/icon7_png.h"
#include "icons/icon8_png.h"
#include "icons/back_png.h"
#include "icons/next_png.h"
#include "icons/return_png.h"

// Lookup table for embedded icons by name
struct EmbeddedIcon {
  const char* name;
  const uint8_t* data;
  size_t length;
};

const EmbeddedIcon embeddedIcons[] = {
  { "icon1.png",  icon1_png,  icon1_png_len  },
  { "icon2.png",  icon2_png,  icon2_png_len  },
  { "icon3.png",  icon3_png,  icon3_png_len  },
  { "icon4.png",  icon4_png,  icon4_png_len  },
  { "icon5.png",  icon5_png,  icon5_png_len  },
  { "icon6.png",  icon6_png,  icon6_png_len  },
  { "icon7.png",  icon7_png,  icon7_png_len  },
  { "icon8.png",  icon8_png,  icon8_png_len  },
  { "back.png",   back_png,   back_png_len   },
  { "next.png",   next_png,   next_png_len   },
  { "return.png", return_png, return_png_len },
};

const int embeddedIconCount = sizeof(embeddedIcons) / sizeof(embeddedIcons[0]);

// Find an embedded icon by filename. Returns nullptr if not found.
inline const EmbeddedIcon* findEmbeddedIcon(const char* name) {
  for (int i = 0; i < embeddedIconCount; i++) {
    if (strcmp(embeddedIcons[i].name, name) == 0) {
      return &embeddedIcons[i];
    }
  }
  return nullptr;
}
