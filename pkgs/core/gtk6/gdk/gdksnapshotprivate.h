#pragma once

#include "gdksnapshot.h"

G_BEGIN_DECLS

struct _GdkSnapshot
{
  GObject parent_instance;
};

struct _GdkSnapshotClass {
  GObjectClass parent_class;
};

G_END_DECLS

